// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "light.h"
#include "MoonrayLightFilter.h"
#include "mesh.h"
#include "renderDelegate.h"
#include "ValueConverter.h"
#include "HdmLog.h"
#include "instancer.h"
#include "hydra2_utils.h"
#include "tokens.h"

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usdLux/blackbody.h>

#include <iostream>

using namespace pxr;
namespace {

const std::string&
defaultRdlClassName(const TfToken& type)
{
    static const std::string def;
    static std::map<TfToken,std::string> rdlClassMap =
        { { HdPrimTypeTokens->cylinderLight , "CylinderLight" },
          { HdPrimTypeTokens->diskLight, "DiskLight" },
          { HdPrimTypeTokens->distantLight, "DistantLight" },
          { HdPrimTypeTokens->domeLight, "EnvLight" },
          { HdPrimTypeTokens->rectLight, "RectLight" },
          { HdPrimTypeTokens->sphereLight, "SphereLight" },
          { HdMoonrayTokens->geometryLight, "MeshLight" }};
    auto it = rdlClassMap.find(type);
    if (it == rdlClassMap.end()) return def;
    return it->second;
}

bool
isMoonRayLightClass(const std::string& className, 
                    hdMoonray::HdMoonray_RenderDelegate& renderDelegate)
{
    return renderDelegate.scene().checkClassInterface(className, hdMoonray::MoonrayAttribute::InterfaceType::INTERFACE_LIGHT);
}

bool
isUsdShapedLight(const pxr::SdfPath& id, pxr::HdSceneDelegate *sceneDelegate)
{
    return sceneDelegate->GetLightParamValue(id, pxr::HdLightTokens->shapingConeAngle).IsHolding<float>() ||
           sceneDelegate->GetLightParamValue(id, pxr::HdLightTokens->shapingConeSoftness).IsHolding<float>();
}

bool
canUseMoonRaySpotLightForType(const pxr::TfToken& type)
{
    return type == pxr::HdPrimTypeTokens->diskLight ||
           type == pxr::HdPrimTypeTokens->sphereLight;
}

bool
canUseMoonRayShapingForType(const pxr::TfToken& type)
{
    return canUseMoonRaySpotLightForType(type) ||
           type == pxr::HdPrimTypeTokens->rectLight;
}

}

