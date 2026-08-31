// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "dataSourceProcedural.h"
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/usdImaging/usdImaging/dataSourceAttribute.h>
#include <pxr/usdImaging/usdImaging/dataSourceRelationship.h>

#include <iostream>
PXR_NAMESPACE_OPEN_SCOPE

static TfToken proceduralToken("procedural");

MoonrayDataSourceProcedural::MoonrayDataSourceProcedural(
        const SdfPath &sceneIndexPath,
        UsdPrim usdPrim,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
    : _sceneIndexPath(sceneIndexPath)
    , _usdPrim(usdPrim)
    , _stageGlobals(stageGlobals)
{
}

TfTokenVector
MoonrayDataSourceProcedural::GetNames()
{   
    TfTokenVector result;
    std::vector<UsdProperty> properties = _usdPrim.GetProperties();
    for (const UsdProperty& prop : properties) {
        if (prop.GetNamespace() == proceduralToken) {
            result.push_back(prop.GetBaseName());
        }
    }
    return result;
}

HdDataSourceBaseHandle
MoonrayDataSourceProcedural::Get(const TfToken &name)
{   std::string attrName = proceduralToken.GetString() + ":" + name.GetString();
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


MoonrayDataSourceProceduralPrim::MoonrayDataSourceProceduralPrim(
        const SdfPath &sceneIndexPath,
        UsdPrim usdPrim,
        const UsdImagingDataSourceStageGlobals &stageGlobals)
    : UsdImagingDataSourceGprim(sceneIndexPath, usdPrim, stageGlobals)
{
}

TfTokenVector 
MoonrayDataSourceProceduralPrim::GetNames()
{
    TfTokenVector result = UsdImagingDataSourceGprim::GetNames();
    result.push_back(proceduralToken);

    return result;
}

HdDataSourceBaseHandle 
MoonrayDataSourceProceduralPrim::Get(const TfToken & name)
{
    if (name == proceduralToken) {
        return MoonrayDataSourceProcedural::New(
            _GetSceneIndexPath(),
            _GetUsdPrim(),
            _GetStageGlobals());
    } else {
        return UsdImagingDataSourceGprim::Get(name);
    }
}

/*static*/
HdDataSourceLocatorSet
MoonrayDataSourceProceduralPrim::Invalidate(
    UsdPrim const& prim,
    const TfToken &subprim,
    const TfTokenVector &properties,
    const UsdImagingPropertyInvalidationType invalidationType)
{
    static const HdDataSourceLocator procLocator(proceduralToken);
    
    HdDataSourceLocatorSet locators;

    for (const TfToken &propertyName : properties) {
        if (propertyName.GetString().find(proceduralToken.GetString() + ":") == 0) {
            TfToken name(propertyName.GetString().substr(proceduralToken.GetString().size() + 1));
            locators.insert(procLocator.Append(name));
        }
    }

    // Give base classes a chance to invalidate.
    locators.insert(
        UsdImagingDataSourceGprim::Invalidate(
            prim, subprim, properties, invalidationType));
    return locators;
}

PXR_NAMESPACE_CLOSE_SCOPE
