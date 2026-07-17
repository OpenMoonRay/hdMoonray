// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "RenderPass.h"
#include "RenderBuffer.h"
#include "RenderDelegate.h"
#include "Camera.h"

#include <pxr/imaging/cameraUtil/framing.h>
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <scene_rdl2/render/logging/logging.h>
#include <scene_rdl2/scene/rdl2/Utils.h>

#include <iostream>
#include <chrono>
#include <cstdlib>
#include <sstream>

//#define DEBUG_MSG

namespace {

bool
cameraFramingDiagEnabled()
{
    static const bool enabled = std::getenv("HDMR_CAMERA_FRAMING_DIAG") != nullptr;
    return enabled;
}

template <typename T>
std::string
diagString(const T& value)
{
    std::ostringstream out;
    out << value;
    return out.str();
}

std::string
tokenListString(const pxr::TfTokenVector& tokens)
{
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) {
            out << ", ";
        }
        out << tokens[i].GetString();
    }
    out << "]";
    return out.str();
}

void
logRenderPassFraming(const pxr::HdRenderPassStateSharedPtr& renderPassState,
                     const pxr::TfTokenVector& renderTags,
                     size_t aovBindingCount,
                     const pxr::SdfPath& cameraId,
                     int width,
                     int height)
{
    if (!cameraFramingDiagEnabled()) {
        return;
    }

#if PXR_VERSION >= 2102
    const pxr::CameraUtilFraming& framing = renderPassState->GetFraming();
    std::cerr << "[HDMR_CAMERA_FRAMING_DIAG] RenderPass._Execute"
              << " viewport=" << diagString(renderPassState->GetViewport())
              << " framingValid=" << (framing.IsValid() ? "1" : "0")
              << " displayWindow=" << diagString(framing.displayWindow)
              << " dataWindow=" << diagString(framing.dataWindow)
              << " pixelAspectRatio=" << framing.pixelAspectRatio
              << " computedWidth=" << width
              << " computedHeight=" << height
              << " computedAspect=" << (height ? double(width) / double(height) : 0.0)
              << " cameraId=" << cameraId
              << " renderTags=" << tokenListString(renderTags)
              << " aovBindingCount=" << aovBindingCount
              << std::endl;
#else
    std::cerr << "[HDMR_CAMERA_FRAMING_DIAG] RenderPass._Execute"
              << " viewport=" << diagString(renderPassState->GetViewport())
              << " framingValid=0"
              << " computedWidth=" << width
              << " computedHeight=" << height
              << " computedAspect=" << (height ? double(width) / double(height) : 0.0)
              << " cameraId=" << cameraId
              << " renderTags=" << tokenListString(renderTags)
              << " aovBindingCount=" << aovBindingCount
              << std::endl;
#endif
}

void
logSceneVariableFraming(int width, int height, float frame)
{
    if (!cameraFramingDiagEnabled()) {
        return;
    }

    std::cerr << "[HDMR_CAMERA_FRAMING_DIAG] RenderPass.SceneVariables"
              << " image_width=" << width
              << " image_height=" << height
              << " frame=" << frame
              << std::endl;
}

}

