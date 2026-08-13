// Copyright 2023-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MoonrayObject.h"
#include <pxr/imaging/hd/camera.h>

#include <mutex>

namespace hdMoonray {

    class HdMoonray_RenderDelegate;

class HdMoonray_Camera: public pxr::HdCamera
{
public:
    explicit HdMoonray_Camera(const pxr::SdfPath& id): pxr::HdCamera(id) {}

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Sync(pxr::HdSceneDelegate* sceneDelegate,
              pxr::HdRenderParam*   renderParam,
              pxr::HdDirtyBits*     dirtyBits) override;

    void Finalize(pxr::HdRenderParam *renderParam) override;
    ~HdMoonray_Camera();

    static MoonrayObject createCamera(pxr::HdSceneDelegate*, HdMoonray_RenderDelegate&, const pxr::SdfPath&);

    float getNear() const { return mNear; }
    float getFar() const { return mFar; }
    std::pair<float, float> getShutterInterval() const { return std::make_pair(mShutterOpen, mShutterClose); }

    void setAsPrimaryCamera(HdMoonray_RenderDelegate&, double aspectRatio);
    pxr::HdSceneDelegate* getSceneDelegate() const { return mSceneDelegate; }

private:

    MoonrayObject createCameraInternal(pxr::HdSceneDelegate*, HdMoonray_RenderDelegate&);
    pxr::HdSceneDelegate* mSceneDelegate = nullptr; // set by Sync, needed by setAsPrimaryCamera
    void updateCamera(pxr::HdSceneDelegate*, HdMoonray_RenderDelegate&, pxr::HdDirtyBits bits);
    
    MoonrayObject mMoonrayCamera;

    std::mutex mMutex;
    float mNear = 1;
    float mFar = 10000;
    float mShutterOpen = 0;
    float mShutterClose = 0; 

    // set to aspect ratio of image when camera is selected for rendering
    // the aperture ratio of the camera may be adjusted to match by updateCamera()
    double mDesiredAspectRatio = 0;  
    
    pxr::TfToken mClass; // moonray:class
    mutable bool mXformChanged = true;
    mutable bool mProjChanged = true;
    mutable bool mParamsChanged = true;
};

}