namespace hdMoonray {

/*static*/ MoonrayObject 
HdMoonray_Light::lightForMaterialLink(HdSceneDelegate *sceneDelegate, const SdfPath& path)
{
    // find a light of any type, given its path
    SdfPath lightPath(path);
    lightPath.ReplacePrefix(SdfPath::AbsoluteRootPath(), sceneDelegate->GetDelegateID());
    const HdRenderIndex& ri = sceneDelegate->GetRenderIndex();
    static std::vector<TfToken> lightTypes = {
        HdPrimTypeTokens->cylinderLight,
        HdPrimTypeTokens->diskLight,
        HdPrimTypeTokens->distantLight,
        HdPrimTypeTokens->domeLight,
        HdPrimTypeTokens->rectLight,
        HdPrimTypeTokens->sphereLight,
        HdMoonrayTokens->geometryLight
    };
    HdSprim *lightPrim = nullptr;
    for (TfToken lightType : lightTypes) {
            if (lightPrim = ri.GetSprim(lightType,lightPath)) break;
    }
    HdMoonray_Light* hdLight = dynamic_cast<HdMoonray_Light*>(lightPrim);
    if (!hdLight) {
        std::cout << "Cannot find light " << lightPath.GetString() << std::endl;
        return nullptr;
    }
    MoonrayObject rdlLight = hdLight->mMoonrayLight;
    if (rdlLight.isNull()) {
         std::cout << "Light not created yet " << lightPath.GetString() << std::endl;
    }
    return rdlLight;
}

HdDirtyBits
HdMoonray_Light::GetInitialDirtyBitsMask() const
{
    return HdLight::AllDirty;
}


const std::string&
HdMoonray_Light::rdlClassName(const SdfPath& id,
                            HdSceneDelegate *sceneDelegate,
                            HdMoonray_RenderDelegate& renderDelegate)
{
    const static std::string spotLight("SpotLight");

    // identify the rdl light class to use. This can be specified via "token moonray:class =" or
    // deduced from the Lux/Usd type
    const std::string& luxRdlClass(defaultRdlClassName(mType));
    mRectToSpotlight = false;

    VtValue v = sceneDelegate->GetLightParamValue(id, HdMoonrayTokens->moonray_class);
    bool isRectLight = false;
    if (v.IsHolding<TfToken>()) {
        // moonray:class is specified
        TfToken rdlClassToken = v.UncheckedGet<TfToken>();
        const std::string& rdlClassName = rdlClassToken.GetString();

        // check the specified class is actually an RDL light
        if (!isMoonRayLightClass(rdlClassName, renderDelegate)) {
            // if the specified class is invalid, fall back to the default class for the Lux type, with a warning
            if (!luxRdlClass.empty()) {
                const bool shapedSpot = isUsdShapedLight(id, sceneDelegate) &&
                                        canUseMoonRaySpotLightForType(mType);
                const std::string& fallbackClass = shapedSpot ? spotLight : luxRdlClass;
                Logger::warn(id, ".moonray:class: invalid MoonRay light class '", rdlClassName,
                             "'; falling back to USD light type '", mType.GetString(),
                             "' as '", fallbackClass, "'");
                return fallbackClass;
            }
            Logger::error(id, ".moonray:class: invalid MoonRay light class '", rdlClassName,
                          "' and no fallback exists for USD light type '", mType.GetString(), "'");
            return luxRdlClass;
        }

        if (rdlClassToken == HdMoonrayTokens->RectLight) {
            isRectLight = true;
            mRectToSpotlight = isUsdShapedLight(id, sceneDelegate);
        }

        // Issue a warning if the moonray:class does not match the Lux type:
        bool warning = false;
        if (rdlClassToken == HdMoonrayTokens->SpotLight) {
            warning = (mType != HdPrimTypeTokens->diskLight && mType != HdPrimTypeTokens->sphereLight);
        } else {
            warning = (rdlClassToken != luxRdlClass);
        }
        if (warning) {
            Logger::warn(id, ".moonray:class: '", rdlClassName,
                         "' may not be compatible with USD light type '", mType.GetString(), "'");
        }
        mRectToSpotlight = isRectLight && isUsdShapedLight(id, sceneDelegate);
        return rdlClassName;
    }
    
    // no moonray:class specified, use the default class for the Lux type
    // Use of the shaping API on a SphereLight or DiskLight causes us to use the RDL SpotLight class
    if (isUsdShapedLight(id, sceneDelegate)) {
        if (!canUseMoonRayShapingForType(mType)) {
              Logger::warn(id, ": shaping api may not be compatible with USD light type '", mType.GetString(), "'");
        } else if (canUseMoonRaySpotLightForType(mType)) {
            return spotLight;
        }
    }
    if (luxRdlClass.empty()) {
        Logger::error(id, ": Unsupported light type ", mType, " replaced by DiskLight");
        return defaultRdlClassName(HdPrimTypeTokens->diskLight);
    }
    return luxRdlClass;
}

bool
HdMoonray_Light::isSupportedType(const TfToken& type)
{
    return !defaultRdlClassName(type).empty();
}


void
HdMoonray_Light::fixLightXform(GfMatrix4d& mat)
{
    // in Usd/Lux cylinder is along x-axis, in moonray it is along y.
    if (mType == HdPrimTypeTokens->cylinderLight) {
        // rotate -90deg about z
        double t;
        t = mat[0][0]; mat[0][0] = -mat[1][0]; mat[1][0] = t;
        t = mat[0][1]; mat[0][1] = -mat[1][1]; mat[1][1] = t;
        t = mat[0][2]; mat[0][2] = -mat[1][2]; mat[1][2] = t;
    }
}

void
HdMoonray_Light::syncXform(const SdfPath& id,
                 HdSceneDelegate *sceneDelegate,
                 HdMoonray_RenderDelegate& renderDelegate)
{
    HdTimeSampleArray<GfMatrix4d, 4> sampledXforms;
    std::pair<float, float> shutterInterval = renderDelegate.scene().getTimeSamplingInterval();
    sceneDelegate->SampleTransform(id, shutterInterval.first, shutterInterval.second, &sampledXforms);
    // if there's only one sample, it should match the cached value   
    if (sampledXforms.count <= 1) {
        GfMatrix4d rdlXform0 = sampledXforms.values[0];
        fixLightXform(rdlXform0);
        mMoonrayLight.set("node_xform", rdlXform0);
    } else {
        // first and last samples will be sample interval boundaries
        GfMatrix4d rdlXform0 = sampledXforms.values[0];
        fixLightXform(rdlXform0);
        GfMatrix4d rdlXform1 = sampledXforms.values[sampledXforms.count-1];
        fixLightXform(rdlXform1);
        mMoonrayLight.set("node_xform", rdlXform0, rdlXform1);
   }
}

// If all the lights are off the renderDelegate has to turn the default dome light on
void
HdMoonray_Light::setOn(bool value, HdMoonray_RenderDelegate& renderDelegate) {
    if (value != mOn) {
        mOn = value;
        if (value)
            renderDelegate.scene().addLight();
        else
            renderDelegate.scene().removeLight();
        mMoonrayLight.set("on", value);
    }
}
void
HdMoonray_Light::resetLightObject(HdMoonray_RenderDelegate& renderDelegate)
{
    if (mMoonrayLight.isNull()) {
        return;
    }

    // RDL SceneObjects cannot be deleted from SceneContext during interactive
    // updates. Match the geometry lifecycle: make the old object inert and let
    // createSceneObject() reuse or class-suffix replacement objects as needed.
    UpdateGuard guard(renderDelegate, mMoonrayLight);
    setOn(false, renderDelegate);
    renderDelegate.scene().releaseCategory(mMoonrayLight.sceneObject(), LightCategory, mLightLinkCategory);
    renderDelegate.scene().releaseCategory(mMoonrayLight.sceneObject(), ShadowCategory, mShadowLinkCategory);
    mMoonrayLight = MoonrayObject();
    mLightLinkCategory = pxr::TfToken();
    mShadowLinkCategory = pxr::TfToken();
}

GfVec3f
colorTemperatureToRGB(float kelvin)
{
    float t = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
    float r, g, b;

    if (t <= 66.0f) {
        r = 1.0f;
        g = 0.39008157876901960784f * log(t) - 0.6318414437886274509f;
    } else {
        r = 1.29293618606274509804f * pow(t - 60.0f, -0.1332047592f);
        g = 1.12989086089529411765f * pow(t - 60.0f, -0.0755148492f);
    }

    if (t >= 66.0f)
        b = 1.0f;
    else if(t <= 19.0f)
        b = 0.0f;
    else
        b = 0.54320678911019607843f * log(t - 10.0f) - 1.19625408914f;

    return GfVec3f(std::clamp(r, 0.0f, 1.0f),
                        std::clamp(g, 0.0f, 1.0f),
                        std::clamp(b, 0.0f, 1.0f));
}

void
HdMoonray_Light::syncParams(const SdfPath& id,
                  HdSceneDelegate *sceneDelegate,
                  HdMoonray_RenderDelegate& renderDelegate)
{
    PrimAccess access(id, HdMoonrayTokens->moonray, sceneDelegate, renderDelegate);

    for (auto attrIt = mMoonrayLight.beginAttributes(); attrIt != mMoonrayLight.endAttributes(); ++attrIt) {
        const std::string& attrName = (*attrIt).name();

        // attributes directly updated by Sync():
        if (attrName == "on" || attrName == "node_xform" || attrName == "intensity") continue;

        if (attrName == "geometry") {
            VtValue relval = access.Get(HdMoonrayTokens->geometry);
            if (relval.IsHolding<VtArray<SdfPath>>()) {
                VtArray<SdfPath> paths = relval.UncheckedGet<VtArray<SdfPath>>();
                if (paths.size() > 0) {
                    SdfPath geomPath = paths.front();
                    geomPath.ReplacePrefix(SdfPath::AbsoluteRootPath(), sceneDelegate->GetDelegateID());
                    HdRprim* prim = const_cast<HdRprim*>(sceneDelegate->GetRenderIndex().GetRprim(geomPath));
                    HdMoonray_Mesh* mesh = dynamic_cast<HdMoonray_Mesh*>(prim);
                    if (mesh) {
                        // geometry sync should not be running in parallel with light sync
                        MoonrayObject geom = mesh->geometryForMeshLight(renderDelegate);
                        if (geom.isValid()) {
                            mMoonrayLight.set("geometry",geom);
                        }
                    } 
                }
            }
            continue;
        }

        // check if attr is set by "moonray:name" under light container
        std::string moonrayAttrName = "moonray:" + attrName;
        VtValue val = sceneDelegate->GetLightParamValue(id, TfToken(moonrayAttrName));
        if (!val.IsEmpty()) {
            (*attrIt).set(val);
            continue;
        }
        // if GetLightParamValue goes away, custom attrs will need to be set by "inputs:moonray:name",
        // since "moonray:name" will be inaccessible. Then we will use the material container,
        // but for now support this via GLPV
        moonrayAttrName = "inputs:moonray:" + attrName;
        val = sceneDelegate->GetLightParamValue(id, TfToken(moonrayAttrName));
        if (!val.IsEmpty()) {
            (*attrIt).set(val);
            continue;
        }


        // Find the equivalent Lux attribute
        static const std::map<std::string,TfToken> map = {
            { "color", HdLightTokens->color },
            //{ "intensity", HdLightTokens->intensity }, // handled in sync
            { "exposure", HdLightTokens->exposure },
            { "radius", HdLightTokens->radius },
            { "normalized", HdLightTokens->normalize },
            { "width", HdLightTokens->width },
            { "height", HdLightTokens->height },
            { "angular_extent", HdLightTokens->angle },
            { "texture", HdLightTokens->textureFile },
            { "lens_radius", HdLightTokens->radius },
            { "spread", HdLightTokens->shapingConeAngle },
            { "outer_cone_angle", HdLightTokens->shapingConeAngle },
            { "inner_cone_angle", HdLightTokens->shapingConeSoftness }
        };
        auto mapIt = map.find(attrName);
        if (mapIt != map.end()) {
            TfToken luxName = mapIt->second;
            if (attrName == "spread") {
                float coneAngle = 90; // USD default value
                val = sceneDelegate->GetLightParamValue(id, pxr::HdLightTokens->shapingConeAngle);
                if (val.IsHolding<float>()) coneAngle = val.UncheckedGet<float>();
                // MoonRay RectLight spread is the normalized cone angle:
                // 1 is a diffuse 90-degree cone and 0 is parallel emission.
                const float spread = std::clamp(coneAngle, 0.0f, 90.0f) / 90.0f;
                (*attrIt).set(spread);
                continue;

            } else if (luxName == HdLightTokens->shapingConeAngle) {
                float coneAngle = 90; // Lux default value
                val = sceneDelegate->GetLightParamValue(id, HdLightTokens->shapingConeAngle);
                if (val.IsHolding<float>()) coneAngle = val.UncheckedGet<float>();
                coneAngle = std::clamp(coneAngle, 0.0f, 180.0f);
                // USD shaping:cone:angle is an off-axis half angle; MoonRay
                // SpotLight outer_cone_angle is the full side-to-side apex.
                (*attrIt).set(2 * coneAngle);
                continue;

            } else if (luxName == HdLightTokens->shapingConeSoftness) {
                float softness = 0; // Lux default value
                val = sceneDelegate->GetLightParamValue(id, luxName);
                if (val.IsHolding<float>()) softness = val.UncheckedGet<float>();
                softness = std::clamp(softness, 0.0f, 1.0f);
                float coneAngle = 90; // Lux default value
                val = sceneDelegate->GetLightParamValue(id, HdLightTokens->shapingConeAngle);
                if (val.IsHolding<float>()) coneAngle = val.UncheckedGet<float>();
                coneAngle = std::clamp(coneAngle, 0.0f, 180.0f);
                // USD softness is the fraction of non-cutoff angles used for
                // falloff. MoonRay inner_cone_angle is the full bright apex.
                const float innerConeAngle = coneAngle * (1.0f - softness);
                (*attrIt).set(2 * innerConeAngle);
                continue;

            } else if (luxName == HdLightTokens->radius && mRectToSpotlight == true) {
                // If a rect light with shaping is converted to a spotlight, we approximate
                // the rect light area using the spotlight's lens_radius parameter.
                float width = 1.0f;
                val = sceneDelegate->GetLightParamValue(id, HdLightTokens->width);
                if (val.IsHolding<float>()) width = val.UncheckedGet<float>();
                float height = 1.0f;
                val = sceneDelegate->GetLightParamValue(id, HdLightTokens->height);
                if (val.IsHolding<float>()) height = val.UncheckedGet<float>();
                const float radius = std::sqrt((width * height) / M_PI);
                (*attrIt).set(radius);
                continue;
            } else if (luxName == HdLightTokens->color) {
                GfVec3f color(1.0f);
                val = sceneDelegate->GetLightParamValue(id, luxName);
                if (val.IsHolding<GfVec3f>()) color = val.UncheckedGet<GfVec3f>();
                val = sceneDelegate->GetLightParamValue(id, HdLightTokens->enableColorTemperature);
                if (val.IsHolding<bool>() && val.UncheckedGet<bool>()) {
                    val = sceneDelegate->GetLightParamValue(id, HdLightTokens->colorTemperature);
                    if (val.IsHolding<float>()) {
                        const float temperature = val.UncheckedGet<float>();

                        // This was originally using the function UsdLuxBlackbodyTemperatureAsRgb
                        // to do the conversion.   However there was a noticeable difference in the color
                        // compared to the Karma renderer.   To better match this we use the algorithm
                        // from the following site:
                        // https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html
                        GfVec3f  tempRgb = colorTemperatureToRGB(temperature);
                        color[0] *= tempRgb[0];
                        color[1] *= tempRgb[1];
                        color[2] *= tempRgb[2];
                    }
                }
                (*attrIt).setColor(color);
                continue;

            } else {
                // special case: Lux CylinderLight uses "length" where rdl2 uses "height"
                if (luxName == HdLightTokens->height && mType == HdPrimTypeTokens->cylinderLight) {
                    luxName = HdLightTokens->length;
                }
                // schema will cause correct default to be returned
                val = sceneDelegate->GetLightParamValue(id, luxName);
                if (!val.IsEmpty()) {
                    (*attrIt).set(val);
                    continue;
                }
            }
        }

        // no setting, so reset to default
        (*attrIt).setToDefault();
    }
}

void
HdMoonray_Light::syncFilterList(const SdfPath& id,
                      HdSceneDelegate *sceneDelegate,
                      HdMoonray_RenderDelegate& renderDelegate)
{
    VtValue val = sceneDelegate->GetLightParamValue(id, TfToken(HdTokens->filters));
    MoonrayObjectVector filters;
    if (!val.IsHolding<SdfPathVector>()) {
        mMoonrayLight.set("light_filters",filters);
        return;
    }    
    const SdfPathVector& paths = val.UncheckedGet<SdfPathVector>();
    for (const SdfPath& path : paths) {
        MoonrayObject filter = MoonrayLightFilter::getLightFilter(path, renderDelegate, sceneDelegate);
        if (filter.isValid()) {
            filters.append(filter);
        }
    }
    mMoonrayLight.set("light_filters",filters);
}

void
HdMoonray_Light::Sync(HdSceneDelegate *sceneDelegate,
            HdRenderParam   *renderParam,
            HdDirtyBits     *dirtyBits)
{
    SdfPath id = GetId();
    hdmLogSyncStart("Light", id, dirtyBits);

    HdMoonray_RenderDelegate& renderDelegate(HdMoonray_RenderDelegate::get(renderParam));

    // HDM-125: usdview sets the intensity of lights to 0.0f if "Enable Scene Lights" is turned off,
    // so treat intensity=0 as turning off the light. Also turn it off when lighting disabled.
    float intensity = 0.0f;
    if (not renderDelegate.options().getDisableLighting() && sceneDelegate->GetVisible(id)) {
        VtValue val = sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity);
        intensity = val.IsHolding<float>() ? val.UncheckedGet<float>() : 1.0f;
    }

