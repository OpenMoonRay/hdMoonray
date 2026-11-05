// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/base/vt/dictionary.h>
#include <pxr/imaging/hd/types.h>

#include <string>

namespace scene_rdl2 { namespace rdl2 { 
    class RenderOutput; 
}}
namespace hdMoonray {


class MoonrayOutput
{
public:
    MoonrayOutput() : mRenderOutput(nullptr) {}
    MoonrayOutput(scene_rdl2::rdl2::RenderOutput* renderOutput) : mRenderOutput(renderOutput) {}

    scene_rdl2::rdl2::RenderOutput* renderOutput() const { return mRenderOutput; }
    const std::string& objectName() const; 

    bool isNull() const { return mRenderOutput == nullptr; }
    bool isValid() const { return mRenderOutput != nullptr; }

    /// A null RenderOutput* is also used to indicate the beauty (rgba) buffer).
    static MoonrayOutput beautyOutput() { return MoonrayOutput(); }
    bool isBeauty() const { return mRenderOutput == nullptr; }

    void setAsPrimvar(const std::string& pvName, pxr::HdFormat format);
    void setAsCryptomatte();
    void setAsDepth();
    void setAsNormal();
    void setAsSt();
    void setAsLpe(const std::string& lpeName);
    void setAsShader(const std::string& shaderName);

    void applySettings(const pxr::VtDictionary& aovSettings);

    void setMinFilter();
    void setClosestFilter();

    void setFileName(const std::string& fileName);

    void deactivate();

    std::string getPrimvarName() const;

private:
    scene_rdl2::rdl2::RenderOutput* mRenderOutput;
};

} // namespace hdMoonray