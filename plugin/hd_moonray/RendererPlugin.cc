// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ArrasRenderer.h"
#include <hydramoonray/NullRenderer.h>
#include <hydramoonray/RenderDelegate.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#if PXR_VERSION >= 2603
#include <pxr/imaging/hd/rendererCreateArgs.h>
#endif

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE // this does not work unless inside the pxr namespace

class HdMoonrayRendererPlugin final : public pxr::HdRendererPlugin {
public:
    HdMoonrayRendererPlugin() {}

    pxr::HdRenderDelegate *CreateRenderDelegate() override {
        return new hdMoonray::RenderDelegate(new hdMoonray::ArrasRenderer());
    }

    pxr::HdRenderDelegate *CreateRenderDelegate(pxr::HdRenderSettingsMap const& settings) override {
        auto it = settings.find(pxr::TfToken("disableRender"));
        if (it != settings.end()) {
            if (it->second.Get<bool>()) {
                auto rd = new hdMoonray::RenderDelegate(new hdMoonray::NullRenderer(),settings);
                rd->setDisableRender(true);
                return rd;
            }
        }
        return new hdMoonray::RenderDelegate(new hdMoonray::ArrasRenderer(),settings);
    }

    void DeleteRenderDelegate(pxr::HdRenderDelegate *renderDelegate) override {
        delete renderDelegate;
    }
#if PXR_VERSION >= 2302
    bool IsSupported(bool gpuEnabled = true) const override {
        return true;
    }
#else
bool IsSupported() const override {
        return true;
    }
#endif
#if PXR_VERSION >= 2603
    // USD 26.3 added a new pure-virtual IsSupported(HdRendererCreateArgs const&, ...)
    // overload (the bool-only overload above is now a deprecated, non-pure helper).
    bool IsSupported(pxr::HdRendererCreateArgs const &rendererCreateArgs,
                     std::string *reasonWhyNot = nullptr) const override {
        return true;
    }
#endif

private:
    // uncopyable
    HdMoonrayRendererPlugin(const HdMoonrayRendererPlugin&)             = delete;
    HdMoonrayRendererPlugin &operator =(const HdMoonrayRendererPlugin&) = delete;
};

TF_REGISTRY_FUNCTION(TfType)
{
    pxr::HdRendererPluginRegistry::Define<HdMoonrayRendererPlugin>();
}

PXR_NAMESPACE_CLOSE_SCOPE
