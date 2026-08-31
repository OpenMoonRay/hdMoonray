// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "hydra2_utils.h"

using namespace pxr;

namespace hdMoonray {

PrimAccess::PrimAccess(const SdfPath& id,
                       const TfToken& schema,
                       HdSceneDelegate* sceneDelegate,
                       HdMoonray_RenderDelegate& renderDelegate)
    : mId(id), mSchema(schema), mSceneDelegate(sceneDelegate)
{
    // try to get the schema container from the scene index
    HdRenderIndex& renderIndex = sceneDelegate->GetRenderIndex();
    HdSceneIndexBaseRefPtr sceneIndex = renderIndex.GetTerminalSceneIndex();
    if (sceneIndex) {
        HdSceneIndexPrim siPrim = sceneIndex->GetPrim(id);
        HdContainerDataSourceHandle primDs = siPrim.dataSource;
        if (primDs) {
            mSchemaContainer = HdContainerDataSource::Cast(primDs->Get(schema));
        }
    }
    // if we fail, mSchemaContainer will be null, 
    // and we'll fall back to fetching data directly from the scene delegate
}

VtValue
PrimAccess::Get(const TfToken& name) const
{    
    if (mSchemaContainer) {
        HdDataSourceBaseHandle ds = mSchemaContainer->Get(name);
        if (ds) {
            HdSampledDataSourceHandle sampledDs = HdSampledDataSource::Cast(ds);
            if (sampledDs) {
                return sampledDs->GetValue(0.0f);
            }
        }
    }

    // fall back to fetching data directly from the scene delegate
    std::string fullName = mSchema.GetString() + ":" + name.GetString();
    return mSceneDelegate->Get(mId, TfToken(fullName));
}

pxr::HdContainerDataSourceHandle 
getPrimContainer(const pxr::SdfPath& id, pxr::HdSceneDelegate* sceneDelegate)
{
    HdRenderIndex& renderIndex = sceneDelegate->GetRenderIndex();
    HdSceneIndexBaseRefPtr sceneIndex = renderIndex.GetTerminalSceneIndex();
    if (sceneIndex) {
        HdSceneIndexPrim siPrim = sceneIndex->GetPrim(id);
        return siPrim.dataSource;
    }
    return nullptr;
}

}

