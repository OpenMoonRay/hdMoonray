// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometryBase.h"
#include <pxr/imaging/hd/points.h>

namespace hdMoonray {

class HdMoonray_Points final : public pxr::HdPoints, public HdMoonray_GeometryBase
{
public:
    explicit HdMoonray_Points(pxr::SdfPath const& id);

    /// Dirty bits to pass to first call to Sync()
    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Update the data identified by dirtyBits. Must not query other data.
    void Sync(pxr::HdSceneDelegate* sceneDelegate,
              pxr::HdRenderParam*   renderParam,
              pxr::HdDirtyBits*     dirtyBits,
              const pxr::TfToken&   reprToken) override;

    void Finalize(pxr::HdRenderParam* renderParam) override;

protected:

    // Hydra overrides
    void _InitRepr(pxr::TfToken const &reprToken, pxr::HdDirtyBits *dirtyBits) override {}
    pxr::HdDirtyBits _PropagateDirtyBits(pxr::HdDirtyBits bits) const override { return bits; }

    // HdMoonray_GeometryBase overrides
    void primvarChanged(pxr::HdSceneDelegate *sceneDelegate, HdMoonray_RenderDelegate& renderDelegate,
                        const pxr::TfToken& name,  const pxr::VtValue& value,
                        const pxr::HdInterpolation& interp, const pxr::TfToken& role);
private:
    HdMoonray_Points(const HdMoonray_Points&)             = delete;
    HdMoonray_Points &operator =(const HdMoonray_Points&) = delete;
};

}

