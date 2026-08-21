// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/imaging/hd/sceneIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>

namespace hdMoonray {

    class HdMoonray_RenderDelegate;

// PrimAccess wraps access to the attributes of a prim, so that
// when SceneIndexes are in use it fetches data from the scene index.
// When SceneIndexes are not in use, it fetches data directly from the scene delegate.
class PrimAccess 
{
public:
    PrimAccess(const pxr::SdfPath& id,
               const pxr::TfToken& schema,
               pxr::HdSceneDelegate* sceneDelegate,
               HdMoonray_RenderDelegate& renderDelegate);

    pxr::VtValue Get(const pxr::TfToken& name) const;

    const pxr::SdfPath& id() const { return mId; }
    pxr::HdSceneDelegate* sceneDelegate() const { return mSceneDelegate; }

private:
    pxr::SdfPath mId;
    pxr::TfToken mSchema;
    pxr::HdSceneDelegate* mSceneDelegate;
    pxr::HdContainerDataSourceHandle mSchemaContainer;
};

pxr::HdContainerDataSourceHandle getPrimContainer(const pxr::SdfPath& id, pxr::HdSceneDelegate* sceneDelegate);

}

