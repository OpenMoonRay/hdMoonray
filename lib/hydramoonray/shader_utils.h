// Copyright 2023-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MoonrayObject.h"
#include <pxr/usd/sdf/path.h>
#include <pxr/imaging/hd/sceneDelegate.h>

namespace hdMoonray {

class HdMoonray_RenderDelegate;

// Get the network for the given terminal from the material resource
// associated with id, and create corresponding SceneObjects
MoonrayObject 
getTerminal(const pxr::SdfPath& id, 
            const std::string& terminalName, 
            HdMoonray_RenderDelegate&, pxr::HdSceneDelegate*);

// This is a bit of a hack, used to support texture maps in MoonrayLightFilter
// it is intended to get a single node connected to a given parameter
MoonrayObject
getNodeByConnection(const pxr::SdfPath& id, 
                    const std::string& paramName,
                    HdMoonray_RenderDelegate&, pxr::HdSceneDelegate*);

}