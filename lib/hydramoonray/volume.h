// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometryBase.h"
#include <pxr/imaging/hd/volume.h>
#include <pxr/imaging/hd/field.h>

namespace hdMoonray {

/// USD-style volume prim
class HdMoonray_Volume final : public pxr::HdVolume, public HdMoonray_GeometryBase
{
public:
    explicit HdMoonray_Volume(pxr::SdfPath const& id);

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
    
    bool isVolume() override { return true; }
    bool supportsUserData() override { return false; }

private:
    HdMoonray_Volume(const HdMoonray_Volume&)             = delete;
    HdMoonray_Volume &operator =(const HdMoonray_Volume&) = delete;
};

/// USD field prim, these belong to the volumes
class HdMoonray_OpenVdbAsset final : public pxr::HdField
{
public:
    HdMoonray_OpenVdbAsset(const pxr::SdfPath& id): pxr::HdField(id) {}

    void Sync(pxr::HdSceneDelegate*, pxr::HdRenderParam*, pxr::HdDirtyBits*) override;
    
    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override { return DirtyBits::AllDirty; }
    
    const std::string& filePath() const { return mFilePath; }
    pxr::TfToken name() const { return mName; }
    int index() const { return mIndex; }

private:
    std::string mFilePath;
    pxr::TfToken mName;
    int mIndex = -1;
};

}
