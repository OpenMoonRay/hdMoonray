// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// Mesh is implemented using RdlMeshGeometry
// RDL attributes that don't have an equivalent in Mesh are set to
// an automatic default, but may be overridden by primvars of the form
// "moonray:xyz":
//  - mesh_resolution def: 1 << refineLevel
//  - adaptive_error def: 0.0 or 1.0
//  - smooth_normal def: false
// Supports "uv" or "st" for texture coords, and 
// "normal" or "normals" for normals

// 
#include "mesh.h"
#include "renderDelegate.h"
#include "material.h"
#include "HdmLog.h"
#include "MurmurHash3.h"
#include "tokens.h"

#include <pxr/base/gf/vec2f.h>
#include <pxr/imaging/hd/meshUtil.h>
#include <pxr/imaging/pxOsd/tokens.h>

#include <iostream>

using namespace pxr;

namespace {

// constants for RDL attrs and enums
const std::string rdlClassMesh("RdlMeshGeometry");
const std::string rdlAttrFaceVertexCount("face_vertex_count");
const std::string rdlAttrVerticesByIndex("vertices_by_index");
const std::string rdlAttrOrientation("orientation");
constexpr int rdlOrientationRightHanded = 0;
constexpr int rdlOrientationLeftHanded = 1;
const std::string rdlAttrPartFaceCountList("part_face_count_list");
const std::string rdlAttrPartFaceIndices("part_face_indices");
const std::string rdlAttrPartList("part_list");
const std::string rdlAttrSmoothNormal("smooth_normal");
const std::string rdlAttrIsSubd("is_subd");
const std::string rdlAttrMeshResolution("mesh_resolution");
const std::string rdlAttrAdaptiveError("adaptive_error");
const std::string rdlAttrSubdScheme("subd_scheme");
constexpr int rdlSubdSchemeBilinear = 0;
constexpr int rdlSubdSchemeCatClark = 1;
const std::string rdlAttrSubdBoundary("subd_boundary");
constexpr int rdlSubdBoundaryNone = 0;
constexpr int rdlSubdBoundaryEdgeOnly = 1;
constexpr int rdlSubdBoundaryEdgeAndCorner = 2;
const std::string rdlAttrSubdFvarLinear("subd_fvar_linear");
constexpr int rdlSubdFvarLinearNone = 0;
constexpr int rdlSubdFvarLinearCornersOnly = 1;
constexpr int rdlSubdFvarLinearCornersPlus1 = 2;
constexpr int rdlSubdFvarLinearCornersPlus2 = 3;
constexpr int rdlSubdFvarLinearBoundaries = 4;
constexpr int rdlSubdFvarLinearAll = 5;
const std::string rdlAttrSubdCreaseIndices("subd_crease_indices");
const std::string rdlAttrSubdCreaseSharpnesses("subd_crease_sharpnesses");
const std::string rdlAttrSubdCornerIndices("subd_corner_indices");
const std::string rdlAttrSubdCornerSharpnesses("subd_corner_sharpnesses");
const std::string rdlAttrNormalList("normal_list");
const std::string rdlAttrUvList("uv_list");
}

