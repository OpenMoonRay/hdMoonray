// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometryBase.h"
#include <pxr/imaging/hd/mesh.h>

namespace hdMoonray {


class HdMoonray_Mesh final : public pxr::HdMesh, public HdMoonray_GeometryBase
{
public:
    explicit HdMoonray_Mesh(pxr::SdfPath const& id);

    /// Dirty bits to pass to first call to Sync()
    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Update the data identified by dirtyBits. Must not query other data.
    void Sync(pxr::HdSceneDelegate* sceneDelegate,
              pxr::HdRenderParam*   renderParam,
              pxr::HdDirtyBits*     dirtyBits,
              const pxr::TfToken&   reprToken) override;

    void Finalize(pxr::HdRenderParam* renderParam) override;

    // used to get the geometry associated with a MeshLight
    // not threadsafe, use with caution
    MoonrayObject geometryForMeshLight(HdMoonray_RenderDelegate& renderDelegate);

protected:
    // Hydra overrides
    void _InitRepr(pxr::TfToken const &reprToken, pxr::HdDirtyBits *dirtyBits) override;
    pxr::HdDirtyBits _PropagateDirtyBits(pxr::HdDirtyBits bits) const override;

    // HdMoonray_GeometryBase overrides
    void syncAttributes(pxr::HdSceneDelegate* sceneDelegate, HdMoonray_RenderDelegate& renderDelegate,
                        pxr::HdDirtyBits* dirtyBits, const pxr::TfToken& reprToken) override;
    void primvarChanged(pxr::HdSceneDelegate *sceneDelegate, HdMoonray_RenderDelegate& renderDelegate,
                        const pxr::TfToken& name,  const pxr::VtValue& value,
                        const pxr::HdInterpolation& interp, const pxr::TfToken& role) override;

    void primvarAttributeOverride(const std::string& name, const pxr::VtValue& value, HdMoonray_RenderDelegate& renderDelegate) override;

private:
    HdMoonray_Mesh(const HdMoonray_Mesh&)             = delete;
    HdMoonray_Mesh &operator =(const HdMoonray_Mesh&) = delete;

    void syncTopology(const pxr::HdMeshTopology& topology);
    void syncSubdivScheme(const pxr::HdMeshTopology& topology,
                        pxr::HdSceneDelegate *sceneDelegate,
                        HdMoonray_RenderDelegate& renderDelegate,
                        const pxr::TfToken& reprToken);
    void syncSubdivTags(const pxr::PxOsdSubdivTags& tags);
    void syncCryptomatteUserData(HdMoonray_RenderDelegate& renderDelegate);

    float clampMeshResolution(float meshResolution, HdMoonray_RenderDelegate& renderDelegate) const;
};

}