    bool initialize = false;
    std::string rdlClass;
    if (mMoonrayLight.isValid() && ((*dirtyBits) & pxr::HdLight::DirtyParams)) {
        rdlClass = rdlClassName(id, sceneDelegate, renderDelegate);
        if (rdlClass != mMoonrayLight.className()) {
            resetLightObject(renderDelegate);
            *dirtyBits = pxr::HdLight::AllDirty;
        }
    }
    if (mMoonrayLight.isNull()) {
        *dirtyBits = DirtyBits::Clean; // don't call Sync again if no light is created
        if (not (intensity > 0)) return; // don't create invisible lights
        renderDelegate.scene().simplifyPath(id, sceneDelegate);
        if (rdlClass.empty()) {
            rdlClass = rdlClassName(id, sceneDelegate, renderDelegate);
        }
        
        mMoonrayLight = renderDelegate.scene().createObject(rdlClass, id);
        if (mMoonrayLight.isNull()) return; // if there was an error this already printed an error message
        initialize = true; // Force the catagories to be updated
        *dirtyBits = HdLight::AllDirty; // force other settings to be made
    }

    UpdateGuard guard(renderDelegate, mMoonrayLight);

    if ((*dirtyBits) & HdLight::DirtyTransform) {
        syncXform(id, sceneDelegate, renderDelegate);
    }

