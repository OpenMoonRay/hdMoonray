// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayLightFilter.h"
#include "renderDelegate.h"
#include "ValueConverter.h"
#include "material.h"
#include "camera.h"
#include "HdmLog.h"
#include "shader_utils.h"
#include "tokens.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usdLux/tokens.h>

#include <iostream>

using namespace pxr;

namespace {

void
makeLightFilterInert(hdMoonray::MoonrayObject lightFilter,
                     hdMoonray::HdMoonray_RenderDelegate& renderDelegate)
{
    if (lightFilter.isNull()) {
        return;
    }

    hdMoonray::UpdateGuard guard(renderDelegate, lightFilter);
    try {
        lightFilter.set("on", false);
    } catch (const std::exception&) {
        // Some future/custom LightFilter classes may not expose "on". RDL
        // objects cannot be deleted interactively, so category release and
        // pointer clearing still make the object inert from hdMoonray's side.
    }
}

}

namespace hdMoonray {

HdDirtyBits
MoonrayLightFilter::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::DirtyParams;
}

void
MoonrayLightFilter::syncParams(const PrimAccess& access,
                              HdMoonray_RenderDelegate& renderDelegate)
{
    // querying "lightFilterLink" will return a token used to name the "category" that
    // holds all geometry that this lightFilter links to. See Light.cc for more details
    // on how linking and categories work.
    HdSceneDelegate* sceneDelegate = access.sceneDelegate();
    TfToken t;
    VtValue linkVal = sceneDelegate->GetLightParamValue(access.id(), HdTokens->lightFilterLink);
    if (linkVal.IsHolding<TfToken>()) {
        t = linkVal.Get<TfToken>();
    }
    if (t != mLightFilterCategory) {     
        if (mLightFilterCategory != HdMoonrayTokens->categoryUnset) {
            renderDelegate.scene().releaseCategory(mLightFilter, FilterCategory, mLightFilterCategory);
        }
        mLightFilterCategory = t;
        renderDelegate.scene().setCategory(mLightFilter, FilterCategory, mLightFilterCategory);
    }
    for (auto attrIt = mLightFilter.beginAttributes(); attrIt != mLightFilter.endAttributes(); ++attrIt) {
        const std::string& attrName = (*attrIt).name();
        if (attrName == "node_xform") {
            syncXform(access, renderDelegate);
        } else if (attrName == "projector") {
            syncProjector(access, renderDelegate);
        } else if (attrName == "texture_map") {
            syncTextureMap(access, renderDelegate);
        } else  if (attrName == "light_filters") {
            syncCombineFilters(access, renderDelegate);
        } else {
            VtValue val = access.Get(TfToken(attrName));
            if (val.IsEmpty()) {
                val = getTerminalNodeParameter(
                    access.id(), "lightFilter", TfToken(attrName), sceneDelegate);
            }
            if (val.IsEmpty()) {
                (*attrIt).setToDefault();
            } else {
                (*attrIt).set(val);
            }
        }
    }
}

void
MoonrayLightFilter::syncProjector(const PrimAccess& access,
                                  HdMoonray_RenderDelegate& renderDelegate)
{
    // sync the "projector" attribute of Cookie light filters,
    // which is authored as a "rel" to a camera.
    HdSceneDelegate* sceneDelegate = access.sceneDelegate();
    VtValue val = access.Get(HdMoonrayTokens->projector);
    SdfPath path;
    if (val.IsHolding<SdfPath>()) {
        path = val.UncheckedGet<SdfPath>();
    } else if (val.IsHolding<SdfPathVector>()) {
        const SdfPathVector& paths = val.UncheckedGet<SdfPathVector>();
        if (!paths.empty()) {
            path = paths[0];
        }
    } else if (val.IsHolding<VtArray<SdfPath>>()) {
        const VtArray<SdfPath>& paths = val.UncheckedGet<VtArray<SdfPath>>();
        if (!paths.empty()) {
            path = paths[0];
        }
    }
    
    if (path.IsEmpty()) {
        mLightFilter.setToDefault("projector");
    } else {
        path = path.ReplacePrefix(SdfPath::AbsoluteRootPath(), sceneDelegate->GetDelegateID());
        MoonrayObject mo = HdMoonray_Camera::createCamera(sceneDelegate, renderDelegate, path);
        mLightFilter.set("projector", mo);
        if (mo.isNull()) {
            Logger::error(GetId(), ".moonray:projector: ", path, " not found");
        }
    }
}

