// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "instancer.h"
#include "renderDelegate.h"
#include "ValueConverter.h"
#include "HdmLog.h"
#include "tokens.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/quath.h>


using namespace pxr;

#include <iostream>
namespace {

VtValue getElement(const VtValue& array, size_t index)
{
    if (array.IsHolding<VtFloatArray>()) {
        const VtFloatArray& v = array.UncheckedGet<VtFloatArray>();
        if (index < v.size()) return VtValue(v[index]);
    } else if (array.IsHolding<VtVec2fArray>()) {
        const VtVec2fArray& v = array.UncheckedGet<VtVec2fArray>();
        if (index < v.size()) return VtValue(v[index]);
    } else if (array.IsHolding<VtVec3fArray>()) {
        const VtVec3fArray& v = array.UncheckedGet<VtVec3fArray>();
        if (index < v.size()) return VtValue(v[index]);
    } else if (array.IsHolding<VtIntArray>()) {
        const VtIntArray& v = array.UncheckedGet<VtIntArray>();
        if (index < v.size()) return VtValue(v[index]);
    }
    return VtValue();
}

// instance transform token changes in 0.23.11 
#if PXR_VERSION >= 2311
const TfToken INSTANCE_TRANSFORMS = HdInstancerTokens->instanceTransforms;
const TfToken INSTANCE_ROTATIONS = HdInstancerTokens->instanceRotations;
const TfToken INSTANCE_SCALES = HdInstancerTokens->instanceScales;
const TfToken INSTANCE_TRANSLATIONS = HdInstancerTokens->instanceTranslations;
#else
const TfToken INSTANCE_TRANSFORMS = HdInstancerTokens->instanceTransform;
const TfToken INSTANCE_ROTATIONS = HdInstancerTokens->rotate;
const TfToken INSTANCE_SCALES = HdInstancerTokens->scale;
const TfToken INSTANCE_TRANSLATIONS = HdInstancerTokens->translate;
#endif

}

namespace hdMoonray {

// instancer method values
constexpr int XFORM_ATTRIBUTES = 0;
constexpr int POINT_FILE = 1;
constexpr int XFORM_LIST = 2;

HdMoonray_Instancer::HdMoonray_Instancer(HdSceneDelegate* delegate, const SdfPath& id)
        : HdInstancer(delegate, id)
{ 
}

HdMoonray_Instancer::~HdMoonray_Instancer()
{
}

// Hydra sends several apparent junk primvars that are not in the usd data, ignore them
static bool
ignorePrimvar(TfToken name)
{
    if (name.GetText()[0] == '_') return true; // Hydra internal variables
    if (not strncmp(name.GetText(), "usd", 3)) return true; // sceneflow data
    static const std::set<TfToken> names {
        TfToken("asset_name"), // bookeeping from Pixar
        TfToken("mesh_path"), // from sprinkles, huge array of repeated strings
        TfToken("hit_prim_path") // new version of the sprinkles huge array
    };
    if (names.count(name)) return true;
    return false;
}

void
HdMoonray_Instancer::Sync(HdSceneDelegate* sceneDelegate,
                          HdRenderParam* renderParam,
                          HdDirtyBits* dirtyBits)
{
    const SdfPath& id = GetId();
    hdmLogSyncStart("Instancer", id, dirtyBits);

    _UpdateInstancer(sceneDelegate, dirtyBits);

    if (HdChangeTracker::IsInstancerDirty(*dirtyBits, id)) {
        // not clear what, if anything, is covered by this
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        mXform = sceneDelegate->GetInstancerTransform(id);
    }

    if (HdChangeTracker::IsInstanceIndexDirty(*dirtyBits, id)) {
        // this does not do anything, the indexes are looked up per-prototype below
    }

    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id)) {

        TfTokenVector primvarNames;

        const HdPrimvarDescriptorVector& primvars =
            sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationInstance);

        for (const HdPrimvarDescriptor& pv: primvars) {
            
            if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {

                if (ignorePrimvar(pv.name)) continue;

                PrimvarInfo& info = mPrimvars[pv.name];
                info.value = sceneDelegate->Get(id, pv.name);
                info.role = pv.role;
            }
        }
    }

    *dirtyBits = 0;
}


