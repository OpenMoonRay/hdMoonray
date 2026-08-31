// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "hydra2_utils.h"
#include "MoonrayObject.h"

#include <pxr/imaging/hd/sprim.h>
#include <pxr/imaging/hd/material.h>

namespace hdMoonray {

class HdMoonray_RenderDelegate;

class MoonrayLightFilter : public pxr::HdSprim
{
public:
    MoonrayLightFilter(const pxr::TfToken& type, const pxr::SdfPath& id):
      pxr::HdSprim(id), mType(type) {}

    /// Dirty bits to pass to first call to Sync()
    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Update the data identified by dirtyBits. Must not query other data.
    void Sync(pxr::HdSceneDelegate* sceneDelegate,
              pxr::HdRenderParam*   renderParam,
              pxr::HdDirtyBits*     dirtyBits) override;

    const pxr::TfToken& type() const { return mType; }

    static MoonrayObject getLightFilter(const pxr::SdfPath& id,
                                        HdMoonray_RenderDelegate& renderDelegate,
                                        pxr::HdSceneDelegate *sceneDelegate);

    void Finalize(pxr::HdRenderParam *renderParam) override;

private:
    MoonrayObject getOrCreateFilter(const PrimAccess& access,
                                    HdMoonray_RenderDelegate& renderDelegate);
    std::string rdlClassName(const pxr::SdfPath& id,
                             pxr::HdSceneDelegate *sceneDelegate);
    void syncParams(const PrimAccess& access,
                    HdMoonray_RenderDelegate& renderDelegate);
    void syncXform(const PrimAccess& access,
                   HdMoonray_RenderDelegate& renderDelegate);
    void syncTextureMap(const PrimAccess& access,
                        HdMoonray_RenderDelegate& renderDelegate);
    void syncProjector(const PrimAccess& access,
                        HdMoonray_RenderDelegate& renderDelegate); 
    void syncCombineFilters(const PrimAccess& access,
                            HdMoonray_RenderDelegate& renderDelegate);

    pxr::TfToken mType;
    MoonrayObject mLightFilter;
    std::mutex mCreateMutex;

    // the name of a category holding all geometry filtered by this light
    pxr::TfToken mLightFilterCategory;

};

}

