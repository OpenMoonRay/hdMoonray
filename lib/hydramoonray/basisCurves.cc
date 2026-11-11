// Copyright 2023-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// HdMoonray_BasisCurves are implemented using RdlCurveGeometry
// The following features are supported:
//
// - linear, bezier and bspline bases (NOT Catmull Rom)
// - nonperiodic only, periodic curves are NOT supported
// - non-indexed verts only, indexed verts are NOT supported
//
// Tessellation rate and roundness are set based on the refineLevel
// display style ("complexity" in usd_view). As with other RDL geometry
// attributes, these can be overridden by a primvar.

#include "basisCurves.h"
#include "renderDelegate.h"
#include "HdmLog.h"
#include "tokens.h"

#include <pxr/base/gf/vec2f.h>

using namespace pxr;
namespace {

// round curves are more expensive than rayFacing, so are not generated
// at this refine level and below
constexpr int REFINE_LEVEL_FLAT_CURVES = 2;
// tessellation rate used at refine level 0
constexpr int TESS_RATE_LOW = 1;
// tessellation rate used at refine level greater than 0
constexpr int TESS_RATE_NORMAL = 4;

// constants for RDL attrs and enums used herein
const std::string rdlClassCurves("RdlCurveGeometry");
const std::string rdlAttrCurveType("curve_type");
constexpr int rdlCurveType_linear = 0;
constexpr int rdlCurveType_bezier = 1;
constexpr int rdlCurveType_bspline = 2;
const std::string rdlAttrCurvesSubtype("curves_subtype");
constexpr int rdlCurvesSubtype_rayFacing = 0;
constexpr int rdlCurvesSubtype_round = 1;
// subtype normalOriented is not used
const std::string rdlAttrCurveVertexCounts("curves_vertex_count");
const std::string rdlAttrTessellationRate("tessellation_rate");
const std::string rdlAttrReverseNormals("reverse_normals");
const std::string rdlAttrUvList("uv_list");
const std::string rdlAttrRadiusList("radius_list");

}

namespace hdMoonray {

HdMoonray_BasisCurves::HdMoonray_BasisCurves(SdfPath const& id) :
    HdBasisCurves(id), HdMoonray_GeometryBase(this) 
{}

void
HdMoonray_BasisCurves::Finalize(HdRenderParam* renderParam)
{
    // causes geometry to be hidden
    resetGeometryObject(HdMoonray_RenderDelegate::get(renderParam));
}

HdDirtyBits
HdMoonray_BasisCurves::GetInitialDirtyBitsMask() const
{
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::DirtyPrimID
        | HdChangeTracker::DirtyDisplayStyle
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyMaterialId
        | HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyDoubleSided
        | HdChangeTracker::DirtySubdivTags
        | HdChangeTracker::DirtyWidths
        | HdChangeTracker::DirtyInstancer
        | HdChangeTracker::DirtyInstanceIndex
        | HdChangeTracker::DirtyCategories;
    return (HdDirtyBits)mask;
}

void
HdMoonray_BasisCurves::syncTopology(const HdBasisCurvesTopology& topology)
{
    if (topology.HasIndices()) {
        Logger::error(GetId(), ": curve indices are not supported");
        return;
    }

    if (topology.GetCurveWrap() != HdTokens->nonperiodic) {
        Logger::error(GetId(), ": unsupported curve wrap '", topology.GetCurveWrap(), "'");
        return;
    }

    // note Hydra has separate type (linear, cubic) and basis (bezier, bSpline, catmullRom)
    // where basis is ignored for linear type. RDL has a single type enum (linear, bezier, bspline)
    const TfToken curveType(topology.GetCurveType());
    const TfToken curveBasis(topology.GetCurveBasis());
    int rdlCurveType;
    if (curveType == HdTokens->linear)  rdlCurveType = rdlCurveType_linear;
    else if (curveBasis == HdTokens->bezier) rdlCurveType = rdlCurveType_bezier;
    else if (curveBasis == HdTokens->bSpline) rdlCurveType = rdlCurveType_bspline;
    else {
        Logger::error(GetId(), ": unsupported curve basis '", curveBasis, "'");
        return;
    }

    mGeometry.set(rdlAttrCurveType, rdlCurveType);
    mGeometry.set(rdlAttrCurveVertexCounts, topology.GetCurveVertexCounts());
}

void
HdMoonray_BasisCurves::syncDisplayStyle(const HdDisplayStyle& style)
{
    // refineLevel is set by the "complexity" menu item in usdview
    // and is an integer 0 to 8. At low refineLevel, we simplify the geometry
    // to improve render performance. These defaults can be overridden by primvars
    // moonray:curves_subtype and moonray::tessellation_rate
    if (not isPrimvarUsed(HdMoonrayTokens->moonray_curves_subtype)) {
        bool round = style.refineLevel > REFINE_LEVEL_FLAT_CURVES;
        mGeometry.set(rdlAttrCurvesSubtype, round ? rdlCurvesSubtype_round : 
                                                      rdlCurvesSubtype_rayFacing);
    }
    if (not isPrimvarUsed(HdMoonrayTokens->moonray_tessellation_rate)) {
        int tessellationRate = (style.refineLevel < 1) ? TESS_RATE_LOW : TESS_RATE_NORMAL;
        mGeometry.set(rdlAttrTessellationRate, tessellationRate);
    }
}

void 
HdMoonray_BasisCurves::syncAttributes(HdSceneDelegate* sceneDelegate, 
                            HdMoonray_RenderDelegate& renderDelegate,
                            HdDirtyBits* dirtyBits,
                            const TfToken& reprToken) 
{    
    // Overridden from HdMoonray_GeometryBase to sync additional data in the
    // BasicCurves schema to RDL attributes. Will be called from syncAll()
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, GetId())) {
        syncTopology(GetBasisCurvesTopology(sceneDelegate));
    }    
        
    if (HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, GetId())) {
        syncDisplayStyle(GetDisplayStyle(sceneDelegate));
    }

    if (HdChangeTracker::IsTransformDirty(*dirtyBits, GetId())) {
        mGeometry.set(rdlAttrReverseNormals, isMirror());
    }

    // base class handles transform and double-sided
    HdMoonray_GeometryBase::syncAttributes(sceneDelegate, renderDelegate, dirtyBits, reprToken);
}

