// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <scene_rdl2/render/logging/logging.h>

#include <pxr/imaging/hd/types.h>
#include <pxr/usd/sdf/path.h>


namespace hdMoonray {

using scene_rdl2::logging::Logger;

void hdmLogSyncStart(const std::string& type, const pxr::SdfPath& id, pxr::HdDirtyBits *dirtyBits);
void hdmLogSyncEnd(const pxr::SdfPath& id);

void hdmLogRenderBuffer(const std::string& msg,const pxr::SdfPath& id);

void hdmLogArras(const std::string& msg);

}