namespace hdMoonray {

HdMoonray_Mesh::HdMoonray_Mesh(SdfPath const& id) :
    HdMesh(id), HdMoonray_GeometryBase(this) 
{
}

void
HdMoonray_Mesh::Finalize(HdRenderParam* renderParam)
{
    // causes geometry to be hidden
    resetGeometryObject(HdMoonray_RenderDelegate::get(renderParam));
}

HdDirtyBits
HdMoonray_Mesh::GetInitialDirtyBitsMask() const
{
    int mask = HdChangeTracker::Clean
        | HdChangeTracker::InitRepr
        | HdChangeTracker::DirtyPrimID
        | HdChangeTracker::DirtyDisplayStyle
        | HdChangeTracker::DirtyPrimvar
        | HdChangeTracker::DirtyMaterialId
        | HdChangeTracker::DirtyTopology
        | HdChangeTracker::DirtyTransform
        | HdChangeTracker::DirtyVisibility
        | HdChangeTracker::DirtyDoubleSided
        | HdChangeTracker::DirtySubdivTags
        | HdChangeTracker::DirtyInstancer
        | HdChangeTracker::DirtyInstanceIndex
        | HdChangeTracker::DirtyCategories
        | HdChangeTracker::DirtyRepr;
    return (HdDirtyBits)mask;
}

HdDirtyBits
HdMoonray_Mesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

// This seems to be a GL thing to set up a shader. Sync is not called.
// This is only needed if Sync() looks at Repr. 
// Mesh does this to get flat vs smooth usdview render setting.
void
HdMoonray_Mesh::_InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits)
{
    // doing this seems to cause Sync() to be called:
    *dirtyBits |= HdChangeTracker::DirtyRepr;
}


MoonrayObject 
HdMoonray_Mesh::geometryForMeshLight(HdMoonray_RenderDelegate& renderDelegate)
{
    // MeshLight(GeometryLight) may require the RDL2 geometry
    // object before the Mesh has synced. Since light syncs
    // are not run in parallel, this does not need to be
    // threadsafe
    if (mGeometry.isValid()) return mGeometry;
    mGeometry = renderDelegate.scene().createObject(rdlClassMesh, rprim.GetId());
    return mGeometry;
}

void
HdMoonray_Mesh::syncTopology(const HdMeshTopology& topology)
{
    // copies face and subset (part) data to RDL
    // vertices (points) are copied by base HdMoonray_GeometryBase class

    mGeometry.set(rdlAttrFaceVertexCount, topology.GetFaceVertexCounts());
    mGeometry.set(rdlAttrVerticesByIndex, topology.GetFaceVertexIndices());

    TfToken orientation = topology.GetOrientation();
    int rdlOrientation = rdlOrientationRightHanded;
    if (orientation == PxOsdOpenSubdivTokens->leftHanded) {
        rdlOrientation = rdlOrientationLeftHanded;
    }
    mGeometry.set(rdlAttrOrientation, rdlOrientation);

    // subsets (== rdl parts)
    const HdGeomSubsets& geomSubsets(topology.GetGeomSubsets());
    VtIntArray partFaceCountList;
    VtIntArray partFaceIndices;
    partFaceCountList.reserve(geomSubsets.size());
    partFaceIndices.reserve(geomSubsets.size()); // actually larger
    partList.clear();       partList.reserve(geomSubsets.size());
    partMaterials.clear();  partMaterials.reserve(geomSubsets.size());
    partPaths.clear();      partPaths.reserve(geomSubsets.size());
    for (const HdGeomSubset& geomSubset : geomSubsets) {
        partFaceCountList.push_back(int(geomSubset.indices.size()));
        for (int i : geomSubset.indices) {
            partFaceIndices.push_back(i);
        }
        partList.push_back(geomSubset.id.GetName());
        partMaterials.push_back(geomSubset.materialId);
        partPaths.push_back(geomSubset.id);
    }
    mGeometry.set(rdlAttrPartFaceCountList, partFaceCountList);
    mGeometry.set(rdlAttrPartFaceIndices, partFaceIndices);
    mGeometry.set(rdlAttrPartList, partList);
}

float
HdMoonray_Mesh::clampMeshResolution(float meshResolution, HdMoonray_RenderDelegate& renderDelegate) const
{
    // clamp mesh resolution to a reasonable range, to avoid
    // excessive memory usage or crashes. This is a user option
    // that can be overridden by a primvar, so we don't want to
    // stomp on the primvar value if it is set.
    float maxRes = renderDelegate.options().getMaxMeshResolution();
    if (maxRes > 0.0f && meshResolution > maxRes) {
        Logger::warn("Mesh ",GetId(),": limiting mesh resolution from ",meshResolution,
                      " to ", maxRes);
        return maxRes;
    }
    return meshResolution;
}

