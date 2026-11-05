// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MoonrayOutput.h"

#include <pxr/imaging/hd/aov.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/imaging/hd/renderSettings.h>

namespace hdMoonray {

class HdMoonray_RenderDelegate;

// RenderVar associates a Moonray RenderOutput object with a Hydra AOV.
// There is generally one RenderOutput for each AOV, except in the special cases of cryptomatte and instanceId.
// When rendering interactively, each AOV is associated with a RenderBuffer, which in turn uses a RenderVar to build its RenderOutput.
// In the interactive case, RenderBuffer can support some AOVs not supported by Moonray, by post-processing a RenderOutput that is supported.
// In the batch case, the RenderOutput is configured to write directly to a file, with no RenderBuffer, 
// and any post-processing must be done outside of HdMoonray.
class HdMoonray_RenderVar
{
public:

    void setAov(const pxr::HdRenderPassAovBinding& aovBinding,
                HdMoonray_RenderDelegate* renderDelegate);

    void setAov(const pxr::HdRenderSettings::RenderProduct& product,
                const pxr::HdRenderSettings::RenderProduct::RenderVar& renderVar,
                HdMoonray_RenderDelegate* renderDelegate);

    static pxr::HdAovDescriptor getAovDescriptor(pxr::TfToken const& name);

    MoonrayOutput renderOutput() const { return mOutput; }

    // extra outputs are used for nested instancers, which need to write out multiple AOVs for each instance
    size_t numExtraOutputs() const { return mExtraOutputs.size(); }
    MoonrayOutput extraOutputs(unsigned i) const { return mExtraOutputs[i]; }

    bool isOpenGLDepth() const { return mIsOpenGLDepth; }
    bool isBeauty() const { return mOutput.isBeauty(); }
    float emptyValue() const { return mEmptyValue; }

    void finalize();

private:

    void buildOutputs(const pxr::TfToken& sourceName,
                      const pxr::TfToken& sourceType,
                      pxr::HdFormat targetFormat,
                      const pxr::VtDictionary& aovSettings,
                      const std::string& fileName,
                      HdMoonray_RenderDelegate* renderDelegate);

    pxr::TfToken mSourceName;
    MoonrayOutput mOutput;
    std::vector<MoonrayOutput> mExtraOutputs;

    bool mIsOpenGLDepth = false;
    float mEmptyValue = 0.0f;
};

}

