// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// Points are implemented using RdlPointGeometry
// Very similar to (but simpler than) BasisCurves

#include "points.h"
#include "renderDelegate.h"
#include "HdmLog.h"
#include "tokens.h"

#include <pxr/base/gf/vec2f.h>

#include <iostream>

using namespace pxr;

namespace {

// constants for RDL attrs and enums used herein
const std::string rdlClassPoints("RdlPointGeometry");
const std::string rdlAttrRadiusList("radius_list");

}

namespace hdMoonray {

HdMoonray_Points::HdMoonray_Points(SdfPath const& id):
    HdPoints(id), HdMoonray_GeometryBase(this) 
{}

void
HdMoonray_Points::Finalize(HdRenderParam* renderParam)
{
    // causes geometry to be hidden
    resetGeometryObject(HdMoonray_RenderDelegate::get(renderParam));
}

HdDirtyBits
HdMoonray_Points::GetInitialDirtyBitsMask() const
{
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::DirtyPoints
        | HdChangeTracker::DirtyPrimID
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyMaterialId
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyWidths
        | HdChangeTracker::DirtyDoubleSided
        | HdChangeTracker::DirtyInstancer
        | HdChangeTracker::DirtyInstanceIndex
        | HdChangeTracker::DirtyCategories;
    return (HdDirtyBits)mask;
}

void
HdMoonray_Points::primvarChanged(HdSceneDelegate *sceneDelegate, HdMoonray_RenderDelegate& renderDelegate,
                                 const TfToken& name,  const VtValue& value,
                                 const HdInterpolation& interp, const TfToken& role)
{
    // Handles primvars that drive RDL attributes, rather than creating UserData
    // RdlPointGeometry directly supports primvar "widths" via
    // object attribute "radius_list"
    if (name ==  HdTokens->widths) {
        if (value.IsEmpty()) {
            mGeometry.setToDefault(rdlAttrRadiusList);
        } else if (value.IsHolding<VtFloatArray>()) {
            const VtFloatArray& v = value.UncheckedGet<VtFloatArray>();
            VtFloatArray w(v.begin(), v.end());
            // width is diameter : convert to radius
            for (float& r : w) r /= 2;
            mGeometry.set(rdlAttrRadiusList, w);
        }
    } else {
        // allow HdMoonray_GeometryBase to handle all other cases
        HdMoonray_GeometryBase::primvarChanged(sceneDelegate, renderDelegate,
                                      name, value, interp, role);
    }
}

void
HdMoonray_Points::Sync(HdSceneDelegate *sceneDelegate,
                       HdRenderParam   *renderParam,
                       HdDirtyBits     *dirtyBits,
                       TfToken const   &reprToken)
{
    hdmLogSyncStart("Points", GetId(), dirtyBits);   
    HdMoonray_RenderDelegate& renderDelegate(HdMoonray_RenderDelegate::get(renderParam));

    _UpdateVisibility(sceneDelegate, dirtyBits);
    _UpdateInstancer(sceneDelegate, dirtyBits);
    
    syncAll(rdlClassPoints, sceneDelegate, renderDelegate, dirtyBits, reprToken);
    
    hdmLogSyncEnd(GetId());
}

}
