// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "pxr/usdImaging/usdImaging/dataSourcePrim.h"
#include "pxr/usdImaging/usdImaging/dataSourceStageGlobals.h"

#include "pxr/imaging/hd/dataSource.h"

PXR_NAMESPACE_OPEN_SCOPE

class MoonrayDataSourceLightFilter : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(MoonrayDataSourceLightFilter);

    TfTokenVector GetNames() override;
    HdDataSourceBaseHandle Get(const TfToken &name) override;

private:
    MoonrayDataSourceLightFilter(
            const SdfPath &sceneIndexPath,
            UsdPrim usdPrim,
            const UsdImagingDataSourceStageGlobals &stageGlobals);

private:
    const SdfPath _sceneIndexPath;
    UsdPrim _usdPrim;
    const UsdImagingDataSourceStageGlobals & _stageGlobals;
};

HD_DECLARE_DATASOURCE_HANDLES(MoonrayDataSourceLightFilter);

// ----------------------------------------------------------------------------


class MoonrayDataSourceLightFilterPrim : public UsdImagingDataSourcePrim
{
public:
    HD_DECLARE_DATASOURCE(MoonrayDataSourceLightFilterPrim);

    TfTokenVector GetNames() override;
    HdDataSourceBaseHandle Get(const TfToken &name) override;

    USDIMAGING_API
    static HdDataSourceLocatorSet Invalidate(
            UsdPrim const& prim,
            const TfToken &subprim,
            const TfTokenVector &properties,
            UsdImagingPropertyInvalidationType invalidationType);

private:
    MoonrayDataSourceLightFilterPrim(
        const SdfPath &sceneIndexPath,
        UsdPrim usdPrim,
        const UsdImagingDataSourceStageGlobals &stageGlobals);

};

HD_DECLARE_DATASOURCE_HANDLES(MoonrayDataSourceLightFilterPrim);

PXR_NAMESPACE_CLOSE_SCOPE