void
MoonrayLightFilter::syncXform(const PrimAccess& access,
                             HdMoonray_RenderDelegate& renderDelegate)
{
    // TODO: Doesn't work for Hydra 2 : implement sample for PrimAccess
    HdTimeSampleArray<GfMatrix4d, 4> sampledXforms;
    std::pair<float, float> shutterInterval = renderDelegate.scene().getTimeSamplingInterval();
    access.sceneDelegate()->SampleTransform(access.id(), shutterInterval.first, shutterInterval.second, &sampledXforms);
    // if there's only one sample, it should match the cached value
    if (sampledXforms.count <= 1) {
        mLightFilter.set("node_xform", sampledXforms.values[0]);
    } else {
        // first and last samples will be sample interval boundaries
        mLightFilter.set("node_xform", sampledXforms.values[0], sampledXforms.values[sampledXforms.count-1]);
   }
}

void
MoonrayLightFilter::syncCombineFilters(const PrimAccess& access,
                                       HdMoonray_RenderDelegate& renderDelegate)
{
    // sync the "light_filters" attribute of Combine light filters,
    // which is authored as "rel"s to light filters.
    HdSceneDelegate* sceneDelegate = access.sceneDelegate();
    VtValue val = access.Get(HdMoonrayTokens->light_filters);
    // in Usd with SceneIndex mode this value is generated by UsdImagingDataSourceRelationship
    // from a rel in Usd. This produces VtArray<SdfPath>, not SdfPathVector (which is
    // std::vector<SdfPath>)
    if (val.IsHolding<VtArray<SdfPath>>()) {
        MoonrayObjectVector filters;
        VtArray<SdfPath> pathVec = val.UncheckedGet<VtArray<SdfPath>>();
        for (const SdfPath& cpath : pathVec) {
            SdfPath path(cpath);
            path = path.ReplacePrefix(SdfPath::AbsoluteRootPath(), sceneDelegate->GetDelegateID());
            MoonrayObject mo = MoonrayLightFilter::getLightFilter(path, renderDelegate, sceneDelegate);
            if (mo.isValid()) {
                filters.append(mo);
            } else {
                Logger::error(GetId(), ".moonray:light_filters: ", path, " not found");
            }
        }
        mLightFilter.set("light_filters", filters);
    } else if (not val.IsEmpty()) {
        Logger::error(GetId(), ".moonray:light_filters: must be a list of paths");
    }
}

void
MoonrayLightFilter::syncTextureMap(const PrimAccess& access,
                                   HdMoonray_RenderDelegate& renderDelegate)
{
    HdSceneDelegate* sceneDelegate = access.sceneDelegate();
    MoonrayObject textureMap = 
        getNodeByConnection(access.id(), "moonray:texture_map", renderDelegate, sceneDelegate);
    mLightFilter.set("texture_map", textureMap);
}


