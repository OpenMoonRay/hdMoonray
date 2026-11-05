// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// Primvar-related functions are in Primvars.cc

#include "geometryBase.h"
#include "instancer.h"
#include "renderDelegate.h"
#include "material.h"
#include "ValueConverter.h"
#include "tokens.h"

#include <pxr/imaging/hd/extComputationUtils.h>
#include <pxr/base/gf/vec2f.h>

using namespace pxr;

namespace hdMoonray {

void
HdMoonray_GeometryBase::resetGeometryObject(HdMoonray_RenderDelegate& renderDelegate)
{
    if (mGeometry.isValid()) {
        // RDL objects cannot be deleted, so hide the object
        // Also used to hide objects to fix Pixar bug 
        //     https://github.com/PixarAnimationStudios/USD/issues/801
        UpdateGuard guard(renderDelegate, mGeometry);
        forceInvisible();
        mGeometry = MoonrayObject();
    }
}

bool
HdMoonray_GeometryBase::createGeometry(HdSceneDelegate *sceneDelegate,
                                       HdMoonray_RenderDelegate& renderDelegate,
                                       const std::string& className)
{
    // create the geometry. Returns true if sync should continue
   
    // if this class is pruned, don't create the object, and
    // hide it if it already exists
    if (renderDelegate.options().getPruneProcedural(className) ||
        (isVolume() && renderDelegate.options().getPruneVolume())) {
        resetGeometryObject(renderDelegate);
        return false;
    } 
    
    if (mGeometry.isValid()) {
        return true;
    }

    renderDelegate.scene().simplifyPath(rprim.GetId(), sceneDelegate);

    mGeometry = renderDelegate.scene().createObject(className, rprim.GetId());
    if (mGeometry.isValid()) {
        // RDL objects cannot be deleted, so we may get back an existing object that
        // is no longer in use. We want to reset all attributes so that sync is correct.
        UpdateGuard guard(renderDelegate, mGeometry);
        mGeometry.resetToDefault();
    }

    return mGeometry.isValid();
}

void
HdMoonray_GeometryBase::syncAll(const std::string& className,
                                HdSceneDelegate *sceneDelegate,
                                HdMoonray_RenderDelegate&  renderDelegate,
                                HdDirtyBits *dirtyBits,
                                TfToken const &reprToken)
{
    // create the RDL object
    if (createGeometry(sceneDelegate, renderDelegate, className)) {

        // mGeometry must be non-null
        UpdateGuard guard(renderDelegate, mGeometry);

        // primvars may override other means of setting attributes
        // we handle this by syncing primvars first...
        syncPrimvars(sceneDelegate, renderDelegate, dirtyBits);

        // and then checking in the following function if a primvar
        // is set (function isPrimvarUsed)
        syncAttributes(sceneDelegate, renderDelegate, dirtyBits, reprToken);

        // perform material and light assignment, and instancing
        assign(sceneDelegate, renderDelegate, dirtyBits);

        // populate the "primitive_attributes" list with user data
        syncPrimitiveAttributes();
    }

    // clear dirty bits to indicate everything is synced
    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void
HdMoonray_GeometryBase::syncAttributes(HdSceneDelegate* sceneDelegate,
                                        HdMoonray_RenderDelegate& renderDelegate,
                                        HdDirtyBits* dirtyBits,
                                        const TfToken& reprToken)
{
    // sync plain attributes. Should be called after syncPrimvars, since
    // it uses the list mAppliedPrimvars to avoid overwriting any primvar overrides
    // mGeometry must be non-null and have an active UpdateGuard.
    // This function should be overridden to handle additional attributes in a
    // subclass.

    const SdfPath& id = rprim.GetId();

    // all geometry has a transform, mapped to RDL node_xform. This
    // cannot be overridden by a primvar
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        HdTimeSampleArray<GfMatrix4d, 4> sampledXforms;
        std::pair<float, float> sampleInterval = renderDelegate.scene().getTimeSamplingInterval();
        sceneDelegate->SampleTransform(id, sampleInterval.first, sampleInterval.second, &sampledXforms);
        // if there's only one sample, it should match the cached value
        if (sampledXforms.count <= 1) {
            mGeometry.set("node_xform",sampledXforms.values[0]);
        } else {
            // first and last samples will be sample interval boundaries
            mGeometry.set("node_xform",sampledXforms.values[0], sampledXforms.values[sampledXforms.count-1]);
       }
       mMirror = sampledXforms.values[0].GetDeterminant() < 0; // workaround for MOONRAY-3512
    }

    // side_type can be overridden by a primvar, so we have to check this hasn't happened
    if (HdChangeTracker::IsDoubleSidedDirty(*dirtyBits, id)) {
        if (!isPrimvarUsed(HdMoonrayTokens->moonray_side_type)) {
            int side_type = 1; // force single-sided
            if (renderDelegate.options().isDoubleSided() || sceneDelegate->GetDoubleSided(id)) {
                side_type = 0; // force two-sided
            }
            mGeometry.set("side_type", side_type);
        }
    }
}
void
HdMoonray_GeometryBase::addUserData(pxr::TfToken key,
                                    const MoonrayObject& userData)
{
    mUserData[key] = userData;
    mUserDataChanged = true;
}

void
HdMoonray_GeometryBase::syncPrimitiveAttributes()
{
    // update the object's user data list, if it has changed and the class has it.
    // this is done near the end, so that other sync code can add user data via the
    // function above (e.g.the code to generate cryptomatte ids, which depends on the topology)
    if (mUserDataChanged && supportsUserData()) {
        MoonrayObjectVector userDatas;
        for (auto& i : mUserData) {
            userDatas.append(i.second);
        }
        mGeometry.set("primitive_attributes", userDatas);
        mUserDataChanged = false;
    }
}

void
HdMoonray_GeometryBase::assign(HdSceneDelegate* sceneDelegate,
                      HdMoonray_RenderDelegate& renderDelegate,
                      HdDirtyBits* dirtyBits)
{
    // perform material and light assignment and instance creation

    const SdfPath& id = rprim.GetId();
    const SdfPath& instancerId = rprim.GetInstancerId();
    
    if (*dirtyBits & HdChangeTracker::DirtyMaterialId) {
        rprim.SetMaterialId(sceneDelegate->GetMaterialId(id));
    }

    if (*dirtyBits & (HdChangeTracker::DirtyCategories | 
                      HdChangeTracker::DirtyMaterialId |
                      HdChangeTracker::DirtyVisibility)) {
                
        if (rprim.IsVisible()) {

            // make sure RDL visible_xyz flags are set correctly
            restoreVisibility(sceneDelegate);

            // categories associated with the geometry define which light
            // sets are assigned
            VtArray<TfToken> categories;
            if (instancerId.IsEmpty()) {
                categories = sceneDelegate->GetCategories(id);
            } else {
                // if this geometry is instanced, we have to use GetInstanceCategories()
                std::vector<VtArray<TfToken>> instCategories =
                    sceneDelegate->GetInstanceCategories(instancerId);
                // instCategories is a vector of per-instance Category sets. 
                // Currently we can only apply a single set to the entire set of instances, 
                // so we use the first
                if (instCategories.size() == 0) {
                    // point instancer
                    categories = sceneDelegate->GetCategories(instancerId);
                } else {
                    categories = instCategories[0];
                }
            }

            // renderDelegate can translate the categories to RDL objects and
            // fill in a Assignment object with the proper light set
            MoonrayAssignment assignment;
            renderDelegate.scene().updateAssignmentFromCategories(assignment, categories);
        
            // fill in the material assignment
            HdMoonray_Material::assign(assignment, 
                                       rprim.GetMaterialId(), 
                                       renderDelegate, 
                                       sceneDelegate, 
                                       isVolume());

            // add the assignment to the Layer table
            renderDelegate.scene().assign(mGeometry, assignment);

            // repeat for all parts in the part list
            for (size_t i = 0; i < partList.size(); ++i) {
                // when instanced. light linking for parts can only be inherited from the instancer
                if (instancerId.IsEmpty()) {
                    // not instanced : there could be light linking on the GeomSubset
                    renderDelegate.scene().updateAssignmentFromCategories(assignment, sceneDelegate->GetCategories(partPaths[i]));
                }
                HdMoonray_Material::assign(assignment, 
                                           partMaterials[i], 
                                           renderDelegate, 
                                           sceneDelegate, 
                                           isVolume());
                renderDelegate.scene().assign(mGeometry, partList[i], assignment);
            }

        } else {
            forceInvisible();
            renderDelegate.scene().addUnassigned(mGeometry);
        }
    }

    // make instances
    if (HdChangeTracker::IsInstancerDirty(*dirtyBits, id) ||
        HdChangeTracker::IsInstanceIndexDirty(*dirtyBits, id) ||
        HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        HdMoonray_Instancer* instancer = static_cast<HdMoonray_Instancer*>(
            sceneDelegate->GetRenderIndex().GetInstancer(instancerId));
        if (instancer) {
            instancer->makeInstanceGeometry(id, "", mGeometry, this, 0);
        }
    }
   
}
namespace {

