// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MoonrayObject.h"

#include <pxr/imaging/hd/light.h>


namespace hdMoonray {

class HdMoonray_RenderDelegate;

class HdMoonray_Light: public pxr::HdLight
{
public:
    HdMoonray_Light(const pxr::TfToken& type, const pxr::SdfPath& id):
        pxr::HdLight(id), mType(type), mRectToSpotlight(false) 
        {}

    /// Dirty bits to pass to first call to Sync()
    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    /// Update the data identified by dirtyBits. Must not query other data.
    void Sync(pxr::HdSceneDelegate* sceneDelegate,
              pxr::HdRenderParam*   renderParam,
              pxr::HdDirtyBits*     dirtyBits) override;

    const pxr::TfToken& type() const { return mType; }

    static bool isSupportedType(const pxr::TfToken& token);

    void Finalize(pxr::HdRenderParam *renderParam) override;

    // required to implement material->light linking (Accent material)
    static MoonrayObject lightForMaterialLink(pxr::HdSceneDelegate *sceneDelegate, const pxr::SdfPath& path);

private:
    const std::string& rdlClassName(const pxr::SdfPath& id,
                                    pxr::HdSceneDelegate *sceneDelegate,
                                    HdMoonray_RenderDelegate& renderDelegate);

    void syncXform(const pxr::SdfPath& id,
                   pxr::HdSceneDelegate *sceneDelegate,
                   HdMoonray_RenderDelegate& renderDelegate);
    void syncParams(const pxr::SdfPath& id,
                    pxr::HdSceneDelegate *sceneDelegate,
                    HdMoonray_RenderDelegate& renderDelegate);
    void syncFilterList(const pxr::SdfPath& id,
                        pxr::HdSceneDelegate *sceneDelegate,
                        HdMoonray_RenderDelegate& renderDelegate);

    void fixLightXform(pxr::GfMatrix4d& mat);

    pxr::TfToken mType;

    // Holds whether this light was converted from a rect light to a spot light
    bool mRectToSpotlight;

    MoonrayObject mMoonrayLight;

    bool mOn = false;
    void setOn(bool, HdMoonray_RenderDelegate& renderDelegate);
    void resetLightObject(HdMoonray_RenderDelegate& renderDelegate);

    // the name of a category holding all geometry lit by this light
    pxr::TfToken mLightLinkCategory;
    pxr::TfToken mShadowLinkCategory;
};

}

