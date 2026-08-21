// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "pxr/usdImaging/usdImaging/dataSourcePrim.h"
#include "pxr/usdImaging/usdImaging/dataSourceStageGlobals.h"


#include "pxr/imaging/hd/dataSource.h"

PXR_NAMESPACE_OPEN_SCOPE

class MoonrayDataSourceMeshLight : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(MoonrayDataSourceMeshLight);

    TfTokenVector GetNames() override;
    HdDataSourceBaseHandle Get(const TfToken &name) override;

private:
    MoonrayDataSourceMeshLight(
            const SdfPath &sceneIndexPath,
            UsdPrim usdPrim,
            const UsdImagingDataSourceStageGlobals &stageGlobals);

private:
    const SdfPath _sceneIndexPath;
    UsdPrim _usdPrim;
    const UsdImagingDataSourceStageGlobals & _stageGlobals;
};

HD_DECLARE_DATASOURCE_HANDLES(MoonrayDataSourceMeshLight);

// ----------------------------------------------------------------------------


class MoonrayDataSourceMeshLightPrim : public UsdImagingDataSourcePrim
{
public:
    HD_DECLARE_DATASOURCE(MoonrayDataSourceMeshLightPrim);

    TfTokenVector GetNames() override;
    HdDataSourceBaseHandle Get(const TfToken &name) override;

    USDIMAGING_API
    static HdDataSourceLocatorSet Invalidate(
            UsdPrim const& prim,
            const TfToken &subprim,
            const TfTokenVector &properties,
            UsdImagingPropertyInvalidationType invalidationType);

private:
    MoonrayDataSourceMeshLightPrim(
        const SdfPath &sceneIndexPath,
        UsdPrim usdPrim,
        const UsdImagingDataSourceStageGlobals &stageGlobals);

};

HD_DECLARE_DATASOURCE_HANDLES(MoonrayDataSourceMeshLightPrim);

PXR_NAMESPACE_CLOSE_SCOPE
