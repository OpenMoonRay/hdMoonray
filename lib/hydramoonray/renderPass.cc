// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "renderPass.h"
#include "renderBuffer.h"
#include "renderDelegate.h"

#include "camera.h"
#include "tokens.h"

#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/utils.h>

#include <iostream>
#include <chrono>

using namespace pxr;
namespace hdMoonray
{

HdMoonray_RenderPass::~HdMoonray_RenderPass()
{
}

bool
HdMoonray_RenderPass::IsConverged() const
{
    if (mProductRenderComplete) {
        return true;
    }

    // This oddness is to work around a Hydra bug that has been reported to pixar.
    // It does not call RenderBuffer::Resolve after IsConverged returns true, so
    // it never shows the last generated image. Fix this by requiring IsConverged()
    // to be called twice to return true and disable the _Execute call between them.
    if (mDeferIsConverged) {
        return true;
    } else {
        mDeferIsConverged = mRenderDelegate.renderer().isFrameComplete();
        return false;
    }
}

void
HdMoonray_RenderPass::_MarkCollectionDirty()
{
}

void
HdMoonray_RenderPass::_Sync()
{
}

void
HdMoonray_RenderPass::showRenderPass(const HdRenderPassStateSharedPtr& renderPassState,
                                     const TfTokenVector& renderTags) const
{
    std::cout << "Executing RenderPass" << std::endl;
    std::cout << "  Camera: " << renderPassState->GetCamera()->GetId() << std::endl;
    std::cout << "  AOVs: " << std::endl;
    const HdRenderPassAovBindingVector& aovBindings = renderPassState->GetAovBindings();
    for (const HdRenderPassAovBinding& aovBinding : aovBindings) {
        std::cout << "    " << aovBinding.aovName << " -> " << aovBinding.renderBuffer->GetId() << std::endl;
        std::cout << "     ";
        for (const auto& entry : aovBinding.aovSettings) {
            std::cout << entry.first << "=" << entry.second << " ";
        }
        std::cout << std::endl;
    }
}

// set up the Moonray scene variables for the pass
// NB this function must be careful not to trigger a scene update if the values are unchanged, 
// since that may cause the render to never terminate.
void
HdMoonray_RenderPass::setupSceneVars(HdMoonray_Camera* camera,
                                     int imageWidth, int imageHeight)
{
    camera->setAsPrimaryCamera(mRenderDelegate, double(imageWidth)/imageHeight); 
    
    // current frame
    double dframe = 0;
    HdSceneIndexBaseRefPtr sceneIndex = GetRenderIndex()->GetTerminalSceneIndex();
    if (sceneIndex) {
       HdUtils::GetCurrentFrame(sceneIndex, &dframe);
    }
    float frame = static_cast<float>(dframe);

    // motion blur
    std::pair<float, float> shutterInterval = camera->getShutterInterval();
    
    rdl2::FloatVector motionSteps(2);
    std::pair<float, float> interval = mRenderDelegate.scene().getTimeSamplingInterval();
    motionSteps[0] = interval.first;
    motionSteps[1] = interval.second;

    const bool motionBlur = mRenderDelegate.options().getEnableMotionBlur() && 
        (motionSteps[0] != motionSteps[1]) &&
        (shutterInterval.first != shutterInterval.second);

    // time sampling interval is needed iff motion blur is enabled
    mRenderDelegate.scene().enableTimeSamplingInterval(motionBlur);

    const rdl2::SceneVariables& sv(mRenderDelegate.sceneContext().getSceneVariables());
    if (frame != sv.get(sv.sFrameKey) ||
        motionSteps != sv.get(sv.sMotionSteps) ||
        motionBlur != sv.get(sv.sEnableMotionBlur) ||
        imageWidth != sv.get(sv.sImageWidth) ||
        imageHeight != sv.get(sv.sImageHeight))
    {
        rdl2::SceneVariables& wsv(mRenderDelegate.acquireSceneContext().getSceneVariables());
        wsv.beginUpdate();
        wsv.set(wsv.sFrameKey, frame);
        wsv.set(wsv.sMotionSteps, motionSteps);
        wsv.set(wsv.sEnableMotionBlur, motionBlur);
        wsv.set(wsv.sImageWidth, imageWidth);
        wsv.set(wsv.sImageHeight, imageHeight);
        wsv.endUpdate();
    }
}

// execute a render described by renderPassState and renderTags. Used for interactive rendering.
// This will be repeatedly called until IsConverged() returns true.
// It must be careful to avoid triggering a scene update if the values are unchanged, 
// since that may cause the render to never terminate.
void
HdMoonray_RenderPass::execFromRenderPassState(const HdRenderPassStateSharedPtr& renderPassState,
                                              const TfTokenVector& renderTags,
                                              HdSceneIndexBaseRefPtr sceneIndex)
{
    if (!mShown && mRenderDelegate.options().getShowRenderPasses() ) {
        showRenderPass(renderPassState, renderTags);
        mShown = true;
    }

    mRenderDelegate.applySettings();

    // Deal with changes to "purpose"
    mRenderDelegate.setRenderTags(GetRenderIndex(), renderTags);

    const rdl2::SceneContext& sc(mRenderDelegate.sceneContext());
    const rdl2::SceneVariables& sv(sc.getSceneVariables());

    int imageWidth, imageHeight;

    if (renderPassState->GetFraming().IsValid()) {
        const GfRect2i& dw(renderPassState->GetFraming().dataWindow);
        imageWidth = dw.GetWidth();
        imageHeight = dw.GetHeight();
    } else {
         // older clients may use viewport instead of framing
        const GfVec4f& vp = renderPassState->GetViewport();
        imageWidth = vp[2];
        imageHeight = vp[3];
    }

    const HdMoonray_Camera* camera(dynamic_cast<const HdMoonray_Camera*>(renderPassState->GetCamera()));
    if (not camera) {
        Logger::error("RenderPassState without camera is unsupported");
        // It could the view+proj matricies below and update a fake Camera. But
        // usdview is not using this.
        return;
    }

    setupSceneVars(const_cast<HdMoonray_Camera*>(camera), imageWidth, imageHeight);
    

    // AOV bindings
    const HdRenderPassAovBindingVector& aovBindings = renderPassState->GetAovBindings();
    for (const HdRenderPassAovBinding& aovBinding : aovBindings) {
        HdMoonray_RenderBuffer* buffer = reinterpret_cast<HdMoonray_RenderBuffer*>(aovBinding.renderBuffer);
        buffer->bind(aovBinding, camera);      
    }

    if (mRenderDelegate.renderer().isUpdateActive()) {      
        mDeferIsConverged = false;
    }
    mRenderDelegate.renderer().endUpdate();

    static std::string prevRdlaOutput;
    const std::string& rdlOutput(mRenderDelegate.options().getRdlOutput());
    if (rdlOutput != prevRdlaOutput) {
        prevRdlaOutput = rdlOutput;
        if (not rdlOutput.empty()) {
            rdl2::writeSceneToFile(mRenderDelegate.sceneContext(),
                                   rdlOutput,
                                   false, // deltas
                                   true); // skip defaults
        }
    }
}

// Execute a render described by a RenderSettings prim. Used for batch rendering.
// Render buffers are not used : output is written directly to disk by Moonray.
// This function returns when all the products are rendered. IsConverged() will return true after return.
void
HdMoonray_RenderPass::execFromRenderSettingsPrim(const HdMoonray_RenderSettings* rsprim)
{
    std::cout << "Using RenderSettings prim " << rsprim->GetId() << std::endl;
    const HdRenderSettings::RenderProducts& products = rsprim->GetRenderProducts();
    if (products.empty()) {
        mProductRenderComplete = true;
        Logger::error("RenderSettings prim ", rsprim->GetId(), " has no products");
        return;
    }

    for (const HdRenderSettings::RenderProduct& product : products) {
        mRenderDelegate.resetSettingsToDefaults();
        mRenderDelegate.setRenderSettings(rsprim->GetNamespacedSettings());
        mRenderDelegate.setRenderSettings(product.namespacedSettings);
        mRenderDelegate.applySettings();
        renderProduct(product);
    }
 
    mProductRenderComplete = true;
}

void
HdMoonray_RenderPass::renderProduct(const HdRenderSettings::RenderProduct& product)
{
    std::cout << "  Rendering product " << product.name << std::endl;
    // render tags TODO

    // framing
    int imageWidth = product.resolution[0];
    int imageHeight = product.resolution[1];
    std::cout << "    Resolution: " << imageWidth << "x" << imageHeight << std::endl;

    // camera
    HdSprim* cameraPrim = GetRenderIndex()->GetSprim(HdPrimTypeTokens->camera, product.cameraPath);
    HdMoonray_Camera* camera = dynamic_cast<HdMoonray_Camera*>(cameraPrim);
    if (!camera) {
        Logger::error("Cannot find camera '", product.cameraPath.GetString(),"'");
        mProductRenderComplete = true;
        return;
    }
    std::cout << "    Camera: " << camera->GetId() << std::endl;

    setupSceneVars(camera, imageWidth, imageHeight);
    
    // aovs
    std::string outputFile = product.name.GetString();
    std::vector<HdMoonray_RenderVar> renderVars(product.renderVars.size());
    unsigned i = 0;
    for (const HdRenderSettings::RenderProduct::RenderVar& renderVar : product.renderVars) {
        std::cout << "    Var " << renderVar.sourceName << std::endl;
        for (const auto& entry : renderVar.namespacedSettings) {
            std::cout << "      " << entry.first << "=" << entry.second << std::endl;
        }
        renderVars[i].setAov(product, renderVar, &mRenderDelegate);
        ++i;
    }

    // execute render   
    if (mRenderDelegate.options().getGenerateOnly()) {
        std::cout << "  Generate Only option enabled, skipping render" << std::endl;
    } else {
        std::cout << "Starting render..." << std::endl;
        mRenderDelegate.renderer().endUpdate();
        while (!mRenderDelegate.renderer().isFrameComplete()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    
        std::cout << "Product " << product.name << " render complete" << std::endl;
    }
    // write rdl if requested
    std::string rdlOutput = mRenderDelegate.options().getRdlOutput();
    if (!rdlOutput.empty()) {
        // if rdlOutput contains a "$P", replace with product name (ie. filename)
        size_t pos = rdlOutput.find("$P");
        if (pos != std::string::npos) {
            rdlOutput.replace(pos, 2, outputFile);
        }
        rdl2::writeSceneToFile(mRenderDelegate.sceneContext(), rdlOutput, false, true);
        std::cout << "Wrote RDL to " << rdlOutput << std::endl;
    }
}

void
HdMoonray_RenderPass::_Execute(const HdRenderPassStateSharedPtr& renderPassState,
                               const TfTokenVector& renderTags)
{
    HdSceneIndexBaseRefPtr sceneIndex = GetRenderIndex()->GetTerminalSceneIndex();
    
    // Determine if we should use a render settings prim or legacy render settings.
    HdMoonray_RenderSettings* rsprim = nullptr;
    if (sceneIndex) {
        SdfPath rsp;
        if (HdUtils::HasActiveRenderSettingsPrim(sceneIndex, &rsp)) {
            rsprim = dynamic_cast<HdMoonray_RenderSettings*>(
                GetRenderIndex()->GetBprim(HdPrimTypeTokens->renderSettings, rsp));
        }
    }
    // Apply client settings before deciding whether this is an interactive
    // Houdini render. Houdini 22 uses houdini:viewport for this distinction.
    mRenderDelegate.applySettings();

    // An active RenderSettings prim also exists in the Solaris viewport. It
    // must not switch IPR to the one-shot, disk-product batch path: Houdini
    // communicates the active prim's viewport settings through SetRenderSetting.
    if (!rsprim || mRenderDelegate.options().isHoudini()) {
        mProductRenderComplete = false;
        execFromRenderPassState(renderPassState, renderTags, sceneIndex);
        return;
    }

    // Offline product renders are one-shot, but any scene or RenderSettings
    // change must clear completion so Hydra can execute the updated product.
    const unsigned renderSettingsVersion = rsprim->getVersion();
    const bool renderSettingsChanged =
        renderSettingsVersion != mRenderSettingsVersion ||
        rsprim->GetAndResetHasDirtyProducts();
    if (renderSettingsChanged || mRenderDelegate.renderer().isUpdateActive()) {
        mProductRenderComplete = false;
        mRenderSettingsVersion = renderSettingsVersion;
    }
    if (!mProductRenderComplete) {
        execFromRenderSettingsPrim(rsprim);
    }
}

   

}

