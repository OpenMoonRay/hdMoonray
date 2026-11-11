// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "dataSourceMeshLight.h"
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/usdImaging/usdImaging/dataSourceAttribute.h>
#include <pxr/usdImaging/usdImaging/dataSourceRelationship.h>

#include <iostream>
PXR_NAMESPACE_OPEN_SCOPE

namespace {
    const TfToken moonrayToken("moonray");
}

MoonrayDataSourceMeshLight::MoonrayDataSourceMeshLight(
        const SdfPath &sceneIndexPath,
        UsdPrim usdPrim,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
    : _sceneIndexPath(sceneIndexPath)
    , _usdPrim(usdPrim)
    , _stageGlobals(stageGlobals)
{
}

TfTokenVector
MoonrayDataSourceMeshLight::GetNames()
{
    TfTokenVector result;
    std::vector<UsdProperty> properties = _usdPrim.GetProperties();
    for (const UsdProperty& prop : properties) {
        if (prop.GetNamespace() == moonrayToken) {
            result.push_back(prop.GetBaseName());
        }
    }
    return result;
}

HdDataSourceBaseHandle
MoonrayDataSourceMeshLight::Get(const TfToken &name)
{
    std::string attrName = moonrayToken.GetString() + ":" + name.GetString();
    UsdProperty prop = _usdPrim.GetProperty(TfToken(attrName)); 
    if (UsdRelationship rel = prop.As<UsdRelationship>()) {
        return UsdImagingDataSourceRelationship::New(rel, _stageGlobals);
    } 
    if (UsdAttribute attr = prop.As<UsdAttribute>()) {
        return UsdImagingDataSourceAttributeNew(attr, _stageGlobals);
    }
    return nullptr;
}

// ----------------------------------------------------------------------------


MoonrayDataSourceMeshLightPrim::MoonrayDataSourceMeshLightPrim(
        const SdfPath &sceneIndexPath,
        UsdPrim usdPrim,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
    : UsdImagingDataSourcePrim(sceneIndexPath, usdPrim, stageGlobals)
{
}

TfTokenVector 
MoonrayDataSourceMeshLightPrim::GetNames()
{
    TfTokenVector result = UsdImagingDataSourcePrim::GetNames();
    result.push_back(moonrayToken);

    return result;
}

HdDataSourceBaseHandle 
MoonrayDataSourceMeshLightPrim::Get(const TfToken & name)
{
    if (name == moonrayToken) {
        return MoonrayDataSourceMeshLight::New(
            _GetSceneIndexPath(),
            _GetUsdPrim(),
            _GetStageGlobals());
    } else {
        return UsdImagingDataSourcePrim::Get(name);
    }
}

/*static*/
HdDataSourceLocatorSet
MoonrayDataSourceMeshLightPrim::Invalidate(
    UsdPrim const& prim,
    const TfToken &subprim,
    const TfTokenVector &properties,
    const UsdImagingPropertyInvalidationType invalidationType)
{
    static const HdDataSourceLocator procLocator(moonrayToken);
    
    HdDataSourceLocatorSet locators;

    for (const TfToken &propertyName : properties) {
        if (propertyName.GetString().find(moonrayToken.GetString() + ":") == 0) {
            TfToken name(propertyName.GetString().substr(moonrayToken.GetString().size() + 1));
            locators.insert(procLocator.Append(name));
        }
    }

    // Give base classes a chance to invalidate.
    locators.insert(
        UsdImagingDataSourcePrim::Invalidate(
            prim, subprim, properties, invalidationType));
    return locators;
}

PXR_NAMESPACE_CLOSE_SCOPE
