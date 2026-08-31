// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// Contains primvar-related functions on HdMoonray_GeometryBase, split
// out into this file as they are the most complex part of the code

#include "geometryBase.h"
#include "instancer.h"
#include "renderDelegate.h"
#include "material.h"
#include "ValueConverter.h"
#include "tokens.h"

#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/base/gf/vec2f.h>


#include <iostream>

using namespace pxr;

namespace {

    constexpr unsigned int INIT_SAMPLES = 3; // initial number of time samples we will request for motion blur.

    // filter primvars by name, to remove excessive "junk" primvars
    // return true if primvar should be ignored
    bool primvarFilter(const TfToken& name)
    {
        if (name.GetText()[0] == '_') return true; // Hydra internal variables
        if (not strncmp(name.GetText(), "usd", 3)) return true; // sceneflow data
        if (not strncmp(name.GetText(), "part:", 4)) return true; // obsolete part api
        static const std::set<TfToken> names {
            TfToken("PreMenvPosingRefPose"), // I think this may be something from Pixar's animation
            TfToken("gprimID"), // Hydra sets this but never requests it
            TfToken("asset_name"), // bookeeping from Pixar
            TfToken("orig_vert_index") // often incorrect primvar from downrez code
        };
        if (names.count(name)) return true;
        return false;
    }

    // utility to set a Vec3f attribute
    inline void setVec3fPrimvar(hdMoonray::MoonrayObject geometry,
                                const std::string& rdlName,
                                const TfToken& hydraName,
                                const VtValue& values)
    {
        if (values.IsHolding<VtVec3fArray>()) {
            geometry.set(rdlName, values.UncheckedGet<VtVec3fArray>());
        } else {
            hdMoonray::Logger::warn("Skipping primvar '", hydraName.GetString(), "': ", rdlName,
                         " requires a Vec3f array, got ", values.GetTypeName());
        }
    }

}

namespace hdMoonray {

void 
HdMoonray_GeometryBase::syncPrimvars(HdSceneDelegate *sceneDelegate,
                            HdMoonray_RenderDelegate& renderDelegate,
                            HdDirtyBits     *dirtyBits)
{
    // This function determines which primvars have changed since the last sync
    // and makes a call to primvarChanged() for each one, providing the new value. 
    // Where a primvar was removed, primvarChanged() is called with an empty value
    //
    // mGeometry should be non-null and have an active UpdateGuard
    const SdfPath& id = rprim.GetId();

    // primId is not a primvar, but we treat it as one because it creates an
    // RDL UserData object
    if (HdChangeTracker::IsPrimIdDirty(*dirtyBits, id)) {
        static TfToken primId("primId");
        // for the float cast, see comment at the start of setUserDataValues
        primvarChanged(sceneDelegate, renderDelegate,
                       primId, VtValue(static_cast<float>(rprim.GetPrimId())),
                       HdInterpolation::HdInterpolationConstant,
                       TfToken());
    }
    
    // We will iterate through every primvar, calling primvarChanged if the primvar
    // is dirty. To detect removed primvars, we maintain the list mAppliedPrimvars of
    // all currently applied primvars, and build a list 'removedPrimvars' by comparing
    // the new and old lists.
    std::set<TfToken> removedPrimvars;
    std::swap(mAppliedPrimvars, removedPrimvars);
    mAppliedPrimvars.clear();
    // initially, removedPrimvars has the old list of applied primvars : as we 
    // see current primvars, we will erase them from the removed list

    // process external computations first, so that they have priority.
    // Values for these are fetched in a single call, so we need two loops
    HdExtComputationPrimvarDescriptorVector dirtyCompPrimvars;
    for (size_t i = 0; i < HdInterpolationCount; ++i) {
        HdInterpolation interp = static_cast<HdInterpolation>(i);
        HdExtComputationPrimvarDescriptorVector compPrimvars =
                sceneDelegate->GetExtComputationPrimvarDescriptors(id, interp);
        for (auto const& pv: compPrimvars) {
            if (primvarFilter(pv.name)) continue;
            mAppliedPrimvars.insert(pv.name);
            removedPrimvars.erase(pv.name);
            if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
                dirtyCompPrimvars.emplace_back(pv);
            }
        }
    }
    // actually compute and update the dirty primvars we discovered
    if (not dirtyCompPrimvars.empty()) {
        HdExtComputationUtils::ValueStore valueStore =
            HdExtComputationUtils::GetComputedPrimvarValues(dirtyCompPrimvars, sceneDelegate);
        for (auto const& compPrimvar : dirtyCompPrimvars) {
            auto const computedValue = valueStore.find(compPrimvar.name);
            primvarChanged(sceneDelegate,
                           renderDelegate,
                           compPrimvar.name,
                           computedValue->second,
                           compPrimvar.interpolation,
                           compPrimvar.role);
        }
    }

