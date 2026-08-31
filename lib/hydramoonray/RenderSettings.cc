// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "RenderSettings.h"
#include "renderDelegate.h"
#include "ValueConverter.h"
#include "Utils.h"

// If adding or changing the descriptors, the file
// ../houdini/soho/parameters/HdMoonrayRendererPlugin_Viewport.ds must be updated to match

// Note: disableRender forces use of the null render delegate, and can only be set at delegate creation time
// generateOnly uses the regular delegate but skips the actual render, and can be set at any time.
using namespace pxr;
namespace {

TF_DEFINE_PRIVATE_TOKENS(Tokens,
    (debug)
    (info)
    (logLevel)
    (showRenderSettings)
    (showRenderPasses)
    (rdlOutput)
    (disableLighting)
    (doubleSided)
    (decodeNormals)
    (enableMotionBlur)
    (pruneWillow)
    (pruneFurDeform)
    (pruneCurveDeform)
    (pruneVolume)
    (pruneWrapDeform)
    (forcePolygon)
    (executionMode)
    (generateOnly)
    (maxMeshResolution)
);

}

namespace hdMoonray {

void
RenderSettings::addDescriptors(HdRenderSettingDescriptorList& descriptorList) const
{
    static HdRenderSettingDescriptorList descriptors = {

        { "Show Debug Messages",      Tokens->debug,               VtValue(getEnv("HDMOONRAY_DEBUG", false)) },
        { "Show Info Messages",       Tokens->info,                VtValue(getEnv("HDMOONRAY_INFO", false)) },
        { "Show Render Settings",     Tokens->showRenderSettings,  VtValue(getEnv("HDMOONRAY_SHOW_RENDER_SETTINGS", false)) },
        { "Show Render Passes",       Tokens->showRenderPasses,    VtValue(getEnv("HDMOONRAY_SHOW_RENDER_PASSES", false)) },
        { "Rdla output",              Tokens->rdlOutput,           VtValue(getEnv("HDMOONRAY_RDLA_OUTPUT","")) },
        { "DoubleSided",              Tokens->doubleSided,         VtValue(getEnv("HDMOONRAY_DOUBLESIDED", false)) },
        { "Maximum Mesh Resolution",  Tokens->maxMeshResolution,   VtValue(getEnv("HDMOONRAY_MAX_MESH_RESOLUTION", 0.0f)) },
        { "Decode Normals",           Tokens->decodeNormals,       VtValue(getEnv("HDMOONRAY_DOUBLESIDED", false)) },
        { "Enable Motion Blur",       Tokens->enableMotionBlur,    VtValue(getEnv("HDMOONRAY_ENABLE_MOTION_BLUR", true)) },
        { "Prune Willow",             Tokens->pruneWillow,         VtValue(getEnv("HDMOONRAY_PRUNE_WILLOW", false)) },
        { "Prune FurDeform",          Tokens->pruneFurDeform,      VtValue(getEnv("HDMOONRAY_PRUNE_FURDEFORM", false)) },
        { "Prune Volumes",            Tokens->pruneVolume,         VtValue(getEnv("HDMOONRAY_PRUNE_VOLUME", false)) },
        { "Prune WrapDeform",         Tokens->pruneWrapDeform,     VtValue(getEnv("HDMOONRAY_PRUNE_WRAPDEFORM", false)) },
        { "Prune CurveDeform",        Tokens->pruneCurveDeform,    VtValue(getEnv("HDMOONRAY_PRUNE_CURVEDEFORM", false)) },
        { "Force Polygon",            Tokens->forcePolygon,        VtValue(getEnv("HDMOONRAY_FORCE_POLYGON", false)) },
        { "Execution Mode",           Tokens->executionMode,       VtValue(getEnv("HDMOONRAY_EXEC_MODE", "auto")) },
        { "Generate Only",            Tokens->generateOnly,        VtValue(getEnv("HDMOONRAY_GENERATE_ONLY", false)) }
    };
    for (const auto& desc : descriptors) {
        descriptorList.push_back(desc);
    }
}
VtValue
RenderSettings::getRenderSetting(const TfToken& key) const
{
    return mDelegate.GetRenderSetting(key);
}

void RenderSettings::apply()
{
    bool info = get<bool>(Tokens->info);
    if (info) {
        Logger::setInfoLevel();
    }

    bool dbg = get<bool>(Tokens->debug);
    if (dbg) {
        Logger::setDebugLevel();
    }

    static const TfToken houdiniInteractive("houdini:interactive");
    VtValue val = mDelegate.GetRenderSetting(houdiniInteractive);
    mDelegate.options().setIsHoudini(not val.IsEmpty());

    // ---------------------------------------------------------------------------------
    // support render settings "moonray:sceneVariable:<name>" and "moonray:sceneVariable_<name>" for any
    // scene variable. The second form is used in the Houdini .ds file because it
    // doesn't like the colon character.

    // These are excluded...
    static const std::set<std::string> sDontWrite = {
        "camera", "motion_steps", "enable_motion_blur", "layer", "image_width", "image_height"
    };

    scene_rdl2::rdl2::SceneVariables& sv = mDelegate.acquireSceneContext().getSceneVariables();
    {
        UpdateGuard guard(sv);
        const SceneClass& sceneClass = sv.getSceneClass();
        for (auto it = sceneClass.beginAttributes(); it != sceneClass.endAttributes(); ++it) {

            const std::string& attrName = (*it)->getName();
            if (sDontWrite.count(attrName)) continue;

            TfToken key = TfToken("sceneVariable:" + attrName);
            VtValue val = mDelegate.GetRenderSetting(key);
            if (not val.IsEmpty()) {
                ValueConverter::setAttribute(&sv, *it, val);
            } else {
                key = TfToken("sceneVariable_" + attrName);
                val = mDelegate.GetRenderSetting(key);
                if (not val.IsEmpty()) {
                    ValueConverter::setAttribute(&sv, *it, val);
                }
            }
        }

        // apply any overrides from the render options
        sv.set(sv.sDebugKey, dbg);
        sv.set(sv.sInfoKey, info);

    }
    mDelegate.options().setRdlOutput(get<std::string>(Tokens->rdlOutput));
    mDelegate.options().setDoubleSided(get<bool>(Tokens->doubleSided));
    mDelegate.options().setDecodeNormals(get<bool>(Tokens->decodeNormals));
    mDelegate.options().setMaxMeshResolution(get<float>(Tokens->maxMeshResolution));
    mDelegate.options().setEnableMotionBlur(get<bool>(Tokens->enableMotionBlur));
    mDelegate.options().setPruneProcedural("WillowGeometry_v3", get<bool>(Tokens->pruneWillow));
    mDelegate.options().setPruneProcedural("FurDeformGeometry", get<bool>(Tokens->pruneFurDeform));
    mDelegate.options().setPruneProcedural("CurveDeformGeometry", get<bool>(Tokens->pruneCurveDeform));
    mDelegate.options().setPruneProcedural("WrapDeformGeometry", get<bool>(Tokens->pruneWrapDeform));
    mDelegate.options().setPruneVolume(get<bool>(Tokens->pruneVolume));
    mDelegate.options().setForcePolygon(get<bool>(Tokens->forcePolygon));
    mDelegate.options().setGenerateOnly(get<bool>(Tokens->generateOnly));

    setDeepIdAttributeName();

    bool showSettings = get<bool>(Tokens->showRenderSettings);
    if (showSettings && !mDelegate.options().getShowRenderSettingChanges()) {
        mDelegate.options().setShowAllRenderSettings(true);
    }
    mDelegate.options().setShowRenderSettingChanges(showSettings);

    mDelegate.options().setShowRenderPasses(get<bool>(Tokens->showRenderPasses));

    mDelegate.options().setSimplifyPaths(getEnv("HDMOONRAY_SIMPLIFY_PATHS", false));
}


std::string
RenderSettings::getExecutionMode() const
{
    VtValue val = mDelegate.GetRenderSetting(Tokens->executionMode);
    if (not val.IsEmpty()) {
        return val.Get<std::string>();
    } else {
        return "auto";
    }
}

void
RenderSettings::setDeepIdAttributeName(){
    TfToken key = TfToken("sceneVariable:deep_id_attribute_names");
    VtValue val = mDelegate.GetRenderSetting(key);
    if (val.IsHolding<pxr::VtArray<std::string>>()) {
        pxr::VtArray<std::string> names = val.UncheckedGet<pxr::VtArray<std::string>>();
        mDelegate.options().setDeepIdAttrName(names.front());
    }
}

// These settings can only be applied during initial setup
/*static*/ bool
RenderSettings::staticDisableRender(HdRenderSettingsMap const& settings)
{
    auto it = settings.find(TfToken("disableRender"));
    if (it != settings.end()) {
        return it->second.Get<bool>(); 
    }
    const char* v = std::getenv("HDMOONRAY_DISABLE_RENDER");
    if (v) {
        return *v && *v != '0' && *v != 'f' && *v != 'F';
    }
    return false;
}

/*static*/ uint32_t 
RenderSettings::staticThreads(HdRenderSettingsMap const& settings)
{
    auto it = settings.find(pxr::TfToken("threads"));
    if (it != settings.end()) {
            return it->second.Get<int>();
    }
    const char* v = std::getenv("HDMOONRAY_THREADS");
    if (v) {
        return int(strtol(v,0,0));
    }
    return 0;
}

} // namespace hdMoonray
