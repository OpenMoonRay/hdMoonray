// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ArrasSettings.h"

#include <hydramoonray/Renderer.h>

#include <mcrt_dataio/client/receiver/ClientReceiverFb.h>
#include <sdk/sdk.h>
#include <atomic>

namespace scene_rdl2 {namespace rdl2 { class RenderOutput; } }

namespace hdMoonray {

class RenderSettings;

class ArrasRenderer : public Renderer
{
public:
    ArrasRenderer();
    ~ArrasRenderer();

    void addDescriptors(pxr::HdRenderSettingDescriptorList& descriptorList) const override
        { mSettings.addDescriptors(descriptorList); }

    void beginUpdate() override;
    void endUpdate() override;
    bool isUpdateActive() const override { return mUpdateActive; }
    // force the next endUpdate to send a delta, even if it is empty
    void forceUpdate() { mUpdateActive = true; mSendEmptyUpdate = true; }

    void pause() override;
    void resume() override;
    bool isPaused() const override { return mPaused; }

    float getProgress() const override;
    float getElapsedSeconds() const override;
    bool isFrameComplete() const override;

    bool allocate(scene_rdl2::rdl2::RenderOutput*, PixelData&, const PixelSize&) override;
    bool resolve(scene_rdl2::rdl2::RenderOutput*, PixelData&) override;
    void deallocate(scene_rdl2::rdl2::RenderOutput*, PixelData&) override;

    void applySettings(const RenderSettings&) override;

    void invalidateAllTextureResources() override;
    void restartRenderer() override;
private:

    void initArras();
    bool connect(bool resetFailCount);

     // Called when messages are received from the renderer
    void messageHandler(const arras4::api::Message& msg);

    // Called whenever an exception is thrown.
    void exceptionHandler(const std::exception& e);

    // Record that the frame in flight cannot finish, and why. Called from the
    // Arras callback threads; the failure is acted on from resolve(), which runs
    // on the thread driving UsdAppUtilsFrameRecorder's convergence loop.
    void setFrameFailure(const std::string& reason);
    std::string getFailureReason() const;

    // Called when statuses are received from other components in Arras.
    void statusHandler(const std::string& msg);

    void updateStatusString(mcrt_dataio::ClientReceiverFb::BackendStat stat, float renderPrepProgress);
    
    std::unique_ptr<arras4::sdk::SDK> mSDK;
    std::shared_ptr<mcrt_dataio::ClientReceiverFb> mFbReceiver;

    // true indicates that the SceneContext may have changed,
    // so we should generate a delta on the next endUpdate() and
    // send it if it isn't empty
    std::atomic<bool> mUpdateActive{true};
    // under the conditions above, send the update even if is empty
    // (i.e. force a re-render even if the scene hasn't changed :
    //  used to rerender on texture reload, for example)
    // cleared by endUpdate
    std::atomic<bool> mSendEmptyUpdate{false};

    bool mPaused = false;

    std::atomic<bool> mConnected{false};

    // Set when Arras reports the session stopped, or the SDK raises, while a
    // frame is in flight. Left alone the frame simply never completes, and the
    // recorder's convergence loop -- which has no timeout -- waits forever.
    std::atomic<bool> mFrameFailed{false};
    mutable std::mutex mFailureMutex;
    std::string mFailureReason;

    std::mutex mMutex;

    // render progress value from the last received image.
    // -1 if no images have been received at all
    float mProgress{-1};

    // elapsed render time
    float mElapsedSeconds{0};

    // have we sent the first message to the current session?
    bool mFirstMessageSent{false};

    // counter value sent with an RDL update. This will be returned
    // with images generated as a result of that update.
    // Therefore, despite latency, when we
    // receive an incoming image we know if the update was applied prior
    // to generation of the image
    uint32_t mLatestUpdateFrameId{0};

    // have we received a complete render of the last sent update?
    bool mFrameComplete{false};

    // number of successive connection failures
    int mFailedConnects{0};

    ArrasSettings mSettings;
    mcrt_dataio::ClientReceiverFb::DenoiseMode mCurrentDenoiseMode;
    mcrt_dataio::ClientReceiverFb::DenoiseEngine mCurrentDenoiseEngine;

};

}
