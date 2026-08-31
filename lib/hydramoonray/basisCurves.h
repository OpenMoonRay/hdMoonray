// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometryBase.h"
#include <pxr/imaging/hd/basisCurves.h>

namespace hdMoonray {

class HdMoonray_BasisCurves final : public pxr::HdBasisCurves, public HdMoonray_GeometryBase
{
public:

    explicit HdMoonray_BasisCurves(pxr::SdfPath const& id);

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

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
    void syncAttributes(pxr::HdSceneDelegate* sceneDelegate, HdMoonray_RenderDelegate& renderDelegate,
                        pxr::HdDirtyBits* dirtyBits, const pxr::TfToken& reprToken) override;
    void primvarChanged(pxr::HdSceneDelegate *sceneDelegate, HdMoonray_RenderDelegate& renderDelegate,
                        const pxr::TfToken& name,  const pxr::VtValue& value,
                        const pxr::HdInterpolation& interp, const pxr::TfToken& role) override;

private:

    HdMoonray_BasisCurves(const HdMoonray_BasisCurves&)             = delete;
    HdMoonray_BasisCurves &operator =(const HdMoonray_BasisCurves&) = delete;

    void syncTopology(const pxr::HdBasisCurvesTopology& topology);
    void syncDisplayStyle(const pxr::HdDisplayStyle& style);
};

}

