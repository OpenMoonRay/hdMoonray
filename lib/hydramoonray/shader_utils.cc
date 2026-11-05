// Copyright 2023-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "shader_utils.h"
#include "renderDelegate.h"
#include "ValueConverter.h"
#include "light.h"
#include "shader_utils.h"

#include <pxr/imaging/hd/material.h>

// turn this on to use a proxy SwitchMaterial for terminals, 
// so that assignments remain valid even if the material network is modified
// #define USE_PROXY_TERMINAL 1

using namespace pxr;
using namespace hdMoonray;
namespace {

const std::string rdlClassSwitchMaterial("SwitchMaterial");
const std::string rdlAttrSwitchChoice("choice");
const std::string rdlAttrSwitchMaterial0("material0");

// get an object attribute from a parameter value,
// depending on the interface type of the attribute.
// There is no way to directly specify an object parameter value in UsdShade,
// so we have to use a string path and look up the object.
MoonrayObject
getObjectAttributeValue(
    const MoonrayAttribute& attribute,
    const VtValue& val, 
    HdMoonray_RenderDelegate& renderDelegate,
    HdSceneDelegate *sceneDelegate)
{
    // LightSet RDL attributes are set from a list of light paths in USD.
    if (attribute.hasInterfaceType(hdMoonray::MoonrayAttribute::InterfaceType::INTERFACE_LIGHTSET) &&
        val.IsHolding<VtStringArray>()) {
            std::set<MoonrayObject> lights;
            VtStringArray strings = val.UncheckedGet<VtStringArray>();
            for (std::string s : strings) {
                SdfPath lightPath(s);
                hdMoonray::MoonrayObject rdlLight = hdMoonray::HdMoonray_Light::lightForMaterialLink(sceneDelegate, lightPath);
                if (rdlLight.isValid()) {
                    lights.insert(rdlLight);
                }
            }
            return renderDelegate.scene().getLightSet(lights);
    }
    return MoonrayObject();
}

// lookup for node connections
// attrname -> path of the connected node
using Connections = std::unordered_map<std::string, SdfPath>;

// construct a Moonray shader SceneObject from a HdMaterialNode, and set its parameters
MoonrayObject
makeMoonrayShader(
    HdMoonray_RenderDelegate& renderDelegate,
    HdSceneDelegate *sceneDelegate,
    const HdMaterialNode& node,
    const Connections& connections,
    const SdfPath& nodeId) 
{
    MoonrayObject shaderObj = renderDelegate.scene().create(node.identifier.GetString(), nodeId);
    if (shaderObj.isValid()) {
        try {
            UpdateGuard guard(shaderObj); 
            for (auto attrIt = shaderObj.beginAttributes(); attrIt != shaderObj.endAttributes(); ++attrIt) {
                const std::string& attrName = (*attrIt).name();
                // check first for a connection
                auto connIt = connections.find(attrName);
                if (connIt != connections.end()) {
                    MoonrayObject input = renderDelegate.scene().getObject(connIt->second);
                    (*attrIt).set(input);
                    continue;
                }

                // if no connection, look for a direct value in the node parameters
                (*attrIt).bind(MoonrayObject());

                auto valIt = node.parameters.find(TfToken(attrName));
                if (valIt != node.parameters.end()) {
                    // Object valued RDL attributes are handled specially.
                    if ((*attrIt).type() == MoonrayAttribute::Type::TYPE_SCENE_OBJECT) {
                        MoonrayObject obj = getObjectAttributeValue(*attrIt, valIt->second, renderDelegate, sceneDelegate);
                        (*attrIt).set(obj);
                    } else {
                        (*attrIt).set(valIt->second);
                    }
                } else {
                    (*attrIt).setToDefault();
                }
            }
        } catch (const std::exception& e) {
            Logger::error(node.path, ": ", e.what());
            return MoonrayObject();
        }
    }
    return shaderObj;
}

// helper for getTerminalInternal
std::string
getNodeWithChannelName(const std::string& nodePath,
                       const std::string& channelName)
{
    return nodePath + std::string(".") + channelName;
}

}

namespace hdMoonray {

