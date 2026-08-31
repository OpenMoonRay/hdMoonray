// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ArrasRenderer.h"
#include <hydramoonray/NullRenderer.h>
#include <hydramoonray/renderDelegate.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE // this does not work unless inside the pxr namespace

class HdMoonrayRendererPlugin final : public pxr::HdRendererPlugin {
public:
    HdMoonrayRendererPlugin() {}

    pxr::HdRenderDelegate *CreateRenderDelegate() override {
        return new hdMoonray::HdMoonray_RenderDelegate(new hdMoonray::ArrasRenderer());
    }

    pxr::HdRenderDelegate *CreateRenderDelegate(pxr::HdRenderSettingsMap const& settings) override {

        if (hdMoonray::RenderSettings::staticDisableRender(settings)) {
            std::cout << "HdMoonray: rendering is DISABLED" << std::endl;
            auto rd = new hdMoonray::HdMoonray_RenderDelegate(new hdMoonray::NullRenderer(),settings);
            rd->options().setDisableRender(true);
            return rd;
        }

        return new hdMoonray::HdMoonray_RenderDelegate(new hdMoonray::ArrasRenderer(),settings);
    }

    void DeleteRenderDelegate(pxr::HdRenderDelegate *renderDelegate) override {
        delete renderDelegate;
    }

#if HD_API_VERSION < 83
    bool IsSupported(bool gpuEnabled = true) const override {
        return true;
    }
#else
    bool IsSupported(
        HdRendererCreateArgs const &rendererCreateArgs,
        std::string * reasonWhyNot = nullptr) const override {
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