void
HdMoonray_Mesh::syncSubdivScheme(const HdMeshTopology& topology,
                       HdSceneDelegate *sceneDelegate,
                       HdMoonray_RenderDelegate& renderDelegate,
                       const TfToken& reprToken)
{
    // sets the subdivision scheme, and related options
    // resolution, adaptive_error and smooth normals

    TfToken subdScheme = topology.GetScheme();
    int rdlScheme = rdlSubdSchemeCatClark; // moonray default
    if (subdScheme == PxOsdOpenSubdivTokens->bilinear) rdlScheme = rdlSubdSchemeBilinear;
    // scheme "loop" is not supported by Moonray, and replaced by "catClark"
    mGeometry.set(rdlAttrSubdScheme, rdlScheme);

    // This is the "complexity" menu item in usdview (low=0, medium=1, ...)
    // There is a topology.GetRefineLevel() but it appears to always be a copy of
    // the parameter (which defaults to 0) sent to GetMeshTopology()
    int refineLevel = GetDisplayStyle(sceneDelegate).refineLevel;

    // We render as polygons if:
    // - the repr has flat shading enabled
    // - the user has forced polygon rendering
    // - the subd scheme is "none"
    // - refineLevel is 0
    bool flatPolygons = _GetReprDesc(reprToken)[0].flatShadingEnabled ||
                       renderDelegate.options().getForcePolygon() ||
                       subdScheme == PxOsdOpenSubdivTokens->none;
    bool smoothPolygons = !flatPolygons && 
                          rdlScheme == rdlSubdSchemeCatClark &&
                          refineLevel == 0;

    bool polygons = flatPolygons || smoothPolygons;
    mGeometry.set(rdlAttrIsSubd, !polygons);

    // mesh resolution, adaptive error and smooth_normals can be overridden
    // by primvars, so check before overwriting
    static TfToken meshResolutionToken("moonray:mesh_resolution");
    static TfToken adaptiveErrorToken("moonray:adaptive_error");
    static TfToken smoothNormalToken("moonray:smooth_normal");

    if (not isPrimvarUsed(meshResolutionToken)) {
        // to match Storm, use 1 << refineLevel for resolution
        float rdlResolution = 1 << refineLevel;
        rdlResolution = clampMeshResolution(rdlResolution, renderDelegate);
        mGeometry.set(rdlAttrMeshResolution, rdlResolution);
    }

    if (not isPrimvarUsed(adaptiveErrorToken)) {
        float adaptive_error = topology.IsEnabledAdaptive() ? 1.0f : 0.0f;
        mGeometry.set(rdlAttrAdaptiveError, adaptive_error);
    }

    if (not isPrimvarUsed(smoothNormalToken)) {
        mGeometry.set(rdlAttrSmoothNormal, smoothPolygons);
    }
}

