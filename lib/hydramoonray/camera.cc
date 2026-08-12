// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// Camera Sprim, implemented by RDL2 PerspectiveCamera
//
// This is complicted by the fact that earlier versions of Moonray did not like changing
// which camera was used after the first render, so this copies all the perspective
// cameras to a single primary camera. This may be fixed now and can be removed.

#include "camera.h"
#include "MoonrayObject.h"
#include "geometryBase.h"
#include "renderDelegate.h"
#include "ValueConverter.h"
#include "HdmLog.h"
#include "tokens.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include "pxr/base/gf/frustum.h"
#include "pxr/base/gf/camera.h"

#include <iostream>
#include <atomic>

// define this to avoid switching the identity of the perspective camera
// (see HDM-95/HDM-351)
#define DONT_SWITCH_PERSPECTIVE_CAMERA

using namespace pxr;

namespace {
    std::atomic<const hdMoonray::HdMoonray_Camera*> pCamera{nullptr};
}

namespace hdMoonray {

HdDirtyBits
HdMoonray_Camera::GetInitialDirtyBitsMask() const
{
    return DirtyBits::AllDirty;
}

void
HdMoonray_Camera::Sync(HdSceneDelegate* sceneDelegate,
                       HdRenderParam*   renderParam,
                       HdDirtyBits*     dirtyBits)
{
    const SdfPath &id = GetId();
    hdmLogSyncStart("Camera", id, dirtyBits);

    mSceneDelegate = sceneDelegate; // save for use by setAsPrimaryCamera() and RenderPass
    HdMoonray_RenderDelegate& renderDelegate(HdMoonray_RenderDelegate::get(renderParam));

    // Remember the dirty bits, as HdCamera::Sync clears them
    HdDirtyBits bits(*dirtyBits);

    // must call this first, it updates members like GetViewMatrix():
    HdCamera::Sync(sceneDelegate, renderParam, dirtyBits);

    // determine the type of camera (orthographic or perspective or custom RDL)
    TfToken newClass;
    // Use the moonray:class, otherwise guess from the projection matrix (the
    // "projection" attribute is not set by usdview for interactive camera)
    { 
        VtValue v = sceneDelegate->GetCameraParamValue(GetId(), HdMoonrayTokens->moonray_class);
        if (v.IsHolding<TfToken>()) {

            newClass = v.UncheckedGet<TfToken>();

        } else {

            const auto& matrix = ComputeProjectionMatrix();

            if (matrix[2][3] == 0) {
                newClass = HdMoonrayTokens->OrthographicCamera;
            } else {
                newClass = HdMoonrayTokens->PerspectiveCamera;
            }
        }
    }

    // The camera is created lazily
    std::lock_guard<std::mutex> lock(mCreateMutex);
    if (newClass != mClass) {
        mClass = newClass;
        mMoonrayCamera = MoonrayObject();
    } else if (mMoonrayCamera.isValid()) {
        updateCamera(sceneDelegate, renderDelegate, bits);
    }

    hdmLogSyncEnd(id);
}

// create the RDL camera if it doesn't exist, and update to match HdCamera
MoonrayObject
HdMoonray_Camera::createCamera(HdSceneDelegate* sceneDelegate, HdMoonray_RenderDelegate& renderDelegate)
{
    if (mMoonrayCamera.isNull()) {
        std::lock_guard<std::mutex> lock(mCreateMutex);
        if (mMoonrayCamera.isNull()) {
            mMoonrayCamera = renderDelegate.scene().createObject(mClass.GetString(), GetId());
            updateCamera(sceneDelegate, renderDelegate, DirtyBits::AllDirty);
        }
    }
    return mMoonrayCamera;
}

// static function to ensure the camera is created at a given path and update it
MoonrayObject
HdMoonray_Camera::createCamera(HdSceneDelegate* sceneDelegate, HdMoonray_RenderDelegate& renderDelegate, const SdfPath& path)
{
    HdMoonray_Camera* camera =
        dynamic_cast<HdMoonray_Camera*>(sceneDelegate->GetRenderIndex().GetSprim(HdPrimTypeTokens->camera, path));
    if (camera) {
        return camera->createCamera(sceneDelegate, renderDelegate);
    }
    return nullptr;
}

// update the existing RDL camera (mCamera) based on the Hydra values and dirty mask
// mCamera must exist when this is called
void
HdMoonray_Camera::updateCamera(HdSceneDelegate* sceneDelegate, HdMoonray_RenderDelegate& renderDelegate, HdDirtyBits bits)
{
    const SdfPath& id = GetId();
    UpdateGuard guard(renderDelegate, mMoonrayCamera);
    
    VtValue v = sceneDelegate->GetCameraParamValue(id, HdCameraTokens->shutterOpen);
    if (not v.IsEmpty()) {
        mShutterOpen = (v.IsHolding<double>()) ? (v.UncheckedGet<double>()) : v.Get<float>();
    }

    v = sceneDelegate->GetCameraParamValue(id, HdCameraTokens->shutterClose);
    if (not v.IsEmpty()) {
        mShutterClose = (v.IsHolding<double>()) ? (v.UncheckedGet<double>()) : v.Get<float>();
    }

    // update the inverse view matrix (in RDL this is node_xform)
    if (bits & HdCamera::DirtyBits::DirtyTransform) {

        HdTimeSampleArray<GfMatrix4d, 4> sampledXforms;
        sceneDelegate->SampleTransform(id, mShutterOpen, mShutterClose, &sampledXforms);
        if (sampledXforms.count <= 1) {
            // if there's only one sample, it should match the cached value
            mMoonrayCamera.set("node_xform", GetTransform());
        } else {            
            // first and last samples will be sample interval boundaries
            GfCamera cam(sampledXforms.values[0]);
            GfFrustum frustum = cam.GetFrustum();
            GfMatrix4d viewInverse0 = frustum.ComputeViewMatrix().GetInverse();
            cam.SetTransform(sampledXforms.values[sampledXforms.count-1]);
            frustum = cam.GetFrustum();
            GfMatrix4d viewInverse1 = frustum.ComputeViewMatrix().GetInverse();
            mMoonrayCamera.set("node_xform", viewInverse0, viewInverse1);
        }
        mXformChanged = true;
    }

    // update the projection parameters.
    if ((bits & HdCamera::DirtyBits::DirtyParams) &&
        (mClass == HdMoonrayTokens->PerspectiveCamera || 
         mClass == HdMoonrayTokens->OrthographicCamera)) {
        
        // These are in mm in USD and RDL, but Hydra uses cm, so we need to multiply by 10
        float apertureWidth  = 10*GetHorizontalAperture();
        float apertureHeight = 10*GetVerticalAperture();
        float focalLength    = 10*GetFocalLength();
        float horizOffset    = 10*GetHorizontalApertureOffset();
        float vertOffset     = 10*GetVerticalApertureOffset();
   
        mNear = GetClippingRange().GetMin();
        mFar  = GetClippingRange().GetMax();

        // apertureWidth/apertureHeight gives us the camera aspect ratio.
        // if necessary, we adjust this based on the requested image aspect ratio (mDesiredAspectRatio)
        // and the window policy. Note that the RDL camera only specifies apertureWidth, so
        // it's not clear that all conform options will work
        GfVec2d adjustedAperture(apertureWidth, apertureHeight);
        if (mDesiredAspectRatio != 0.0) {
            adjustedAperture = CameraUtilConformedWindow(adjustedAperture, GetWindowPolicy(), mDesiredAspectRatio);
        }

        // according to MOONRAY-4278, vertical offset needs to be adjusted
        double adjustedAr = adjustedAperture[0]/apertureHeight;
        vertOffset *= adjustedAr;

        mMoonrayCamera.set("film_width_aperture", (float)adjustedAperture[0]);
        mMoonrayCamera.set("horizontal_film_offset", horizOffset);
        mMoonrayCamera.set("vertical_film_offset", vertOffset);
        mMoonrayCamera.set("near", mNear);
        mMoonrayCamera.set("far", mFar);

        if (mClass == HdMoonrayTokens->PerspectiveCamera) {
            mMoonrayCamera.set("focal", focalLength);
        }

        mProjChanged = true;
    }

    // handles all other params
    if (bits & (DirtyBits::DirtyParams | HdCamera::DirtyBits::DirtyParams)) {
        
        VtValue v;

        // motion params
        if (mClass == HdMoonrayTokens->PerspectiveCamera || 
            mClass == HdMoonrayTokens->OrthographicCamera) {
            const float fStop = GetFStop();
            mMoonrayCamera.set("dof", fStop != 0.0);
            if (fStop) {
                mMoonrayCamera.set("dof_aperture", fStop); // Rdl2 "dof_aperture" is actually fStop ratio
                const float focusDistance = GetFocusDistance();
                mMoonrayCamera.set("dof_focus_distance", focusDistance);
            }
        }

        mMoonrayCamera.set("mb_shutter_open", mShutterOpen);
        mMoonrayCamera.set("mb_shutter_close", mShutterClose);

        // overwrite any attributes with moonray:xyz attributes

        for (auto it = mMoonrayCamera.beginAttributes(); it != mMoonrayCamera.endAttributes(); ++it) {
            const std::string& attrName = (*it).name();
            VtValue val = sceneDelegate->GetCameraParamValue(id, TfToken("moonray:"+attrName));
            if (!val.IsEmpty()) {
                (*it).set(val);
            }
            // TO DO: we should revert any attributes that are no longer set, but we don't have a way to know which ones those are
        }
        mParamsChanged = true;
    }

    // DirtyBits::DirtyWindowPolicy)
    //      The value of GetWindowPolicy() was already updated by HdCamera::Sync()
    // DirtyBits::DirtyClipPlanes) 
    //      arbitrary clipping planes, not supported by Moonray
}

// Set this camera as the camera to use when rendering
//
// This code actually maintains a single RDL perspective camera ("primaryCamera"), and sets this camera as
// active by copying the parameters into "primaryCamera". The original motivation was that changing the
// scene camera identity interactively doesn't work (see HDM-95). This is now addressed, but there are
// some open questions about performance and accuracy of these two approaches (see HDM-351)
// 
// aspectRatio is the shape of the image. The camera's window policy (GetWindowPolicy)
// will be used to adjust the camera as needed to match.
void
HdMoonray_Camera::setAsPrimaryCamera(HdMoonray_RenderDelegate& renderDelegate, double aspectRatio)
{
    if (aspectRatio != mDesiredAspectRatio) {
        mDesiredAspectRatio = aspectRatio;
        if (mMoonrayCamera.isValid()) {
            // will adjusr camera to match mDesiredAspectRatio
            updateCamera(mSceneDelegate, renderDelegate, HdCamera::DirtyBits::DirtyParams);
        }
    }
    createCamera(mSceneDelegate, renderDelegate);

    MoonrayObject cameraToUse;

#ifdef DONT_SWITCH_PERSPECTIVE_CAMERA // see HDM-351
    if (mClass == HdMoonrayTokens->PerspectiveCamera) {
        // update single perspective camera in-place
        if (this == pCamera) {
            if (not mXformChanged && not mProjChanged && not mParamsChanged) return;
        } else {
            pCamera = this;
        }
        MoonrayObject primary = cameraToUse = renderDelegate.scene().primaryCamera();
        UpdateGuard guard(renderDelegate, primary);
        primary.copyFrom(mMoonrayCamera);
        mXformChanged = false;
        mProjChanged = false;
        mParamsChanged = false;
    } else

#endif // DONT_SWITCH_PERSPECTIVE_CAMERA
    {
        if (this == pCamera) return;
        pCamera = this;
        cameraToUse = mMoonrayCamera.sceneObjectAs<rdl2::Camera>();
    }
    rdl2::SceneVariables& wsv(renderDelegate.acquireSceneContext().getSceneVariables());
    UpdateGuard guard(wsv);
    wsv.set(wsv.sCamera, cameraToUse.sceneObjectAs<rdl2::Camera>());
}

void
HdMoonray_Camera::Finalize(HdRenderParam *renderParam)
{
    if (this == pCamera) pCamera = nullptr;
    HdCamera::Finalize(renderParam);
}

HdMoonray_Camera::~HdMoonray_Camera()
{
}

}