    void dumpMaterialNetworkMap(const HdMaterialNetworkMap& networkmap);

MoonrayObject
getTerminalInternal(const SdfPath& id, 
            const std::string& terminalName, 
            HdMoonray_RenderDelegate& renderDelegate, 
            HdSceneDelegate* sceneDelegate)
{
    VtValue resource = sceneDelegate->GetMaterialResource(id);
    if (!resource.IsHolding<HdMaterialNetworkMap>()) {
        return MoonrayObject();
    }
    const HdMaterialNetworkMap& networkmap = resource.UncheckedGet<HdMaterialNetworkMap>();
    auto iter = networkmap.map.find(TfToken(terminalName));
    if (iter == networkmap.map.end()) {
        return MoonrayObject();
    }

    const HdMaterialNetwork & network = iter->second;

    // collect the connections for lookup by output node and output attribute name
    // (HDM-424 : we want to avoid marking the bindings dirty by clearing and re-setting them)
    TfHashMap<SdfPath, Connections, SdfPath::Hash> connections;
    for (const HdMaterialRelationship& rel : network.relationships) {
        Connections& conn = connections[rel.outputId.MakeAbsolutePath(id)];
        conn[rel.outputName.GetString()] = rel.inputId.MakeAbsolutePath(id);
    }

    MoonrayObject last;
    for (const HdMaterialNode& node : network.nodes) {
        MoonrayObject next;

        // Moonray name must be absolute so that it is unique in the scene.
        // (HDM-401 : USD 0.25.5 returns relative node paths)
        SdfPath absPath = node.path.MakeAbsolutePath(id);
        next = makeMoonrayShader(renderDelegate,
                                 sceneDelegate,
                                 node,
                                 connections[absPath],
                                 absPath);
        if (next.isValid()) last = next; // HDM-368 : don't give up on error
    }

    return last.isValid() ? last : MoonrayObject();
}

MoonrayObject
getTerminal(const SdfPath& id,
            const std::string& terminalName,
            HdMoonray_RenderDelegate& renderDelegate,
            HdSceneDelegate* sceneDelegate)
{
    MoonrayObject terminal = getTerminalInternal(id, terminalName, renderDelegate, sceneDelegate);
#if defined(USE_PROXY_TERMINAL)
    // where possible, we return a "proxy" object that has the same effect as the actual terminal,
    // but is named independently of the material network content. Assignments of the proxy will remain valid
    // even if the material network is modified in a way that changes the rdl2 name of the terminal.
    if (terminal.isValid() && terminal.hasInterfaceType(MoonrayAttribute::InterfaceType::INTERFACE_MATERIAL)) {
        MoonrayObject proxy = renderDelegate.scene().createObject(rdlClassSwitchMaterial, id, "_" + terminalName);
        if (proxy.isValid()) {
            UpdateGuard guard(proxy);
            proxy.set(rdlAttrSwitchChoice, 0);
            proxy.set(rdlAttrSwitchMaterial0, terminal);
            return proxy;
        }
    }
#endif
    return terminal;
}

MoonrayObject
getNodeByConnection(const SdfPath& id, 
                    const std::string& paramName,
                    HdMoonray_RenderDelegate& renderDelegate, 
                    HdSceneDelegate* sceneDelegate)
{
    MoonrayObject ret = MoonrayObject();
    VtValue hdMatVal = sceneDelegate->GetMaterialResource(id);
    if (!hdMatVal.IsHolding<HdMaterialNetworkMap>()) {
        return ret;
    }
    const HdMaterialNetworkMap& networkmap = hdMatVal.UncheckedGet<HdMaterialNetworkMap>();
    SdfPath inputId;
    for (auto const& iter : networkmap.map) {
        const HdMaterialNetwork & network = iter.second;
        for (const HdMaterialRelationship& rel : network.relationships) {
            if (rel.outputName == paramName){
                inputId = rel.inputId;
                break;
            }
        }
        if (inputId.IsEmpty())
            continue;

        // found the connected network, import all the shader nodes
        // and set the connected one
        Connections connections;
        for (const HdMaterialNode & node : network.nodes) {
            // we have to skip the actual light filter node
            if (node.identifier == "MoonrayLightFilter" || 
                node.path == id) {
                continue;
            }
            const std::string nodeName = node.path.GetString();
            MoonrayObject shader = makeMoonrayShader(renderDelegate, 
                                                     sceneDelegate, 
                                                     node, 
                                                     connections,
                                                     node.path.MakeAbsolutePath(id));
            if (shader.isNull()) continue;
            if (node.path == inputId) ret = shader;
        }
    }
    return ret;
}

void
dumpMaterialNetworkMap(const HdMaterialNetworkMap& networkmap)
{
    std::cout << "=== Material Networks ===" << std::endl;
    for (auto const& iter : networkmap.map) {
        const TfToken & terminalName = iter.first;
        const HdMaterialNetwork & network = iter.second;

        std::cout << "Terminal '" << terminalName << "':" << std::endl;

        std::cout << "  primvars:";
        for (const TfToken& primvarName : network.primvars) {
            std::cout << " " << primvarName;
        }
        std::cout << std::endl;

        unsigned i = 0;
        for (const HdMaterialNode & node : network.nodes) {
            std::cout << "  node " << i++ << ": " << node.identifier << " " << node.path << std::endl;
            for (auto const& jter : node.parameters) {
                std::cout << "    " << jter.first << " = " << jter.second
                          << " [" << jter.second.GetTypeName() << "] " << std::endl;
            }
        }

        for (const HdMaterialRelationship& rel : network.relationships) {
            std::cout << "  " << rel.outputId << "." << rel.outputName
                      << " bound to "<< rel.inputId << "." << rel.inputName
                      <<  std::endl;
        }
        std::cout << "---" << std::endl;
    }

    std::cout << "=========================" << std::endl;
}


} // namespace hdMoonray