    // now process regular primvars, skipping any already seen as computed primvars
    for (size_t i = 0; i < HdInterpolationCount; ++i) {
        HdInterpolation interp = static_cast<HdInterpolation>(i);
        for (HdPrimvarDescriptor const& pv : rprim.GetPrimvarDescriptors(sceneDelegate, interp)) {
            if (not isPrimvarUsed(pv.name) && not primvarFilter(pv.name)) {
                mAppliedPrimvars.insert(pv.name);
                removedPrimvars.erase(pv.name);
                if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name)) {
                    primvarChanged(sceneDelegate,
                                   renderDelegate,
                                   pv.name, 
                                   rprim.GetPrimvar(sceneDelegate, pv.name),
                                   interp, pv.role);
                }
            }
        }
    }

    // finally, process the removed primvars left behind. We will call primvarChanged
    // with an empty VtValue
    for (auto&& name : removedPrimvars) {
        primvarChanged(sceneDelegate, renderDelegate, name, VtValue(),
                       HdInterpolation::HdInterpolationConstant, TfToken());
    }
}

void 
HdMoonray_GeometryBase::primvarChanged(HdSceneDelegate *sceneDelegate,
                              HdMoonray_RenderDelegate& renderDelegate,
                              const TfToken& name, 
                              const VtValue& value,
                              const HdInterpolation& interp,
                              const TfToken& role)
{
    // process a changed primvar. If the primvar was deleted, value will be empty
    // should be overridden by subclasses to handle their own specific primvars.
    
    // point primvars are supported by all geometry, so handle them here.
    if (name == HdTokens->points) {
        setVec3fPrimvarMb(sceneDelegate, renderDelegate, HdTokens->points, value,
                        "vertex_list_0", "vertex_list_1");
        return;
    } else if (name == HdTokens->velocities) {
        setVec3fPrimvarMb(sceneDelegate, renderDelegate, HdTokens->velocities, value,
                         "velocity_list_0", "velocity_list_1");
        return;
    } else if (name == HdTokens->accelerations) {
        // only 1 sample for acceleration
        try {
            if (value.IsEmpty()) {
                mGeometry.setToDefault("accleration_list"); 
            } else {
                setVec3fPrimvar(mGeometry, "accleration_list", name, value);
            }
        } catch (std::exception& e) {
            // geometry isn't guarantred to have "accleration_list"
        }
        return;
    }
    
    // primvars starting with 'moonray:' act to override an RDL object attribute of the same name
    if (not strncmp(name.GetText(), "moonray:", 8)) {
        primvarAttributeOverride(name.GetString().substr(8), value, renderDelegate);
        return;
    }

    // otherwise, primvar generates a corresponding UserData object
    if (supportsUserData()) {
        // HDM-400 : normals conventionally use name "normal" in Moonray, vs "normals" in Hydra. In general, we might assume that the user has to
        // supply the correct primvar name for the renderer, but the Hydra primvar "normals" originates from a non-primvar attribute in the
        // UsdGeom schemas, so it makes sense to translate "normals" to "normal" in this particular case..
        if (name == HdTokens->normals) {
            primvarUserData(renderDelegate, HdMoonrayTokens->normal, value, interp, role);
        } else {
            primvarUserData(renderDelegate, name, value, interp, role);
        }
    }
}

void
HdMoonray_GeometryBase::primvarAttributeOverride(const std::string& name, const VtValue& value, HdMoonray_RenderDelegate&)
{
    // override an RDL attribute value with the given value, or remove an override if
    // value is empty. Sync may not be perfect here, because we would need to force
    // a sync with the appropriate dirty flags to make sure that the unoverridden value
    // is correct, but we don't know what those flags are.
    try { 
        if (value.IsEmpty()) {
            mGeometry.setToDefault(name);
        } else {
            mGeometry.set(name, value);
        }    
    } catch (const std::exception& e) {
        // name wasn't a valid RDL attribute
        Logger::error(rprim.GetId(), ": ", e.what());
    }
}

}
namespace {
// helper functions for setting up UserData objects

void setUserDataInterpolation(hdMoonray::MoonrayObject userData,
                              const HdInterpolation& interp)
{
    switch (interp) {
        case HdInterpolation::HdInterpolationConstant:
            userData.setDataRate(hdMoonray::MoonrayObject::DataRate::CONSTANT);
            break;
        case HdInterpolation::HdInterpolationUniform:
            // mesh: per-face, curve: per-curve, instancer: constant
            userData.setDataRate(hdMoonray::MoonrayObject::DataRate::UNIFORM);
            break;
        case HdInterpolation::HdInterpolationVarying:
            // mesh: per-point (linear), curve: per-segment, instancer: per-instance
            userData.setDataRate(hdMoonray::MoonrayObject::DataRate::VARYING);
            break;
        case HdInterpolation::HdInterpolationVertex:
            // mesh: per-point (subd), curve: per-point, instancer: per-instance
            userData.setDataRate(hdMoonray::MoonrayObject::DataRate::VERTEX);
            break;
        case HdInterpolation::HdInterpolationFaceVarying:
            // mesh: per-vertex, curve: none, instancer: per-instance
            userData.setDataRate(hdMoonray::MoonrayObject::DataRate::FACE_VARYING);
            break;
        default:
            userData.setDataRate(hdMoonray::MoonrayObject::DataRate::AUTO);
    }
}

} // namespace {

