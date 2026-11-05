// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "geometryBase.h"

#include <pxr/imaging/hd/sceneIndex.h>

// Handles any Moonray geometry shader : the RDL class name should be specified by
// procedural:class. Arbitrary RDL2 attribute "xyz" can be set by USD attribute 
// procedural:xyz.
//
// Requires an adapter to work with usdImaging : the adapter should override
// get(TfToken("parts")) to return a perPartMaterials ( == vector<pair<SdfPath,SdfPath>>)
// where each entry is the id of a part and the id of a material.

namespace hdMoonray {

class HdMoonray_Procedural final : public pxr::HdRprim, public HdMoonray_GeometryBase
{
public:
    explicit HdMoonray_Procedural(pxr::SdfPath const& id);
    
    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Sync(pxr::HdSceneDelegate* sceneDelegate,
              pxr::HdRenderParam*   renderParam,
              pxr::HdDirtyBits*     dirtyBits,
              const pxr::TfToken&   reprToken) override;

    void Finalize(pxr::HdRenderParam* renderParam) override;

    const pxr::TfTokenVector& GetBuiltinPrimvarNames() const override;

protected:

    // Hydra overrides
    void _InitRepr(pxr::TfToken const &reprToken, pxr::HdDirtyBits *dirtyBits) override;
    pxr::HdDirtyBits _PropagateDirtyBits(pxr::HdDirtyBits bits) const override { return bits; }

    // HdMoonray_GeometryBase overrides
    void syncAttributes(pxr::HdSceneDelegate* sceneDelegate, HdMoonray_RenderDelegate& renderDelegate,
                        pxr::HdDirtyBits* dirtyBits, const pxr::TfToken& reprToken) override;
    bool supportsUserData() override;

private:

    HdMoonray_Procedural(const HdMoonray_Procedural&)             = delete;
    HdMoonray_Procedural &operator =(const HdMoonray_Procedural&) = delete;

};

}
