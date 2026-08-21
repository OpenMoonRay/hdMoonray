// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/pxr.h>
#include <pxr/base/tf/staticTokens.h>

PXR_NAMESPACE_OPEN_SCOPE

#define HDM_API
#define HDM_TOKENS \
    ((moonray_class,"moonray:class")) \
    (PerspectiveCamera) \
    (OrthographicCamera) \
    ((moonray_side_type,"moonray:side_type")) \
    (SpotLight) \
    (RectLight) \
    (geometryLight) \
    (geometry) \
    (moonray) \
    ((_class, "class")) \
    (projector) \
    (light_filters) \
    (categoryUnset) \
    (procedural) \
    (parts) \
    (reload_textures) \
    (restart_arras) \
    (output_rdl) \
    (openvdbAsset) \
    (density) \
    (velocity) \
    (fieldName) \
    (fieldIndex) \
    (st) \
    (uv) \
    (normal) \
    (N) \
    ((moonray_curves_subtype,"moonray:curves_subtype")) \
    ((moonray_tessellation_rate,"moonray:tessellation_rate")) \
    (primvars) \
    (lpe) \
    (shader) \
    (raw) \
    (cryptomatte) \
    ((_float, "float")) \
    ((_float2, "float2")) \
    ((_float3, "float3")) \
    ((_float4, "float4")) \
    ((_int, "int")) \
    ((_color3, "color3")) \
    ((_color4, "color4"))

TF_DECLARE_PUBLIC_TOKENS(HdMoonrayTokens, HDM_API, HDM_TOKENS);

#undef HDM_API

PXR_NAMESPACE_CLOSE_SCOPE