namespace hdMoonray {


void
HdMoonray_GeometryBase::primvarUserData(HdMoonray_RenderDelegate& renderDelegate,
                               const TfToken& name,
                               const VtValue& value,
                               const HdInterpolation& interp,
                               const TfToken& role)
{
    // a primvar presumed to represent user data has been added, changed or removed

    if (value.IsEmpty()) {
        // indicates primvar was removed, so we should remove user data
       mUserData.erase(name);
       // cannot delete actual RDL object...
       mUserDataChanged = true;
       return;
    }

    // create UserData object if it doesn't already exist
    auto& userData = mUserData[name];
    if (userData.isNull()) {
        std::string suffix = ".primvars:" + name.GetString();
        userData = renderDelegate.scene().createObject("UserData", getId(), suffix);
        mUserDataChanged = true;
    }

    // store the primvar values into the UserData object
    UpdateGuard guard(renderDelegate, userData);
    setUserDataInterpolation(userData, interp);
    userData.setData(name, value, role);
}


// set a Vec3f attribute from a primvar, with motion blur samples
// if present. Resets attribute to default if value is empty
void 
HdMoonray_GeometryBase::setVec3fPrimvarMb(HdSceneDelegate* sceneDelegate,
                                 HdMoonray_RenderDelegate& renderDelegate,
                                 const TfToken& hydraName,
                                 const VtValue& value,
                                 const std::string& rdlName_0,
                                 const std::string& rdlName_1)
{
    try {
        if (value.IsEmpty()) {
            // primvar was deleted
            mGeometry.setToDefault(rdlName_0);
            mGeometry.setToDefault(rdlName_1);
            return;
        }

        // if time sampling interval is not enabled, just set the single sample
        if (!renderDelegate.scene().isTimeSamplingIntervalEnabled()) {
            setVec3fPrimvar(mGeometry, rdlName_0, hydraName, value);
            mGeometry.setToDefault(rdlName_1);
            return;
        }
        
        // get the motion steps at which we should sample.
        float firstSampleTime, secondSampleTime;
        std::tie(firstSampleTime, secondSampleTime) = renderDelegate.scene().getTimeSamplingInterval();
        if (firstSampleTime == secondSampleTime) {
            setVec3fPrimvar(mGeometry, rdlName_0, hydraName, value);
            mGeometry.setToDefault(rdlName_1);
            return;
        }

        // get samples in interval, and unbox to Vec3f arrays.
        HdTimeSampleArray<VtValue, INIT_SAMPLES> samples;
        sceneDelegate->SamplePrimvar(rprim.GetId(), hydraName, firstSampleTime, secondSampleTime, &samples);
        if (samples.count == 0) {
            setVec3fPrimvar(mGeometry, rdlName_0, hydraName, value);
            mGeometry.setToDefault(rdlName_1);
            return;
        }
        HdTimeSampleArray<VtArray<GfVec3f>, INIT_SAMPLES> pointSamples;
        if (!pointSamples.UnboxFrom(samples)) {
            Logger::warn("Failed to unbox primvar ", hydraName, " as Vec3f array");
            setVec3fPrimvar(mGeometry, rdlName_0, hydraName, value);
            mGeometry.setToDefault(rdlName_1);
            return;
        }

        // resample at sample times and set RDL2 motion samples
        VtArray<GfVec3f> firstSample = pointSamples.Resample(firstSampleTime);
        VtArray<GfVec3f> secondSample = pointSamples.Resample(secondSampleTime);

        if (firstSample.size() != secondSample.size()) {
            setVec3fPrimvar(mGeometry, rdlName_0, hydraName, value);
            mGeometry.setToDefault(rdlName_1);
            return;
        }
        const rdl2::Vec3f* p = reinterpret_cast<const rdl2::Vec3f*>(&firstSample[0]);
        mGeometry.set(rdlName_0, firstSample[0]);
        p = reinterpret_cast<const rdl2::Vec3f*>(&secondSample[0]);
        mGeometry.set(rdlName_1, secondSample[0]);

    } catch (std::exception& e) {
        // may be called in cases that rdlName_0 or _1 don't exist : e.g.
        // if Hydra generates a "points" primvar for procedurals that don't
        // have points
    }
}

}



