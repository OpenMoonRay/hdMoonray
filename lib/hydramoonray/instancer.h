// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MoonrayObject.h"

#include <pxr/imaging/hd/instancer.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/tf/hashmap.h>
#include <pxr/imaging/hd/vtBufferSource.h>

#include "geometryBase.h"
#include "MurmurHash3.h"

#include <mutex>

namespace hdMoonray {

class HdMoonray_Instancer: public pxr::HdInstancer
{
public:
    HdMoonray_Instancer(pxr::HdSceneDelegate* delegate,
              const pxr::SdfPath& id);
    ~HdMoonray_Instancer();

    // Hydra instancers are stored "backwards", the prototype knows what instance it
    // is in. When the prototype generates rdla_scene objects it calls this to
    // generate an instancer scene object. The id and prototypeId are used to look up
    // all the rest of the information. The instancer pointer is set or the object
    // already there is updated. There do not appear to be any other sync calls to
    // the instancer, so it will have to update transforms and primvars for this
    // prototype in this call.
    void makeInstanceGeometry(const pxr::SdfPath& prototypeId,
                              const std::string& protoSuffix,
                              MoonrayObject prototype, 
                              HdMoonray_GeometryBase* hdGeometry,
                              size_t level,
                              size_t childCount = 1);

    void makeInstanceLights(const pxr::SdfPath& prototypeId,
                            MoonrayObject prototype,
                            size_t level, size_t childCount = 1);

    void Sync(pxr::HdSceneDelegate *sceneDelegate,
              pxr::HdRenderParam   *renderParam,
              pxr::HdDirtyBits     *dirtyBits) override;

private:
    void applyPrimvarOverrides(MoonrayObject obj, int index);
    
    struct PrimvarInfo { pxr::VtValue value; pxr::TfToken role; };
    std::map<pxr::TfToken, PrimvarInfo> mPrimvars;
    pxr::GfMatrix4d mXform;

    // map from prototype object to the instancer created for it
    std::map<MoonrayObject, MoonrayObject> mInstancers;
    using LightInstances = std::vector<MoonrayObject>;
    std::map<MoonrayObject, LightInstances> mLightInstances;
    std::mutex mMapMutex;
};

}
