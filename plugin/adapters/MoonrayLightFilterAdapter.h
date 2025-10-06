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

    virtual VtValue Get(UsdPrim const& prim,
                        SdfPath const& cachePath,
                        TfToken const& key,
                        UsdTimeCode time, 
                        VtIntArray* outIndices) const;

};

PXR_NAMESPACE_CLOSE_SCOPE
