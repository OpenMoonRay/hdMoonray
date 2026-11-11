// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/base/gf/vec4f.h>

#include "PixelData.h"
#include "renderVar.h"


namespace hdMoonray {

class HdMoonray_RenderDelegate;
class HdMoonray_Camera;

class HdMoonray_RenderBuffer: public pxr::HdRenderBuffer
{
public:
    explicit HdMoonray_RenderBuffer(const pxr::SdfPath& id): pxr::HdRenderBuffer(id) {}
    ~HdMoonray_RenderBuffer() {}

    /// Get allocation information from the scene delegate.
    void Sync(pxr::HdSceneDelegate*, pxr::HdRenderParam*, pxr::HdDirtyBits*) override;

    /// Deallocate before deletion.
    void Finalize(pxr::HdRenderParam*) override;

    /// Allocate a new buffer with the given dimensions and format.
    bool Allocate(const pxr::GfVec3i& dimensions, pxr::HdFormat, bool multiSampled) override;

    /// Accessors
    unsigned GetWidth() const override { return mPixelData.mWidth; }
    unsigned GetHeight() const override { return mPixelData.mHeight; }
    unsigned GetDepth() const override { return 1; }
    pxr::HdFormat GetFormat() const override { return mFormat; }
    // aovResolveTask does not call Resolve() unless this is true:
    bool IsMultiSampled() const override { return true; }

    /// It looks like this is intended to stop Resolve() from being called, but it does not work
    bool IsConverged() const override;

    /// Get the buffer data correct. This is what calls snapshot.
    void Resolve() override;

    /// Map the buffer for reading/writing. This is for locking, but not used by
    /// Moonray, where Resolve() is the only thing that changes the buffer
    void* Map() override { mMappers++; return mPixelData.mData; }
    void Unmap() override { mMappers--; }
    bool IsMapped() const override { return mMappers.load() != 0; }

    // hdMoonray api:
    void bind(const pxr::HdRenderPassAovBinding&, const HdMoonray_Camera*);

private:
    void _Deallocate() override;

    PixelData mPixelData;
    pxr::HdFormat mFormat = pxr::HdFormatInvalid;

    HdMoonray_RenderDelegate* mRenderDelegate = nullptr;
    HdMoonray_RenderVar mRenderVar;

    // true after bind() is called
    bool mBound = false;
    bool bound() const { return mBound; }

    // data for Resolve() post-processing:
    float mNear = 1;
    float mFar = 2;
    alignas(16) pxr::GfVec4f clearValue{0};

    std::atomic<int> mMappers{0};

    std::vector<int32_t> intBuffer;
};

}

