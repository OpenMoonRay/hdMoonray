// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "NullRenderer.h"

namespace hdMoonray {

NullRenderer::NullRenderer()
{
    mSceneContext = new scene_rdl2::rdl2::SceneContext();
    mSceneContext->setProxyModeEnabled(true);
}

NullRenderer::~NullRenderer()
{
    delete mSceneContext;
}

bool 
NullRenderer::allocate(MoonrayOutput output, PixelData& pd, const PixelSize& request)
{
    // we have to allocate a buffer, even though we won't fill it,
    // because the calling app may fail without one
    // (we could perhaps add an option to disable this)
    pd.mChannels = output.isBeauty() ? 4 : request.mChannels;
    pd.mWidth = request.mWidth;
    pd.mHeight = request.mHeight;
    pd.vec.resize(pd.mWidth * pd.mHeight * pd.mChannels);
    pd.mData = pd.vec.data();

    return true;
}

bool
NullRenderer::resolve(MoonrayOutput, PixelData&, bool) 
{
    return false;
}
    
void 
NullRenderer::deallocate(MoonrayOutput, PixelData&) 
{
}

}
