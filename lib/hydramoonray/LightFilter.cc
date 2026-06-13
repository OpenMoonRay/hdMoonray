// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "LightFilter.h"
#include "RenderDelegate.h"
#include "ValueConverter.h"
#include "Material.h"
#include "Camera.h"
#include "HdmLog.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usdLux/tokens.h>

// nb, must use full name scene_rdl2::rdl2::LightFilter...
#include <scene_rdl2/scene/rdl2/LightFilter.h>
#include <scene_rdl2/scene/rdl2/Layer.h>
#include <scene_rdl2/scene/rdl2/Material.h>
#include <scene_rdl2/render/logging/logging.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

using namespace scene_rdl2::rdl2;

namespace {
using scene_rdl2::logging::Logger;

pxr::TfToken moonrayClassToken("moonray:class");
pxr::TfToken infoIdToken("info:id");
pxr::TfToken fallbackClass("DecayLightFilter");

void
makeLightFilterInert(scene_rdl2::rdl2::LightFilter* lightFilter,
                     hdMoonray::RenderDelegate& renderDelegate)
{
    if (!lightFilter) {
        return;
    }

    hdMoonray::UpdateGuard guard(renderDelegate, lightFilter);
    const SceneClass& sceneClass = lightFilter->getSceneClass();
    try {
        const Attribute* onAttr = sceneClass.getAttribute("on");
        if (onAttr && onAttr->getType() == TYPE_BOOL) {
            lightFilter->set(AttributeKey<Bool>(*onAttr), false);
        }
    } catch (const std::exception&) {
        // Some future/custom LightFilter classes may not expose "on". RDL
        // objects cannot be deleted interactively, so category release and
        // pointer clearing still make the object inert from hdMoonray's side.
    }
}

bool
isMoonRayLightFilterClass(const std::string& className,
                          hdMoonray::RenderDelegate& renderDelegate)
{
    try {
        const SceneClass* sceneClass = renderDelegate.acquireSceneContext().createSceneClass(className);
        return sceneClass && (sceneClass->getDeclaredInterface() & INTERFACE_LIGHTFILTER);
    } catch (const std::exception&) {
        return false;
    }
}

std::string
normalizedRampToken(const std::string& value)
{
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) {
                       if (c == '-' || c == ' ') return '_';
                       return static_cast<char>(std::tolower(c));
                   });
    return result;
}

bool
rampInterpolationTokenToInt(const std::string& token, int* out)
{
    const std::string key = normalizedRampToken(token);

    if (key == "none" || key == "constant") {
        *out = 0;
        return true;
    }
    if (key == "linear") {
        *out = 1;
        return true;
    }
    if (key == "exponential_up" || key == "ease_in") {
        *out = 2;
        return true;
    }
    if (key == "exponential_down" || key == "ease_out") {
        *out = 3;
        return true;
    }
    if (key == "smooth" || key == "smoothstep" || key == "hermite" ||
        key == "bezier" || key == "bspline" || key == "b_spline") {
        *out = 4;
        return true;
    }
    if (key == "catmull_rom" || key == "catmullrom") {
        *out = 5;
        return true;
    }
    if (key == "monotone_cubic" || key == "monotonecubic") {
        *out = 6;
        return true;
    }
    return false;
}

int
getAuthoredRampCount(const pxr::SdfPath& id,
                     const std::string& attrName,
                     pxr::HdSceneDelegate* sceneDelegate)
{
    // Prefer the authored ramp position array length because it is the value
    // that must match the ramp value and interpolation vectors. inputs:ramp is
    // authored by Houdini as the UI point count and is only used as a fallback.
    const char* positionsName = nullptr;
    if (attrName == "interpolation_types") {
        positionsName = "inputs:distances";
    } else if (attrName == "ramp_interpolation_types") {
        positionsName = "inputs:ramp_in_distances";
    } else if (attrName == "density_remap_interpolation_types") {
        positionsName = "inputs:density_remap_inputs";
    }

    if (positionsName) {
        pxr::VtValue positions = sceneDelegate->GetLightParamValue(id, pxr::TfToken(positionsName));
        if (positions.IsHolding<pxr::VtArray<float>>()) {
            return static_cast<int>(positions.UncheckedGet<pxr::VtArray<float>>().size());
        }
    }

    pxr::VtValue countVal = sceneDelegate->GetLightParamValue(id, pxr::TfToken("inputs:ramp"));
    if (countVal.IsHolding<int>()) {
        return countVal.UncheckedGet<int>();
    }
    if (countVal.IsHolding<long>()) {
        return static_cast<int>(countVal.UncheckedGet<long>());
    }
    if (countVal.IsHolding<long long>()) {
        return static_cast<int>(countVal.UncheckedGet<long long>());
    }

    return 0;
}

