// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "renderSettingsPrim.h"

#include <pxr/imaging/hd/renderPass.h>

namespace hdMoonray {

class HdMoonray_RenderDelegate;
class HdMoonray_Camera;

/// RenderPass represents a single render iteration, rendering a view of the
/// scene (the HdRprimCollection) for a specific viewer (the camera/viewport
/// parameters in HdRenderPassState) to the current draw target.

class HdMoonray_RenderPass final: public pxr::HdRenderPass
{
public:
    // constructor. Directly reads data such as the RenderContext from RenderDelegate
    HdMoonray_RenderPass(
        pxr::HdRenderIndex* index,
        const pxr::HdRprimCollection& collection,
        HdMoonray_RenderDelegate* d
    ) : HdRenderPass(index, collection), mRenderDelegate(*d) { }

    ~HdMoonray_RenderPass();

    bool IsConverged() const override;

protected:
    void _Execute(pxr::HdRenderPassStateSharedPtr const& renderPassState,
                  pxr::TfTokenVector const &renderTags) override;

    void _MarkCollectionDirty() override;
    void _Sync() override;

private:
    void setupSceneVars(HdMoonray_Camera* camera,
                        int imageWidth, int imageHeight);
    void execFromRenderPassState(const pxr::HdRenderPassStateSharedPtr& renderPassState,
                                 const pxr::TfTokenVector& renderTags,
                                 pxr::HdSceneIndexBaseRefPtr sceneIndex);

    void execFromRenderSettingsPrim(const HdMoonray_RenderSettings* rsprim);
    void renderProduct(const pxr::HdRenderSettings::RenderProduct& product);

    void showRenderPass(const pxr::HdRenderPassStateSharedPtr& renderPassState,
                        const pxr::TfTokenVector& renderTags) const;

    HdMoonray_RenderDelegate& mRenderDelegate;
    mutable bool mDeferIsConverged = false;
    bool mProductRenderComplete = false; 
    bool mShown = false;
};

}

