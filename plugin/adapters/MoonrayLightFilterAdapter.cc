// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayLightFilterAdapter.h"
#include "dataSourceLightFilter.h"

#include <pxr/usdImaging/usdImaging/lightAdapter.h>
#include <pxr/usdImaging/usdImaging/delegate.h>
#include <pxr/usdImaging/usdImaging/indexProxy.h>
#include <pxr/usdImaging/usdImaging/dataSourceMaterial.h>
#include <pxr/imaging/hd/overlayContainerDataSource.h>
#include "pxr/imaging/hd/retainedDataSource.h"

#include <pxr/imaging/hd/tokens.h>



PXR_NAMESPACE_OPEN_SCOPE

namespace {
    // Note: currently MoonrayLightFilter linking fails if the Hydra type is not
    // "lightFilter". I think because the light linking code is expecting that type...
    TfToken moonrayLightFilterToken("lightFilter");
    // TfToken moonrayLightFilterToken("moonrayLightFilter");
    TfToken cookie_projector_token("moonray:projector");
    TfToken combine_filters_token("moonray:light_filters");
}

TF_REGISTRY_FUNCTION(TfType)
{
    typedef MoonrayLightFilterAdapter Adapter;
    TfType t = TfType::Define<Adapter, TfType::Bases<Adapter::BaseAdapter> >();
    t.SetFactory< UsdImagingPrimAdapterFactory<Adapter> >();
}

MoonrayLightFilterAdapter::~MoonrayLightFilterAdapter()
{
}

TfTokenVector
MoonrayLightFilterAdapter::GetImagingSubprims(UsdPrim const& prim)
{
    return { TfToken() };
}

TfToken
MoonrayLightFilterAdapter::GetImagingSubprimType(
        UsdPrim const& prim,
        TfToken const& subprim)
{
    if (subprim.IsEmpty()) {
        return moonrayLightFilterToken;
    }
    return TfToken();
}

HdContainerDataSourceHandle
MoonrayLightFilterAdapter::GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
{
    if (!subprim.IsEmpty()) {
        return nullptr;
    }

    return HdOverlayContainerDataSource::New(
        HdRetainedContainerDataSource::New(
            HdPrimTypeTokens->material,
            UsdImagingDataSourceMaterial::New(
                prim,
                stageGlobals,
                HdMaterialTerminalTokens->lightFilter)
            ),
            MoonrayDataSourceLightFilterPrim::New(
                prim.GetPath(),
                prim,
                stageGlobals)
        );
}

HdDataSourceLocatorSet
MoonrayLightFilterAdapter::InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        const UsdImagingPropertyInvalidationType invalidationType)
{
    if (subprim.IsEmpty()) {
        return MoonrayDataSourceLightFilterPrim::Invalidate(
            prim, subprim, properties, invalidationType);
    }
    
    return HdDataSourceLocatorSet();
}

bool
MoonrayLightFilterAdapter::IsSupported(UsdImagingIndexProxy const* index) const
{
    return index->IsRprimTypeSupported(moonrayLightFilterToken);
}

VtValue
MoonrayLightFilterAdapter::Get(
    UsdPrim const& prim,
    SdfPath const& cachePath,
    TfToken const &key,
    UsdTimeCode time, 
    VtIntArray* outIndices) const
{
    // we need to provide access to "rel" properties on the prim, since
    // the standard "Get" doesn't see them
    if (key == cookie_projector_token) {
        UsdProperty prop = prim.GetProperty(key);
        if (UsdRelationship rel = prop.As<UsdRelationship>()) {
            SdfPathVector targets;
            if (rel.GetForwardedTargets(&targets) && !targets.empty()) {
                return VtValue(targets.front());
            }
        }
    } else if (key == combine_filters_token) {
        UsdProperty prop = prim.GetProperty(key);
        if (UsdRelationship rel = prop.As<UsdRelationship>()) {
            SdfPathVector targets;
            if (rel.GetForwardedTargets(&targets) && !targets.empty()) {
                return VtValue(targets);
            }
        }
    }
    return BaseAdapter::Get(prim, cachePath, key, time, outIndices);
}

SdfPath
MoonrayLightFilterAdapter::Populate(UsdPrim const& prim,
                            UsdImagingIndexProxy* index,
                            UsdImagingInstancerContext const* instancerContext)
{
    index->InsertSprim(moonrayLightFilterToken, prim.GetPath(), prim);

    return prim.GetPath();
}

void
MoonrayLightFilterAdapter::_RemovePrim(SdfPath const& cachePath,
                                          UsdImagingIndexProxy* index)
{
    index->RemoveSprim(moonrayLightFilterToken, cachePath);
}

PXR_NAMESPACE_CLOSE_SCOPE
