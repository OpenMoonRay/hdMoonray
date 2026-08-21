// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MoonrayObject.h"

#include <pxr/imaging/hd/material.h>
#include <pxr/imaging/hd/rprim.h>

namespace hdMoonray {

class HdMoonray_RenderDelegate;

class HdMoonray_Material: public pxr::HdMaterial
{
public:
    explicit HdMoonray_Material(pxr::SdfPath const& id): pxr::HdMaterial(id) {}

    /// Dirty bits to pass to first call to Sync()
    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Update the data identified by dirtyBits. Must not query other data.
    void Sync(pxr::HdSceneDelegate* sceneDelegate,
              pxr::HdRenderParam*   renderParam,
              pxr::HdDirtyBits*     dirtyBits) override;


    // Fill in a layer assignment using a given material.
    // Set default for a missing surface shader if volume is false, 
    // and for a missing volume shader if volume is true.
     static void assign(MoonrayAssignment& layerAssignment,
                        const pxr::SdfPath& materialId,
                        HdMoonray_RenderDelegate& renderDelegate,
                        pxr::HdSceneDelegate* sceneDelegate,
                        bool volume = false);

private:

    MoonrayObject getSurface(HdMoonray_RenderDelegate&, pxr::HdSceneDelegate*, bool canReturnNull = true);
    MoonrayObject getDisplacement(HdMoonray_RenderDelegate&, pxr::HdSceneDelegate*);
    MoonrayObject getVolumeShader(HdMoonray_RenderDelegate&, pxr::HdSceneDelegate*);

    bool mSurfaceDirty = true;
    bool mDisplacementDirty = true;
    bool mVolumeShaderDirty = true;
    MoonrayObject mMoonrayMaterial;
    MoonrayObject mMoonrayDisplacement;
    MoonrayObject mMoonrayVolumeShader;
    std::mutex mCreateMutex;
};

}