// The SceneDelegate api only allows access to the indices by using the prototypeId, which
// we don't have until this is called. Even the number of indices is unavailable as far
// as I can tell. So this function must update the data for that subset of indices.
// Curently a different InstanceGeometry is created for each prototype. It may be possible
// to use a single one if there is a way to incrementally update the references and refIndices.
void
HdMoonray_Instancer::makeInstanceGeometry(const SdfPath& prototypeId,
                                          const std::string& protoSuffix,
                                          MoonrayObject prototype,
                                          HdMoonray_GeometryBase* hdGeometry,
                                          size_t level, size_t childCount)
{
    HdSceneDelegate* sceneDelegate = GetDelegate();
    HdMoonray_RenderDelegate& renderDelegate = *reinterpret_cast<HdMoonray_RenderDelegate*>(sceneDelegate->GetRenderIndex().GetRenderDelegate());

    _SyncInstancerAndParents(sceneDelegate->GetRenderIndex(), GetId());

    VtIntArray indices = sceneDelegate->GetInstanceIndices(GetId(), prototypeId);
    const size_t count = indices.size();

    MoonrayObject instancer;
    MoonrayObject instanceId;
    {   
        std::lock_guard<std::mutex> lock(mMapMutex);
        MoonrayObject& mapEntry = mInstancers[prototype];
        if (mapEntry.isNull()) {
            mapEntry = renderDelegate.scene().createObject("RdlInstancerGeometry", prototypeId, "/Instancer"+protoSuffix);
            if (mapEntry.isNull()) return; // it already printed an error, give up
            MoonrayAssignment assignment;
            renderDelegate.scene().assign(mapEntry, assignment);
            instanceId = renderDelegate.scene().createObject("UserData", prototypeId, "/instanceId"+protoSuffix);
        } else {
            instanceId = mapEntry.getInstanceIdData(); 
        }
        instancer = mapEntry;
    }

    {
        UpdateGuard guard(renderDelegate, instanceId);
        std::string name("instanceId");
        if (level) name.push_back('A'+level-1);
        VtFloatArray out(count);
        for (size_t i = 0; i < count; ++i)
            out[i] = i * childCount;
        instanceId.setData(name, out, TfToken());
    }


    // add crytomatte id if enabled
    const std::string name("prim_id");
    std::string suffix = "/Instancer.primvars:" + name + protoSuffix;
    MoonrayObject cryptoId = renderDelegate.scene().createObject("UserData",prototypeId, suffix);
    UpdateGuard guard(cryptoId);
    float hash = MurmurHash3_to_float(instancer.sceneObject()->getName().c_str());
    cryptoId.setData(name, hash, TfToken());

    // note: how to get instance paths via SceneIndex
    // PrimAccess primAccess(id, HdInstancerTopologySchemaTokens->instancerTopology, sceneDelegate, renderDelegate);
    // VtValue locationsVal = primAccess.Get(HdInstancerTopologySchemaTokens->instanceLocations);
    // if (locationsVal.IsHolding<VtArray<SdfPath>>()) {
       // const VtArray<SdfPath>& locations = locationsVal.UncheckedGet<VtArray<SdfPath>>();

    // MOONSHINE-1533: Moonray uses the number of transforms to count instances, while Hydra Prman
    // and Embree use the size of the protoIndices array. For bad data where these are unequal,
    // resize the primvars to match the number of instances, so all the renderers produce the same
    // number of instances.
    MoonrayObjectVector primitiveAttributes;
    primitiveAttributes.append(instanceId);
    primitiveAttributes.append(cryptoId);

    for (auto p : mPrimvars) {
        const TfToken& name = p.first;
        if (name == INSTANCE_TRANSFORMS ||
            name == INSTANCE_SCALES ||
            name == INSTANCE_ROTATIONS ||
            name == INSTANCE_TRANSLATIONS)
            continue; // skip the ones used directly

        // TODO: HDM-130: Moonray InstanceGeometry primvars override primvars on the prototype, which
        // is opposite how USD works. Don't create the primvar if it will override incorrectly.
        if (hdGeometry->isPrimvarUsed(name)) continue;
        // Fixme: this also needs to be done with each intermediate instancer.

        const VtValue& value = p.second.value;
        const TfToken& role = p.second.role;
        std::string suffix = "/Instancer.primvars:" + name.GetString() + protoSuffix;
        MoonrayObject primvar = renderDelegate.scene().createObject("UserData", prototypeId, suffix);

        UpdateGuard guard(primvar);
        // there are lots of types, just get the ones encountered so far:
        if (value.IsHolding<VtFloatArray>()) {
            const VtFloatArray& v = value.UncheckedGet<VtFloatArray>();
            if (v.empty()) continue; // don't crash on error
            VtFloatArray out(count);
            for (size_t i = 0; i < count; ++i) out[i] = v[indices[i] % v.size()];
            primvar.setData(name.GetString(), out, role);
        } else if (value.IsHolding<VtVec2fArray>()) {
            const VtVec2fArray& v = value.UncheckedGet<VtVec2fArray>();
            if (v.empty()) continue; // don't crash on error
            VtVec2fArray out(count);
            for (size_t i = 0; i < count; ++i) out[i] = reinterpret_cast<const GfVec2f&>(v[indices[i] % v.size()]);
            primvar.setData(name.GetString(), out, role);
        } else if (value.IsHolding<VtVec3fArray>()) {
            const VtVec3fArray& v = value.UncheckedGet<VtVec3fArray>();
            if (v.empty()) continue; // don't crash on error
            VtVec3fArray out(count);
            for (size_t i = 0; i < count; ++i) out[i] = reinterpret_cast<const GfVec3f&>(v[indices[i] % v.size()]);
            primvar.setData(name.GetString(), out, role);
        } else if (value.IsHolding<GfVec3f>()) {
            const GfVec3f& v = value.UncheckedGet<GfVec3f>();
            primvar.setData(name.GetString(), v, role);
        } else if (value.IsHolding<VtIntArray>()) {
            const VtIntArray& v = value.UncheckedGet<VtIntArray>();
            if (v.empty()) continue; // don't crash on error
            VtIntArray out(count);
            for (size_t i = 0; i < count; ++i) out[i] = v[indices[i] % v.size()];
            primvar.setData(name.GetString(), out, role);
        } else {
            Logger::warn(primvar.objectName(),": ",value.GetTypeName()," not translated");
        }
        primitiveAttributes.append(primvar);
    }

    {   
        UpdateGuard guard(instancer);
        instancer.set("primitive_attributes", primitiveAttributes);
        instancer.set("node_xform", mXform);
        instancer.set("references", MoonrayObjectVector(prototype));
        instancer.set("use_reference_xforms", true);

        auto i = mPrimvars.find(INSTANCE_TRANSFORMS);
        if (i != mPrimvars.end()) {
            instancer.set("method", XFORM_LIST);
            const VtValue& value = i->second.value;
            const VtMatrix4dArray& v = value.Get<VtMatrix4dArray>();
            if (not v.empty()) {
                VtMatrix4dArray mv(count);
                for (size_t i = 0; i < count; ++i)
                    mv[i] = v[indices[i] % v.size()];
                instancer.set("xform_list", mv);
            }

        } else {
            instancer.set("method", XFORM_ATTRIBUTES);

            i = mPrimvars.find(INSTANCE_SCALES);
            if (i != mPrimvars.end()) {
                const VtValue& value = i->second.value;
                const VtVec3fArray& v = value.Get<VtVec3fArray>();
                if (not v.empty()) {
                    VtVec3fArray mv(count);
                    for (size_t i = 0; i < count; ++i)
                        mv[i] = v[indices[i] % v.size()];
                    instancer.set("scales", mv);
                }
            }

            i = mPrimvars.find(INSTANCE_ROTATIONS);
            if (i != mPrimvars.end()) {
                const VtValue& value = i->second.value;

                // in 0.21.11 the type of the rotations attr switched from VtVec4fArray to VtQuathArray
                if (value.IsHolding<VtQuathArray>()) {
                    const VtQuathArray& quats = value.Get<VtQuathArray>();
                    if (not quats.empty()) {
                        VtQuathArray mv(count);
                        for (size_t i = 0; i < count; ++i) {
                            mv[i] = quats[indices[i] % quats.size()];
                        }
                        instancer.set("orientations", mv);
                    }
                } else {
                    const VtVec4fArray& v = value.Get<VtVec4fArray>();
                    if (not v.empty()) {
                        VtVec4fArray mv(count);
                        for (size_t i = 0; i < count; ++i) {
                            mv[i] = v[indices[i] % v.size()];
                        }
                        instancer.set("orientations", mv);
                    }
                }
            }
            i = mPrimvars.find(INSTANCE_TRANSLATIONS);
            if (i != mPrimvars.end()) {
                const VtValue& value = i->second.value;
                const VtVec3fArray& v = value.Get<VtVec3fArray>();
                if (not v.empty()) {
                    VtVec3fArray mv(count);
                    for (size_t i = 0; i < count; ++i)
                        mv[i] = v[indices[i] % v.size()];
                    instancer.set("positions", mv);
                }
            }
        }
    }

    // This instancer may itself be a prototype for another instancer!
    if (not GetParentId().IsEmpty()) {
        renderDelegate.scene().simplifyPath(GetId(), sceneDelegate);
        const SdfPath& parentId = GetParentId();
        HdMoonray_Instancer* parent = (HdMoonray_Instancer*)sceneDelegate->GetRenderIndex().GetInstancer(parentId);
        // we have to add a suffix to the RDL names to make them unique
        size_t protoHash = std::hash<std::string>()(renderDelegate.scene().getSimplePath(prototypeId).GetString());
        const std::string suffix = protoSuffix + "_" + std::to_string(protoHash);
        parent->makeInstanceGeometry(GetId(), suffix, instancer, hdGeometry, level+1, count * childCount);
    }

}
void
HdMoonray_Instancer::makeInstanceLights(const SdfPath& prototypeId,
                                        MoonrayObject prototype,
                                        size_t level, size_t childCount)
{
    const SdfPath& id = GetId();

    HdSceneDelegate* sceneDelegate = GetDelegate();
    HdMoonray_RenderDelegate& renderDelegate = *reinterpret_cast<HdMoonray_RenderDelegate*>(sceneDelegate->GetRenderIndex().GetRenderDelegate());

    _SyncInstancerAndParents(sceneDelegate->GetRenderIndex(), id);

    VtIntArray indices = sceneDelegate->GetInstanceIndices(id, prototypeId);
    const size_t count = indices.size();

    // get the instance transforms
    std::vector<GfMatrix4d> xforms(count, mXform);

    auto it = mPrimvars.find(INSTANCE_TRANSFORMS);
    if (it != mPrimvars.end()) {
        const VtMatrix4dArray& v = it->second.value.Get<VtMatrix4dArray>();
        if (!v.empty()) {
            for (size_t i = 0; i < count; ++i)
                xforms[i] *= v[indices[i] % v.size()];
        }
    } else {
        it = mPrimvars.find(INSTANCE_SCALES);
        
        if (it != mPrimvars.end()) {
            const VtVec3fArray& v = it->second.value.Get<VtVec3fArray>();
            if (not v.empty()) {
                for (size_t i = 0; i < count; ++i)
                    xforms[i] *= GfMatrix4d().SetScale(v[indices[i] % v.size()]);
            }
        }
        
        it = mPrimvars.find(INSTANCE_ROTATIONS);
        if (it != mPrimvars.end()) {
            const VtQuathArray& v = it->second.value.Get<VtQuathArray>();
            if (not v.empty()) {
                for (size_t i = 0; i < count; ++i)
                    xforms[i] *= GfMatrix4d().SetRotate(v[indices[i] % v.size()]);
            }
        }
        
        it = mPrimvars.find(INSTANCE_TRANSLATIONS);
        if (it != mPrimvars.end()) {
            const VtValue& value = it->second.value;
            const VtVec3fArray& v = value.Get<VtVec3fArray>();
            if (not v.empty()) {
                for (size_t i = 0; i < count; ++i)
                    xforms[i] *= GfMatrix4d().SetTranslate(v[indices[i] % v.size()]);
            }
        }
    }

    // get the categories of the prototype, so we can register the instances too
    TfToken lightLinkCategory = sceneDelegate->GetLightParamValue(prototypeId, HdTokens->lightLink).Get<TfToken>();
    TfToken shadowLinkCategory = sceneDelegate->GetLightParamValue(prototypeId, HdTokens->shadowLink).Get<TfToken>();
   
    // (re)build the instance lights
    std::lock_guard<std::mutex> lock(mMapMutex);
    LightInstances& instances = mLightInstances[prototype];
    // turn off any excess instances
    if (instances.size() > count) {
        for (size_t i = count; i < instances.size(); i++) {
            if (instances[i].isValid()) {
                instances[i].beginUpdate();
                instances[i].set("on", false);
                instances[i].endUpdate();
            }
        }
    }
    instances.resize(count, MoonrayObject());
    
    for (size_t i = 0; i < count; i++) {
        if (!instances[i].isValid()) {
            std::string suffix = "_i" + std::to_string(i);
            std::string className = prototype.className();
            MoonrayObject object = renderDelegate.scene().createObject(className, prototypeId, suffix);
            if (object.isNull()) break; // it already printed an error, give up
            instances[i] = object;
        }
        instances[i].beginUpdate();
        instances[i].copyFrom(prototype);
        instances[i].set("node_xform", xforms[i]);
        applyPrimvarOverrides(instances[i], indices[i]);
        instances[i].endUpdate();
        renderDelegate.scene().setCategory(instances[i], LightCategory, lightLinkCategory);
        renderDelegate.scene().setCategory(instances[i], ShadowCategory, shadowLinkCategory);
    }

    // TODO This instancer may be a prototype for another instancer
}

void
HdMoonray_Instancer::applyPrimvarOverrides(MoonrayObject obj,
                                           int index)
{
    for (auto pv : mPrimvars) {
        if (strncmp(pv.first.GetText(), "moonray:", 8)==0) {
            std::string attrName = pv.first.GetString().substr(8);
            if (obj.hasAttribute(attrName)) {
                VtValue val = getElement(pv.second.value, index);
                if (!val.IsEmpty())
                    obj.set(attrName, val);
            }
        }
    }
}

}