void
HdMoonray_Mesh::syncSubdivTags(const PxOsdSubdivTags& tags)
{
    // sets RDL attrs from subdiv tags

    TfToken t = tags.GetVertexInterpolationRule();
    int rdlValue;
    if (t == PxOsdOpenSubdivTokens->none)          rdlValue = rdlSubdBoundaryNone;
    else if (t == PxOsdOpenSubdivTokens->edgeOnly) rdlValue = rdlSubdBoundaryEdgeOnly;
    else                                           rdlValue = rdlSubdBoundaryEdgeAndCorner;
    mGeometry.set(rdlAttrSubdBoundary, rdlValue);

    t = tags.GetFaceVaryingInterpolationRule();
    if (t == PxOsdOpenSubdivTokens->none)              rdlValue = rdlSubdFvarLinearNone;
    else if (t == PxOsdOpenSubdivTokens->cornersOnly)  rdlValue = rdlSubdFvarLinearCornersOnly;
    else if (t == PxOsdOpenSubdivTokens->cornersPlus1) rdlValue = rdlSubdFvarLinearCornersPlus1;
    else if (t == PxOsdOpenSubdivTokens->cornersPlus2) rdlValue = rdlSubdFvarLinearCornersPlus2;
    else if (t == PxOsdOpenSubdivTokens->boundaries)   rdlValue =  rdlSubdFvarLinearBoundaries;
    else if (t == PxOsdOpenSubdivTokens->all)          rdlValue = rdlSubdFvarLinearAll;
    else                                               rdlValue = rdlSubdFvarLinearCornersOnly;
    mGeometry.set(rdlAttrSubdFvarLinear, rdlValue);


    VtIntArray vi = tags.GetCreaseIndices();
    if (not vi.empty()) {
        // Hydra has connected strings of edges. Convert to RdlMesh individual edges
        VtIntArray lengths(tags.GetCreaseLengths());
        const size_t edges = vi.size() - lengths.size();
        VtFloatArray vf = tags.GetCreaseWeights();
        bool weightPerEdge = vf.size() >= edges;
        VtIntArray result(2 * edges);
        VtFloatArray weights(edges);
        size_t i = 0; // index into crease indices
        size_t edge = 0; // index into result
        for (size_t crease = 0; crease < lengths.size(); ++crease) {
            int n = lengths[crease];
            for (int k = 0; k + 1 < n; k++) {
                result[2*edge] = vi[i];
                result[2*edge + 1] = vi[i + 1];
                weights[edge] = weightPerEdge ? vf[edge] : vf[crease];
                ++i; ++edge;
            }
            ++i; // don't connect last point with first of next edge
        }
        mGeometry.set(rdlAttrSubdCreaseIndices, result);
        mGeometry.set(rdlAttrSubdCreaseSharpnesses, weights);
    } else {
        // doing this leaves it in the rdla data
        // mGeometry.setToDefault("subd_crease_indices");
        // mGeometry.setToDefault("subd_crease_sharpnesses");
    }

    vi = tags.GetCornerIndices();
    if (not vi.empty()) {
        mGeometry.set(rdlAttrSubdCornerIndices, vi);
        VtFloatArray vf = tags.GetCornerWeights();
        mGeometry.set(rdlAttrSubdCornerSharpnesses, vf);
    } else {
        // mGeometry.setToDefault("subd_corner_indices");
        // mGeometry.setToDefault("subd_corner_sharpnesses");
    }
}

void
HdMoonray_Mesh::primvarChanged(HdSceneDelegate *sceneDelegate, HdMoonray_RenderDelegate& renderDelegate,
                     const TfToken& name,  const VtValue& value,
                     const HdInterpolation& interp, const TfToken& role)
{
    // Overridden from HdMoonray_GeometryBase to sync primvars that drive RDL object
    // attributes. Will be called from syncAll().
    // RdlMeshGeometry supports st/uv, and normal/normals as
    // object attributes

    if (name == HdTokens->normals || name == HdMoonrayTokens->normal) {
        if (value.IsEmpty()) {
            mGeometry.setToDefault(rdlAttrNormalList);
        } else if (value.IsHolding<VtVec3fArray>()) {
            mGeometry.set(rdlAttrNormalList, value.UncheckedGet<VtVec3fArray>());
        } 
    } else if (name == HdMoonrayTokens->st || name == HdMoonrayTokens->uv) {
        if (value.IsEmpty()) {
            mGeometry.setToDefault(rdlAttrUvList);
        } else {
            if (value.IsHolding<VtVec3fArray>()) {
                const VtVec3fArray& v = value.UncheckedGet<VtVec3fArray>();
                VtVec2fArray out;
                out.reserve(v.size());
                for (const auto& v3 : v) {
                    out.emplace_back(GfVec2f(v3[0], v3[1]));
                }
                mGeometry.set(rdlAttrUvList, out);
            } else if (value.IsHolding<VtVec2fArray>()) {
                mGeometry.set(rdlAttrUvList, value.UncheckedGet<VtVec2fArray>());
            }
        }
    } else {
        // allow HdMoonray_GeometryBase to handle all other cases
        HdMoonray_GeometryBase::primvarChanged(sceneDelegate, renderDelegate,
                                      name, value, interp, role);
    }
}
void
HdMoonray_Mesh::primvarAttributeOverride(const std::string& name, const pxr::VtValue& value, HdMoonray_RenderDelegate& renderDelegate)
{
    // Special handling to clamp mesh resolution
    if (name == rdlAttrMeshResolution && value.IsHolding<float>()) {
        float res = value.UncheckedGet<float>();
        res = clampMeshResolution(res, renderDelegate);
        mGeometry.set(name, res);
    } else {
        HdMoonray_GeometryBase::primvarAttributeOverride(name, value, renderDelegate);
    }
}

