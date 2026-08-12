// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "renderSettingsPrim.h"

#include "pxr/imaging/hd/sceneDelegate.h"

#include <iostream>

using namespace pxr;

namespace hdMoonray
{

HdMoonray_RenderSettings::HdMoonray_RenderSettings(SdfPath const& id) :
    HdRenderSettings(id)
{
}

void 
HdMoonray_RenderSettings::_Sync(HdSceneDelegate *sceneDelegate,
                                HdRenderParam *renderParam,
                                const HdDirtyBits *dirtyBits)
{
}

}
