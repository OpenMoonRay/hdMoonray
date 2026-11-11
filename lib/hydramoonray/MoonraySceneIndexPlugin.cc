// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "MoonraySceneIndexPlugin.h"
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hd/dependencyForwardingSceneIndex.h>
#include <pxr/imaging/hdsi/implicitSurfaceSceneIndex.h>
#include <pxr/imaging/hdsi/lightLinkingSceneIndex.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((sceneIndexPluginName, "MoonraySceneIndexPlugin"))
);

TF_REGISTRY_FUNCTION_WITH_TAG(TfType, MoonraySceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::Define<MoonraySceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION_WITH_TAG(HdSceneIndexPlugin, MoonraySceneIndexPlugin)
{
    const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

    // convert all implicit geometry to meshes
    HdDataSourceBaseHandle const toMesh =
        HdRetainedTypedSampledDataSource<TfToken>::New(
            HdsiImplicitSurfaceSceneIndexTokens->toMesh);

    HdContainerDataSourceHandle const inputArgs =
        HdRetainedContainerDataSource::New(
            HdPrimTypeTokens->sphere, toMesh,
            HdPrimTypeTokens->cube, toMesh,
            HdPrimTypeTokens->cone, toMesh,
            HdPrimTypeTokens->cylinder, toMesh,
            HdPrimTypeTokens->capsule, toMesh,
            HdPrimTypeTokens->plane, toMesh);

    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
        "Moonray",
        _tokens->sceneIndexPluginName,
        inputArgs,
        insertionPhase,
        HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdSceneIndexBaseRefPtr
MoonraySceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene,
    const HdContainerDataSourceHandle &inputArgs)
{
    return HdDependencyForwardingSceneIndex::New(
            HdsiLightLinkingSceneIndex::New(
                    HdsiImplicitSurfaceSceneIndex::New(inputScene, inputArgs),
                HdContainerDataSourceHandle()));
}

PXR_NAMESPACE_CLOSE_SCOPE