// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/imaging/hd/renderSettings.h>

namespace hdMoonray {

class HdMoonray_RenderSettings final : public pxr::HdRenderSettings
{
public:
    HdMoonray_RenderSettings(pxr::SdfPath const& id);

protected:
    void _Sync(pxr::HdSceneDelegate *sceneDelegate, 
           pxr::HdRenderParam *renderParam,
           const pxr::HdDirtyBits *dirtyBits) override;
};

}