pxr::VtValue
expandRampInterpolationToken(const pxr::VtValue& val,
                             const pxr::SdfPath& id,
                             const std::string& attrName,
                             pxr::HdSceneDelegate* sceneDelegate)
{
    if (attrName.find("interpolation") == std::string::npos) {
        return val;
    }

    std::string token;
    if (val.IsHolding<pxr::TfToken>()) {
        token = val.UncheckedGet<pxr::TfToken>().GetString();
    } else if (val.IsHolding<std::string>()) {
        token = val.UncheckedGet<std::string>();
    } else {
        return val;
    }

    int interpolation = 0;
    if (!rampInterpolationTokenToInt(token, &interpolation)) {
        return val;
    }

    // ValueConverter can expand a scalar ramp token using the RDL scene-class
    // default vector size. Solaris light filter ramps can author a different
    // editable point count on the prim, so expand to that authored count here
    // before generic conversion.
    const int count = getAuthoredRampCount(id, attrName, sceneDelegate);
    if (count <= 0) {
        return val;
    }

    pxr::VtArray<int> values;
    values.resize(count);
    std::fill(values.begin(), values.end(), interpolation);
    return pxr::VtValue(values);
}

#if PXR_VERSION >= 2005
# define lightFilterToken (pxr::HdPrimTypeTokens->lightFilter)
# define lightFilterLinkToken (pxr::HdTokens->lightFilterLink)
#else
pxr::TfToken lightFilterToken("lightFilter");
pxr::TfToken lightFilterLinkToken("lightFilterLink");
#endif
}