    if ((*dirtyBits) & HdLight::DirtyParams) {
        setOn(intensity > 0, renderDelegate);
        mMoonrayLight.set("intensity", intensity);
        syncParams(id, sceneDelegate, renderDelegate);
        syncFilterList(id, sceneDelegate, renderDelegate);
        // querying "lightLink" will return a token used to name the "category" that
        // holds all geometry that this light links to. This value will later be
        // returned as one of the entries if any of the linked geometry calls GetCategories().
        // In other words, the scene delegate does the reverse lookup "geometry->lights" for
        // us using an internal cache.
        // Value is either "" or "<id>.collection:lightLink" though this will allow others.
        // Currently Finalize() is called when value changes, but this may be a bug.
        bool categoriesChanged = false;
        TfToken t;
        VtValue v = sceneDelegate->GetLightParamValue(id, HdTokens->lightLink);
        if (v.IsHolding<TfToken>()) {
            t = v.UncheckedGet<TfToken>();
        }
        // registering the category id token with RenderDelegate will enable geometry
        // to look up mLight as the rdl2 scene object corresponding to this category id
        if (initialize || t != mLightLinkCategory) {
            if (!initialize) {
                renderDelegate.scene().releaseCategory(mMoonrayLight.sceneObject(), LightCategory, mLightLinkCategory);
            }
            renderDelegate.scene().setCategory(mMoonrayLight.sceneObject(), LightCategory, t);
            mLightLinkCategory = t;
            categoriesChanged = true;
        }
        // "shadowLink" is much the same
        t = TfToken();
        v = sceneDelegate->GetLightParamValue(id, HdTokens->shadowLink);
        if (v.IsHolding<TfToken>()) {
            t = v.UncheckedGet<TfToken>();
        }
        if (initialize || t != mShadowLinkCategory) {
            if (!initialize) {
                renderDelegate.scene().releaseCategory(mMoonrayLight.sceneObject(), ShadowCategory, mShadowLinkCategory);
            }
            renderDelegate.scene().setCategory(mMoonrayLight.sceneObject(), ShadowCategory, t);
            mShadowLinkCategory = t;
            categoriesChanged = true;
        }
        // Need to call Sync() on all geometry to get categories copied into LightSets
        if (categoriesChanged) {
            // in 0.22.5, DirtyCategories seems to be ignored. We can force a sync using
            // DirtyMaterialId even though it isn't strict;y right...
            sceneDelegate->GetRenderIndex().GetChangeTracker().MarkAllRprimsDirty(
                HdChangeTracker::DirtyCategories | HdChangeTracker::DirtyMaterialId);
        }
    }

    if (HdChangeTracker::IsInstancerDirty(*dirtyBits, id) ||
        HdChangeTracker::IsInstanceIndexDirty(*dirtyBits, id) ||
        HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _UpdateInstancer(sceneDelegate, dirtyBits);
        const SdfPath& instancerId = GetInstancerId();
        HdMoonray_Instancer* instancer = static_cast<HdMoonray_Instancer*>(
            sceneDelegate->GetRenderIndex().GetInstancer(instancerId));
        if (instancer) {
            instancer->makeInstanceLights(id, mMoonrayLight.sceneObject(), 0);
        }
    }

    *dirtyBits = DirtyBits::Clean;
    hdmLogSyncEnd(id);
}

void
HdMoonray_Light::Finalize(HdRenderParam *renderParam)
{
    resetLightObject(HdMoonray_RenderDelegate::get(renderParam));
}

}
