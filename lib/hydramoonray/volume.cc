// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "volume.h"
#include "renderDelegate.h"
#include "HdmLog.h"
#include "tokens.h"

#include <pxr/usd/sdf/types.h> // for SdfAssetPath

#include <iostream>

using namespace pxr;

namespace {
    const std::string rdlClassVdb("VdbGeometry");
    const std::string rdlAttrModel("model");
}

namespace hdMoonray {



HdMoonray_Volume::HdMoonray_Volume(SdfPath const& id) :
    HdVolume(id), HdMoonray_GeometryBase(this) 
{}

void
HdMoonray_Volume::Finalize(HdRenderParam* renderParam)
{
    // causes geometry to be hidden
    resetGeometryObject(HdMoonray_RenderDelegate::get(renderParam));
}

HdDirtyBits
HdMoonray_Volume::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllDirty;
}

void 
HdMoonray_Volume::syncAttributes(HdSceneDelegate* sceneDelegate, 
                            HdMoonray_RenderDelegate& renderDelegate,
                            HdDirtyBits* dirtyBits,
                            const TfToken& reprToken)
{
    auto fields(sceneDelegate->GetVolumeFieldDescriptors(GetId()));
    const std::string* filePath = nullptr;
    for (auto& desc: fields) {
        const HdMoonray_OpenVdbAsset* const field =
            dynamic_cast<HdMoonray_OpenVdbAsset*>(sceneDelegate->GetRenderIndex().GetBprim(
                                            desc.fieldPrimType, desc.fieldId));
        if (!field) continue;
           
        if (desc.fieldName == HdMoonrayTokens->density || 
            desc.fieldName == HdMoonrayTokens->velocity) {
                
            const std::string attributeName = desc.fieldName.GetString() + "_grid";
            std::string grid(field->name().GetString());
            if (field->index() > 0) {
                grid += "[" + std::to_string(field->index()) + "]";
            }
            mGeometry.set(attributeName, grid);
                    
            const std::string& fp = field->filePath();
            if (not fp.empty()) {
                if (filePath && *filePath != fp) {
                    Logger::error(GetId(), " all fields must use same vdb file");
                    break;
                }
                filePath = &fp;
            }
            if (filePath) {
                mGeometry.set(rdlAttrModel, *filePath);
            }
        }
    }

    // base class handles transform and double-sided
    HdMoonray_GeometryBase::syncAttributes(sceneDelegate, renderDelegate, dirtyBits, reprToken);
   
}

void
HdMoonray_Volume::Sync(HdSceneDelegate* sceneDelegate,
             HdRenderParam*   renderParam,
             HdDirtyBits*     dirtyBits,
             const TfToken&   reprToken)
{
    hdmLogSyncStart("Volume", GetId(), dirtyBits);   
    HdMoonray_RenderDelegate& renderDelegate(HdMoonray_RenderDelegate::get(renderParam));
    
    _UpdateVisibility(sceneDelegate, dirtyBits);
    _UpdateInstancer(sceneDelegate, dirtyBits);
    
    syncAll(rdlClassVdb, sceneDelegate, renderDelegate, dirtyBits, reprToken);
    
    hdmLogSyncEnd(GetId());
}

void
HdMoonray_OpenVdbAsset::Sync(HdSceneDelegate *sceneDelegate,
                   HdRenderParam   *renderParam,
                   HdDirtyBits     *dirtyBits)
{
    const SdfPath& id = GetId();
    hdmLogSyncStart("OpenVdbAsset", id, dirtyBits);

    if (*dirtyBits & DirtyParams) {
        VtValue v;
        v = sceneDelegate->Get(id, HdFieldTokens->filePath);
        mFilePath = v.Get<SdfAssetPath>().GetResolvedPath();
        v = sceneDelegate->Get(id, HdMoonrayTokens->fieldName);
        mName = v.Get<TfToken>();
        v = sceneDelegate->GetLightParamValue(id, HdMoonrayTokens->fieldIndex);
        mIndex = (not v.IsEmpty()) ? v.Get<int>() : -1;
    }
    *dirtyBits = Clean;
    hdmLogSyncEnd(id);
}

}
