// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "material.h"
#include "renderDelegate.h"
#include "ValueConverter.h"
#include "HdmLog.h"
#include "light.h"
#include "shader_utils.h"
#include "tokens.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/imaging/hd/rprim.h>

#include <iostream>

using namespace pxr;
namespace hdMoonray {

HdDirtyBits
HdMoonray_Material::GetInitialDirtyBitsMask() const
{
    return AllDirty;
}

void
HdMoonray_Material::Sync(HdSceneDelegate *sceneDelegate,
                         HdRenderParam   *renderParam,
                         HdDirtyBits     *dirtyBits)
{
    auto& renderDelegate(HdMoonray_RenderDelegate::get(renderParam));

    const SdfPath& id = GetId();
    hdmLogSyncStart("Material", id, dirtyBits);

    if (*dirtyBits & AllDirty) {
        mSurfaceDirty = mDisplacementDirty = mVolumeShaderDirty = true;
        // update any material that has been created
        if (mMoonrayMaterial.isValid()) getSurface(renderDelegate, sceneDelegate);
        if (mMoonrayDisplacement.isValid()) getDisplacement(renderDelegate, sceneDelegate);
        if (mMoonrayVolumeShader.isValid()) getVolumeShader(renderDelegate, sceneDelegate);
    }
    *dirtyBits &= ~AllDirty;
    hdmLogSyncEnd(id);
}

MoonrayObject
HdMoonray_Material::getSurface(HdMoonray_RenderDelegate& renderDelegate,
                               HdSceneDelegate *sceneDelegate,
                               bool canReturnNull)
{
    if (mSurfaceDirty || renderDelegate.options().getDecodeNormalsChanged()) {
        std::lock_guard<std::mutex> lock(mCreateMutex);
        mMoonrayMaterial = getTerminal(GetId(), HdMaterialTerminalTokens->surface, renderDelegate, sceneDelegate);
        if (mMoonrayMaterial.isNull() && !canReturnNull) {
            mMoonrayMaterial = renderDelegate.scene().errorMaterial();
        }
        mSurfaceDirty = false;
    }
    return mMoonrayMaterial;
}

MoonrayObject
HdMoonray_Material::getDisplacement(HdMoonray_RenderDelegate& renderDelegate,
                                    HdSceneDelegate *sceneDelegate)
{
    if (mDisplacementDirty) {
        std::lock_guard<std::mutex> lock(mCreateMutex);
        if (mDisplacementDirty) {
            mMoonrayDisplacement = getTerminal(GetId(), HdMaterialTerminalTokens->displacement, renderDelegate, sceneDelegate);
            mDisplacementDirty = false;
        }
    }
    return mMoonrayDisplacement;
}


MoonrayObject
HdMoonray_Material::getVolumeShader(HdMoonray_RenderDelegate& renderDelegate,
                          HdSceneDelegate *sceneDelegate)
{
    if (mVolumeShaderDirty) {
        std::lock_guard<std::mutex> lock(mCreateMutex);
        if (mVolumeShaderDirty) {
            mMoonrayVolumeShader = getTerminal(GetId(), HdMaterialTerminalTokens->volume, renderDelegate, sceneDelegate);
            mVolumeShaderDirty = false;
        }
    }
    return mMoonrayVolumeShader;
}

/* static*/ void
HdMoonray_Material::assign(MoonrayAssignment& assignment,
                           const SdfPath& materialId,
                           HdMoonray_RenderDelegate& renderDelegate,
                           HdSceneDelegate* sceneDelegate,
                           bool forVolume)
{
    if (materialId.IsEmpty()) {
        assignment.material = MoonrayObject();
        assignment.displacement = MoonrayObject();
        assignment.volumeShader = MoonrayObject();
    } else {
        HdMoonray_Material* mtlPrim = static_cast<HdMoonray_Material*>(
            sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->material, materialId));
        if (!mtlPrim) {
            Logger::error("Cannot find material ", materialId);
            assignment.material = forVolume ? MoonrayObject() : renderDelegate.scene().errorMaterial();
            assignment.displacement = MoonrayObject();
            assignment.volumeShader = forVolume ? renderDelegate.scene().defaultVolumeShader() : MoonrayObject();
            return;
        }

        assignment.material = mtlPrim->getSurface(renderDelegate, sceneDelegate, forVolume);
        assignment.displacement = mtlPrim->getDisplacement(renderDelegate, sceneDelegate);
        assignment.volumeShader = mtlPrim->getVolumeShader(renderDelegate, sceneDelegate);
    }
    // fill in defaults for any missing terminals
    if (assignment.material.isNull() && !forVolume) assignment.material = renderDelegate.scene().defaultMaterial();
    if (assignment.volumeShader.isNull() && forVolume) assignment.volumeShader = renderDelegate.scene().defaultVolumeShader();
}

}

