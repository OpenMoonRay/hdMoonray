// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "pxr/pxr.h"

#include "pxr/usdImaging/usdImaging/lightFilterAdapter.h"

#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE


class UsdPrim;

class MoonrayLightFilterAdapter : public UsdImagingLightFilterAdapter {
public:
    typedef UsdImagingLightFilterAdapter BaseAdapter;

    MoonrayLightFilterAdapter()
        : UsdImagingLightFilterAdapter()
    {
    }

    virtual ~MoonrayLightFilterAdapter();

    USDIMAGING_API
    TfTokenVector GetImagingSubprims(UsdPrim const& prim) override;

    USDIMAGING_API
    TfToken GetImagingSubprimType(
        UsdPrim const& prim,
        TfToken const& subprim) override;

    USDIMAGING_API
    HdContainerDataSourceHandle GetImagingSubprimData(
        UsdPrim const& prim,
        TfToken const& subprim,
        const UsdImagingDataSourceStageGlobals &stageGlobals) override;

    USDIMAGING_API
    HdDataSourceLocatorSet InvalidateImagingSubprim(
        UsdPrim const& prim,
        TfToken const& subprim,
        TfTokenVector const& properties,
        UsdImagingPropertyInvalidationType invalidationType) override;

    virtual VtValue Get(UsdPrim const& prim,
                        SdfPath const& cachePath,
                        TfToken const& key,
                        UsdTimeCode time, 
                        VtIntArray* outIndices) const;
    
    bool IsSupported(UsdImagingIndexProxy const*) const override;
    
    SdfPath Populate(UsdPrim const& prim,
                            UsdImagingIndexProxy* index,
                            UsdImagingInstancerContext const* instancerContext) override;
                            
    void _RemovePrim(SdfPath const& cachePath,
                    UsdImagingIndexProxy* index) override;

};

PXR_NAMESPACE_CLOSE_SCOPE