namespace hdMoonray {

using scene_rdl2::logging::Logger;

pxr::HdDirtyBits
LightFilter::GetInitialDirtyBitsMask() const
{
    return pxr::HdChangeTracker::DirtyParams;
}

void
LightFilter::syncParams(const pxr::SdfPath& id,
                  pxr::HdSceneDelegate *sceneDelegate,
                  RenderDelegate& renderDelegate)
{
    const SceneClass& sceneClass = mLightFilter->getSceneClass();

    for (auto it = sceneClass.beginAttributes();
         it != sceneClass.endAttributes(); ++it) {

        const std::string& attrName = (*it)->getName();
        if (attrName == "node_xform") {
            syncXform(id, sceneDelegate);
        } else if (attrName == "projector") {
            syncProjector(id,sceneDelegate, renderDelegate);
        } else if (attrName == "texture_map") {
            syncTextureMap(id, sceneDelegate, renderDelegate);
        } else  if (attrName == "light_filters") {
            syncCombineFilters(id, sceneDelegate, renderDelegate);
        } else {
            pxr::TfToken moonrayName("moonray:" + attrName);
            pxr::VtValue val = sceneDelegate->GetLightParamValue(id, moonrayName);
            if (val.IsEmpty()) {
                pxr::TfToken inputName("inputs:" + attrName);
                val = sceneDelegate->GetLightParamValue(id, inputName);
            }
            if (val.IsEmpty()) {
                ValueConverter::setDefault(mLightFilter, *it);
            } else {
                val = expandRampInterpolationToken(val, id, attrName, sceneDelegate);
                ValueConverter::setAttribute(mLightFilter, *it, val);
            }
        }
    }
}

void
LightFilter::syncProjector(const pxr::SdfPath& id,
                         pxr::HdSceneDelegate *sceneDelegate,
                         RenderDelegate& renderDelegate) 
{
    // sync the "projector" attribute of Cookie light filters,
    // which is authored as a "rel" to a camera. Note that this
    // requires the custom MoonrayLightFilterAdapter to work
    static pxr::TfToken projectorToken("moonray:projector");
    pxr::VtValue val = sceneDelegate->Get(id, projectorToken); // supplied by adapter
    if (val.IsHolding<pxr::SdfPath>()) {
        pxr::SdfPath path = val.UncheckedGet<pxr::SdfPath>();
        path = path.ReplacePrefix(pxr::SdfPath::AbsoluteRootPath(), sceneDelegate->GetDelegateID());
        SceneObject* so = hdMoonray::Camera::createCamera(sceneDelegate, renderDelegate, path);
        mLightFilter->set("projector", so);
        if (not so) {
            Logger::error(GetId(), ".moonray:projector: ", path, " not found");
        }
    } else if (not val.IsEmpty()) {
        Logger::error(GetId(), ".moonray:projector: must be a path");
    }
}

void
LightFilter::syncXform(const pxr::SdfPath& id,
                       pxr::HdSceneDelegate *sceneDelegate)
{
    // some light filters (e.g. Rod) have a blurrable node_xform attr
    // (although not actually inherited from Node, so don't use Node's attribute key)
    // Do not call this function without first verifying that the
    // light filter has an attribute called "node_xform"
    pxr::HdTimeSampleArray<pxr::GfMatrix4d, 4> sampledXforms;
    sceneDelegate->SampleTransform(id, &sampledXforms);
    // if there's only one sample, it should match the cached value
    if (sampledXforms.count <= 1) {
        scene_rdl2::rdl2::Mat4d rdl2Xform0 =
            reinterpret_cast<scene_rdl2::rdl2::Mat4d&>(sampledXforms.values[0]);
        mLightFilter->set("node_xform", rdl2Xform0);
    } else {
        // first and last samples will be sample interval boundaries
        scene_rdl2::rdl2::Mat4d rdl2Xform0 =
            reinterpret_cast<scene_rdl2::rdl2::Mat4d&>(sampledXforms.values[0]);
        mLightFilter->set("node_xform", rdl2Xform0);
        scene_rdl2::rdl2::Mat4d rdl2Xform1 =
            reinterpret_cast<scene_rdl2::rdl2::Mat4d&>(sampledXforms.values[sampledXforms.count-1]);
        mLightFilter->set("node_xform", rdl2Xform1, scene_rdl2::rdl2::TIMESTEP_END);
   }
}

void
LightFilter::syncCombineFilters(const pxr::SdfPath& id,
                                pxr::HdSceneDelegate *sceneDelegate,
                                RenderDelegate& renderDelegate)
{
    // sync the "light_filters" attribute of Combine light filters,
    // which is authored as "rel"s to light filters. Note that this
    // requires the custom MoonrayLightFilterAdapter to work
    static pxr::TfToken filtersToken("moonray:light_filters");
    pxr::VtValue val = sceneDelegate->Get(id, filtersToken); // supplied by adapter
    if (val.IsHolding<pxr::SdfPathVector>()) {
        scene_rdl2::rdl2::SceneObjectVector rdlObjects;
        pxr::SdfPathVector pathVec = val.UncheckedGet<pxr::SdfPathVector>();
        for (const pxr::SdfPath& cpath : pathVec) {
            pxr::SdfPath path(cpath);
            path = path.ReplacePrefix(pxr::SdfPath::AbsoluteRootPath(), sceneDelegate->GetDelegateID());
            SceneObject* so = LightFilter::getFilter(sceneDelegate, renderDelegate, path);
            if (so) {
                rdlObjects.push_back(so);
            } else {
                Logger::error(GetId(), ".moonray:light_filters: ", path, " not found");
            }
        }
        mLightFilter->set("light_filters", rdlObjects);
    } else if (not val.IsEmpty()) {
        Logger::error(GetId(), ".moonray:light_filters: must be a list of paths");
    }
}

void
LightFilter::syncTextureMap(const pxr::SdfPath& id,
                            pxr::HdSceneDelegate *sceneDelegate,
                            RenderDelegate& renderDelegate)
{

    const std::string moonrayName = "moonray:texture_map";
    scene_rdl2::rdl2::SceneObject* shader = nullptr;
    pxr::VtValue hdMatVal = sceneDelegate->GetMaterialResource(id);
    if (!hdMatVal.IsHolding<pxr::HdMaterialNetworkMap>()) {
        return;
    }
    const pxr::HdMaterialNetworkMap& networkmap = hdMatVal.UncheckedGet<pxr::HdMaterialNetworkMap>();
    pxr::SdfPath inputId;
    for (auto const& iter : networkmap.map) {
        const pxr::HdMaterialNetwork & network = iter.second;
        for (const pxr::HdMaterialRelationship& rel : network.relationships) {
            if (rel.outputName == moonrayName){
                inputId = rel.inputId;
                break;
            }
        }
        if (inputId.IsEmpty())
            continue;

        // found the connected network, import all the shader nodes
        // and set the connected one
        for (const pxr::HdMaterialNode & node : network.nodes) {
            // we have to skip the actual light filter node
            if (node.identifier == "MoonrayLightFilter" || 
                node.path == id) {
                continue;
            }
            const std::string nodeName = node.path.GetString();
            shader = makeMoonrayShader(renderDelegate, sceneDelegate, node, nodeName, nullptr);
            if (!shader) {
                continue;
            }
            if (node.path == inputId){
                mLightFilter->set("texture_map", shader);
            }
        }
    }

}

// Hydra doesn't currently seem to analyse the dependency between light and light filter
// correctly, so a light may be synced before the filters it references. To work around
// this, we need to allow the light to create the filter outside Sync, under a mutex...
scene_rdl2::rdl2::LightFilter*
LightFilter::getOrCreateFilter(pxr::HdSceneDelegate *sceneDelegate,
                               RenderDelegate& renderDelegate,
                               const pxr::SdfPath& id)
{
    std::lock_guard<std::mutex> lock(mCreateMutex);

    pxr::VtValue vtClass = sceneDelegate->GetLightParamValue(id, moonrayClassToken);
    if (vtClass.IsEmpty()) {
        vtClass = sceneDelegate->GetLightParamValue(id, infoIdToken);
    }

    pxr::TfToken classToken;
    if (vtClass.IsHolding<pxr::TfToken>()) {
        classToken = vtClass.UncheckedGet<pxr::TfToken>();
    } else if (vtClass.IsHolding<std::string>()) {
        classToken = pxr::TfToken(vtClass.UncheckedGet<std::string>());
    } else {
        classToken = fallbackClass;
        Logger::warn("hdMoonray: Unspecified LightFilter type : creating ", classToken);
    }

    if (!isMoonRayLightFilterClass(classToken.GetString(), renderDelegate)) {
        Logger::error(id, ": invalid MoonRay LightFilter class '", classToken, "'");
        if (mLightFilter) {
            makeLightFilterInert(mLightFilter, renderDelegate);
            renderDelegate.releaseCategory(mLightFilter, RenderDelegate::CategoryType::FilterLink, mLightFilterCategory);
            mLightFilter = nullptr;
            mLightFilterCategory = pxr::TfToken();
        }
        return nullptr;
    }

    if (mLightFilter && mLightFilter->getSceneClass().getName() != classToken.GetString()) {
        makeLightFilterInert(mLightFilter, renderDelegate);
        renderDelegate.releaseCategory(mLightFilter, RenderDelegate::CategoryType::FilterLink, mLightFilterCategory);
        mLightFilter = nullptr;
        mLightFilterCategory = pxr::TfToken();
    }

    if (not mLightFilter) {
        SceneObject* sceneObject = renderDelegate.createSceneObject(classToken.GetString(), id);
        if (!sceneObject) {
            Logger::error(id, ": failed to create MoonRay LightFilter class '", classToken, "'");
            return nullptr;
        }
        mLightFilter = sceneObject->asA<scene_rdl2::rdl2::LightFilter>();
        if (!mLightFilter) {
            Logger::error(id, ": MoonRay scene object class '", classToken, "' is not a LightFilter");
            return nullptr;
        }

        // See Light.cc for explanation...
        pxr::VtValue val = sceneDelegate->GetLightParamValue(id, lightFilterLinkToken);
        if (val.IsHolding<pxr::TfToken>()) {
            mLightFilterCategory = val.UncheckedGet<pxr::TfToken>();
        }
        renderDelegate.setCategory(mLightFilter,RenderDelegate::CategoryType::FilterLink,mLightFilterCategory);
    }
    return mLightFilter;
}

void
LightFilter::Sync(pxr::HdSceneDelegate *sceneDelegate,
                  pxr::HdRenderParam   *renderParam,
                  pxr::HdDirtyBits     *dirtyBits)
{
    pxr::SdfPath id = GetId();
    hdmLogSyncStart("LightFilter", id, dirtyBits);
    RenderDelegate& renderDelegate(RenderDelegate::get(renderParam));

    if (!getOrCreateFilter(sceneDelegate, renderDelegate, id)) {
        *dirtyBits = pxr::HdChangeTracker::Clean;
        hdmLogSyncEnd(id);
        return;
    }

    if ((*dirtyBits) & pxr::HdChangeTracker::DirtyParams) {
        UpdateGuard guard(renderDelegate, mLightFilter);
        syncParams(id, sceneDelegate, renderDelegate);
    }

    *dirtyBits = pxr::HdChangeTracker::Clean;
    hdmLogSyncEnd(id);
}

void
LightFilter::Finalize(pxr::HdRenderParam *renderParam)
{
    if (!mLightFilter) {
        return;
    }
    RenderDelegate& renderDelegate(RenderDelegate::get(renderParam));
    makeLightFilterInert(mLightFilter, renderDelegate);
    renderDelegate.releaseCategory(mLightFilter, RenderDelegate::CategoryType::FilterLink, mLightFilterCategory);
    mLightFilter = nullptr;
    mLightFilterCategory = pxr::TfToken();
}

/*static*/ LightFilter*
get(pxr::HdSceneDelegate* sceneDelegate, const pxr::SdfPath& filterId)
{
    if (not filterId.IsEmpty()) {
        LightFilter* filterPrim = static_cast<LightFilter*>(
            sceneDelegate->GetRenderIndex().GetSprim(lightFilterToken, filterId));
        if (filterPrim) {
            return filterPrim;
        }
        Logger::error(filterId, ": no such LightFilter");
    }
    return nullptr;
}

scene_rdl2::rdl2::LightFilter*
LightFilter::getFilter(pxr::HdSceneDelegate* sceneDelegate,
                       RenderDelegate& renderDelegate,
                       const pxr::SdfPath& filterId)
{
    LightFilter* filterPrim = get(sceneDelegate, filterId);
    if (filterPrim)
        return filterPrim->getOrCreateFilter(sceneDelegate,renderDelegate,filterId);
    return nullptr;
}

}