namespace hdMoonray
{

using scene_rdl2::logging::Logger;

RenderPass::~RenderPass()
{
}

bool
RenderPass::IsConverged() const
{
    // This oddness is to work around a Hydra bug that has been reported to pixar.
    // It does not call RenderBuffer::Resolve after IsConverged returns true, so
    // it never shows the last generated image. Fix this by requiring IsConverged()
    // to be called twice to return true and disable the _Execute call between them.
    if (mDeferIsConverged) {
        return true;
    } else {
        const bool frameComplete = renderDelegate.renderer().isFrameComplete();
        const bool terminalError = renderDelegate.renderer().hasTerminalError();
        if (terminalError && !mTerminalErrorLogged) {
            Logger::error("Renderer reached a terminal error state; unblocking Hydra without "
                          "marking the frame as successfully completed.");
            mTerminalErrorLogged = true;
        }
        mDeferIsConverged = frameComplete || terminalError;
        return false;
    }
}

void
RenderPass::_MarkCollectionDirty()
{
}

void
RenderPass::_Sync()
{
}

void
RenderPass::_Execute(const pxr::HdRenderPassStateSharedPtr& renderPassState,
                     const pxr::TfTokenVector& renderTags)
{
    renderDelegate.noteRenderPassExecuted();

    // Update for any changes in render settings, may create a new renderer
    renderDelegate.getRendererApplySettings();

    // Deal with changes to "purpose"
    renderDelegate.setRenderTags(GetRenderIndex(), renderTags);

    const scene_rdl2::rdl2::SceneContext& sc(renderDelegate.sceneContext());
    const scene_rdl2::rdl2::SceneVariables& sv(sc.getSceneVariables());

    int w, h;
#if PXR_VERSION >= 2102
    if (renderPassState->GetFraming().IsValid()) {
        const pxr::GfRect2i& dw(renderPassState->GetFraming().dataWindow);
        w = dw.GetWidth();
        h = dw.GetHeight();
    } else // older clients may use viewport instead of framing
#endif
    {   const pxr::GfVec4f& vp = renderPassState->GetViewport();
        w = vp[2];
        h = vp[3];
    }

    const Camera* camera(dynamic_cast<const Camera*>(renderPassState->GetCamera()));
    const pxr::HdRenderPassAovBindingVector& aovBindings = renderPassState->GetAovBindings();
    logRenderPassFraming(renderPassState,
                         renderTags,
                         aovBindings.size(),
                         camera ? camera->GetId() : pxr::SdfPath(),
                         w,
                         h);
    if (not camera) {
        Logger::error("RenderPassState without camera is unsupported");
        // It could the view+proj matricies below and update a fake Camera. But
        // usdview is not using this.
        return;
    }
    const_cast<Camera*>(camera)->setAsPrimaryCamera(renderDelegate, double(w)/h);

    float frame = 0;
    scene_rdl2::rdl2::FloatVector motionSteps(2);
    auto usdDelegate = renderDelegate.usdImagingDelegate();
    if (usdDelegate) {
        pxr::UsdTimeCode currTime = usdDelegate->GetTime();
        if (currTime.IsNumeric()) {
            frame = (float)currTime.GetValue();
            pxr::GfInterval interval = usdDelegate->GetCurrentTimeSamplingInterval();
            motionSteps[0] = float(interval.GetMin()) - frame;
            motionSteps[1] = float(interval.GetMax()) - frame;
        }
    } else {
        std::pair<float, float> interval =
            const_cast<Camera*>(camera)->getTimeSamplingInterval();
        motionSteps[0] = interval.first;
        motionSteps[1] = interval.second;
    }
    renderDelegate.renderSettings().getHoudiniFrame(frame);

    const bool motionBlur = renderDelegate.getEnableMotionBlur() &&
                            motionSteps[0] < motionSteps[1];
    if (not motionBlur) { motionSteps[0] = -1; motionSteps[1] = 0; }

    if (frame != sv.get(sv.sFrameKey) ||
        motionSteps != sv.get(sv.sMotionSteps) ||
        motionBlur != sv.get(sv.sEnableMotionBlur) ||
        w != sv.get(sv.sImageWidth) ||
        h != sv.get(sv.sImageHeight))
    {
        scene_rdl2::rdl2::SceneVariables& wsv(renderDelegate.acquireSceneContext().getSceneVariables());
        UpdateGuard guard(wsv);
        wsv.set(sv.sFrameKey, frame);
        wsv.set(sv.sMotionSteps, motionSteps);
        wsv.set(sv.sEnableMotionBlur, motionBlur);
        wsv.set(sv.sImageWidth, w);
        wsv.set(sv.sImageHeight, h);
    }
    logSceneVariableFraming(w, h, frame);

    // const pxr::GfMatrix4d& view = renderPassState->GetWorldToViewMatrix(); // same as camera->GetViewMatrix()
    // // This matrix contains info that is *not* in the Camera, to make the pixels square in the viewport:
    // const pxr::GfMatrix4d& proj = renderPassState->GetProjectionMatrix();

    // tell renderer about any new bindings
    for (const pxr::HdRenderPassAovBinding& aovBinding : aovBindings) {
        RenderBuffer* buffer = reinterpret_cast<RenderBuffer*>(aovBinding.renderBuffer);
        buffer->bind(aovBinding, camera);      
    }

    if (renderDelegate.renderer().isUpdateActive()) {      
        mDeferIsConverged = false;
        mTerminalErrorLogged = false;
    }
    renderDelegate.renderer().endUpdate();

    static std::string prevRdlaOutput;
    const std::string& rdlOutput(renderDelegate.rdlOutput());
    if (rdlOutput != prevRdlaOutput) {
        prevRdlaOutput = rdlOutput;
        if (not rdlOutput.empty()) {
            scene_rdl2::rdl2::writeSceneToFile(sc,
                                          rdlOutput,
                                          false, // deltas
                                          true); // skip defaults
        }
    }
}

}