    const std::string visibleAttrs[] = {
        "visible_in_camera",
        "visible_shadow",
        "visible_diffuse_reflection",
        "visible_diffuse_transmission",
        "visible_glossy_reflection",
        "visible_glossy_transmission",
        "visible_mirror_reflection",
        "visible_mirror_transmission",
        "visible_volume"
};

}

void
HdMoonray_GeometryBase::forceInvisible()
{
    // make sure the geometry object is invisible in all cases
    for (const auto& i : visibleAttrs) {
        mGeometry.set(i, false);
    }
    // flag that we did this, to avoid restoring unnecessarily
    mForcedInvisible = true;
}

void
HdMoonray_GeometryBase::restoreVisibility(HdSceneDelegate* sceneDelegate)
{
    // check that we are in a forced invisible state
    if (!mForcedInvisible) return;
    // we have to reread every primvar that affects visibility
    for (const auto& i : visibleAttrs) {
        VtValue val = rprim.GetPrimvar(sceneDelegate, TfToken("moonray:" + i));
        bool vis = true;
        // allow the primvars to be int or bool
        if (val.IsHolding<bool>()) vis = val.UncheckedGet<bool>();
        else if (val.IsHolding<int>()) vis = (val.UncheckedGet<int>() != 0);
        mGeometry.set(i, vis);
    }
    mForcedInvisible = false;
}

}