void
HdMoonray_BasisCurves::primvarChanged(HdSceneDelegate *sceneDelegate, 
                                      HdMoonray_RenderDelegate& renderDelegate,
                                      const TfToken& name,  
                                      const VtValue& value,
                                      const HdInterpolation& interp, 
                                      const TfToken& role)
{
    // Overridden from HdMoonray_GeometryBase to sync primvars that drive RDL object
    // attributes. Will be called from syncAll().
    // RdlCurveGeometry supports st/uv and widths via
    // object attributes

    if (name == HdMoonrayTokens->st || name == HdMoonrayTokens->uv) {
        mGeometry.set(rdlAttrUvList, value);
    } else if (name ==  HdTokens->widths) {
        if (value.IsEmpty()) {
            mGeometry.setToDefault(rdlAttrRadiusList);
        } else if (value.IsHolding<VtFloatArray>()) {
            const VtFloatArray& v = value.UncheckedGet<VtFloatArray>();
            VtFloatArray w{v.begin(), v.end()};
            // width is diameter : convert to radius
            for (float& r : w) r /= 2;
            mGeometry.set(rdlAttrRadiusList, w);
        } else {
            Logger::error(GetId(), ": unsupported type for widths primvar");
            mGeometry.setToDefault(rdlAttrRadiusList);
        }
    } else {
        // allow HdMoonray_GeometryBase to handle all other cases, including
        // primvar overrides and UserData
        HdMoonray_GeometryBase::primvarChanged(sceneDelegate, renderDelegate,
                                      name, value, interp, role);
    }
}

void
HdMoonray_BasisCurves::Sync(HdSceneDelegate *sceneDelegate,
                            HdRenderParam   *renderParam,
                            HdDirtyBits     *dirtyBits,
                            TfToken const   &reprToken)
{
    hdmLogSyncStart("BasisCurves", GetId(), dirtyBits);   
    HdMoonray_RenderDelegate& renderDelegate(HdMoonray_RenderDelegate::get(renderParam));
    
    _UpdateVisibility(sceneDelegate, dirtyBits);
    _UpdateInstancer(sceneDelegate, dirtyBits);
    
    syncAll(rdlClassCurves, sceneDelegate, renderDelegate, dirtyBits, reprToken);
    
    hdmLogSyncEnd(GetId());
}

}