void
HdMoonray_Mesh::syncCryptomatteUserData(HdMoonray_RenderDelegate& renderDelegate)
{
    // adds additional UserData to support Cryptomatte render outputs
    if (!renderDelegate.options().getDisableRender()) return;
    std::string idName = renderDelegate.options().getDeepIdAttrName();
    if (idName.empty()) return;

    std::string suffix = ".primvars:" + idName;

    MoonrayObject cryptoId = renderDelegate.scene().createObject("UserData", GetId(), suffix);
    {
        UpdateGuard guard(cryptoId);
        if (!partList.empty()){
            VtFloatArray data;
            data.reserve(partList.size());
            for (auto& part: partList){
                data.emplace_back(MurmurHash3_to_float(part.c_str()));
            }
            cryptoId.setData(idName, data, TfToken());
        } else {
            float data = MurmurHash3_to_float(mGeometry.objectName().c_str());
            cryptoId.setData(idName, data, TfToken());

        }

    }
    addUserData(TfToken("primvars:"+idName), cryptoId);
}

void
HdMoonray_Mesh::syncAttributes(HdSceneDelegate* sceneDelegate, 
                            HdMoonray_RenderDelegate& renderDelegate,
                            HdDirtyBits* dirtyBits,
                            const TfToken& reprToken) 
{
    // Overridden from HdMoonray_GeometryBase to sync additional data in the
    // Mesh schema to RDL attributes. Will be called from syncAll()
    
    const SdfPath& id = GetId();

    if (HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, id) ||
        HdChangeTracker::IsReprDirty(*dirtyBits, id) ||
        HdChangeTracker::IsTopologyDirty(*dirtyBits, id) ||
        HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id)) {
            
        const HdMeshTopology& topology = GetMeshTopology(sceneDelegate);
        if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
            // handles faces and GeomSubsets (verts handled by base class)
            syncTopology(topology);
            // if parts change, need to generate user data that drives cryptomattes
            syncCryptomatteUserData(renderDelegate);
        }
        // choose and set the subdiv scheme
        syncSubdivScheme(topology, sceneDelegate, renderDelegate, reprToken);        

        if (HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id)) {
            // tags specify interpolation, creases and corners
            syncSubdivTags(sceneDelegate->GetSubdivTags(id));
        }
    }

    // base class handles transform and double-sided
    HdMoonray_GeometryBase::syncAttributes(sceneDelegate, renderDelegate, dirtyBits, reprToken);
}

void
HdMoonray_Mesh::Sync(HdSceneDelegate *sceneDelegate,
           HdRenderParam   *renderParam,
           HdDirtyBits     *dirtyBits,
           TfToken const   &reprToken)
{
    hdmLogSyncStart("Mesh", GetId(), dirtyBits);
    HdMoonray_RenderDelegate& renderDelegate(HdMoonray_RenderDelegate::get(renderParam));
    
    _UpdateVisibility(sceneDelegate, dirtyBits);
    _UpdateInstancer(sceneDelegate, dirtyBits);
    
    syncAll(rdlClassMesh, sceneDelegate, renderDelegate, dirtyBits, reprToken);

    hdmLogSyncEnd(GetId());
}
 
}

