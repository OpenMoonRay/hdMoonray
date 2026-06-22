// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>

#include <scene_rdl2/scene/rdl2/Types.h>

#include <memory>
#include <string>

namespace hdMoonray {

class ColorManagement
{
public:
    ColorManagement();
    ~ColorManagement();

    bool setRenderingColorSpace(const pxr::TfToken& token);

    const pxr::TfToken& renderingColorSpaceToken() const;
    const std::string& workingColorSpace() const;
    bool hasWorkingColorTransform() const;
    std::string diagnosticSummary() const;
    bool diagnosticNeedsWarning() const;
    bool ocioEnvUnset() const;

    scene_rdl2::rdl2::Rgb toWorkingSpace(const scene_rdl2::rdl2::Rgb& color) const;
    scene_rdl2::rdl2::Rgba toWorkingSpace(const scene_rdl2::rdl2::Rgba& color) const;
    pxr::GfVec3f toWorkingSpace(const pxr::GfVec3f& color) const;
    pxr::GfVec4f toWorkingSpace(const pxr::GfVec4f& color) const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace hdMoonray