// Hydra doesn't currently seem to analyse the dependency between light and light filter
// correctly, so a light may be synced before the filters it references. To work around
// this, we need to allow the light to create the filter outside Sync, under a mutex...
MoonrayObject
MoonrayLightFilter::getOrCreateFilter(const PrimAccess& access,
                                      HdMoonray_RenderDelegate& renderDelegate)
{
    VtValue vtClass = access.Get(HdMoonrayTokens->_class);

    pxr::TfToken classToken;
    if (vtClass.IsHolding<pxr::TfToken>()) {
        classToken = vtClass.UncheckedGet<pxr::TfToken>();
    } else if (vtClass.IsHolding<std::string>()) {
        classToken = pxr::TfToken(vtClass.UncheckedGet<std::string>());
    }
    if (classToken.IsEmpty()) {
        classToken = getTerminalNodeIdentifier(
            access.id(), "lightFilter", access.sceneDelegate());
    }

    if (!renderDelegate.scene().checkClassInterface(classToken.GetString(), MoonrayAttribute::InterfaceType::INTERFACE_LIGHTFILTER)) {
        Logger::error(access.id(), ": invalid MoonRay LightFilter class '", classToken, "'");
        if (mLightFilter.isValid()) {
            makeLightFilterInert(mLightFilter, renderDelegate);
            renderDelegate.scene().releaseCategory(mLightFilter, FilterCategory, mLightFilterCategory);
            mLightFilter = MoonrayObject();
            mLightFilterCategory = pxr::TfToken();
        }
        return MoonrayObject();
    }

    if (mLightFilter.isValid() && mLightFilter.className() != classToken.GetString()) {
        makeLightFilterInert(mLightFilter, renderDelegate);
        renderDelegate.scene().releaseCategory(mLightFilter, FilterCategory, mLightFilterCategory);
        mLightFilter = MoonrayObject();
        mLightFilterCategory = pxr::TfToken();
    }

    std::lock_guard<std::mutex> lock(mCreateMutex);
    if (mLightFilter.isNull()) {
        mLightFilter = renderDelegate.scene().createObject(classToken.GetString(), access.id());
        mLightFilterCategory = HdMoonrayTokens->categoryUnset; // empty token means "applies to all geometry"
        
    }
    return mLightFilter;
}

void
MoonrayLightFilter::Sync(HdSceneDelegate *sceneDelegate,
                         HdRenderParam   *renderParam,
                         HdDirtyBits     *dirtyBits)
{
    SdfPath id = GetId();
    hdmLogSyncStart("MoonrayLightFilter", id, dirtyBits);
    HdMoonray_RenderDelegate& renderDelegate(HdMoonray_RenderDelegate::get(renderParam));
    PrimAccess access(GetId(), HdMoonrayTokens->moonray, sceneDelegate, renderDelegate);

    if (getOrCreateFilter(access, renderDelegate).isNull()) {
        *dirtyBits = pxr::HdChangeTracker::Clean;
        hdmLogSyncEnd(id);
        return;
    }

    if ((*dirtyBits) & HdChangeTracker::DirtyParams) {
        UpdateGuard guard(renderDelegate, mLightFilter);
        syncParams(access, renderDelegate);
    }

    *dirtyBits = HdChangeTracker::Clean;
    hdmLogSyncEnd(id);
}

void
MoonrayLightFilter::Finalize(HdRenderParam *renderParam)
{
    if (mLightFilter.isNull()) {
        return;
    }

    HdMoonray_RenderDelegate& renderDelegate(HdMoonray_RenderDelegate::get(renderParam));
    makeLightFilterInert(mLightFilter, renderDelegate);
    renderDelegate.scene().releaseCategory(mLightFilter, FilterCategory, mLightFilterCategory);
    mLightFilter = MoonrayObject();
    mLightFilterCategory = pxr::TfToken();
}

/* static*/ MoonrayObject
MoonrayLightFilter::getLightFilter(const SdfPath& id,
                            HdMoonray_RenderDelegate& renderDelegate,
                            HdSceneDelegate *sceneDelegate)
{
    if (id.IsEmpty()) return MoonrayObject();
    MoonrayLightFilter* moonrayFilterPrim = static_cast<MoonrayLightFilter*>(
        sceneDelegate->GetRenderIndex().GetSprim(HdLightFilterTypeTokens->lightFilter, id));
    if (moonrayFilterPrim) {
        PrimAccess access(id, HdMoonrayTokens->moonray, sceneDelegate, renderDelegate);
        return moonrayFilterPrim->getOrCreateFilter(access, renderDelegate);
    }
    return MoonrayObject();
}

}

