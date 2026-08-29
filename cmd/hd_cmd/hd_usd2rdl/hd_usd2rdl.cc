// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file hd_usd2rdl.cc

// convert usd scene to rdl using hydra

#include "FreeCamera.h"
#include "RenderOptions.h"
#include "RenderSettings.h"
#include "SceneDelegate.h"

#include <scene_rdl2/common/platform/Platform.h>
#include <pxr/base/gf/frustum.h>
#include <pxr/base/tf/token.h>
#include <pxr/imaging/hd/engine.h>
#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>
#include <pxr/imaging/hdx/renderTask.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usdImaging/usdImaging/delegate.h>

#include <pxr/base/trace/collector.h>
#include <pxr/base/trace/reporter.h>
#include <fstream>

#include <scene_rdl2/common/rec_time/RecTime.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

pxr::TfToken sRendererId("HdMoonrayRendererDebugPlugin");
const char * const sAov = "color";

pxr::GfFrustum
computeFrustumToFrameStage(const pxr::UsdStageRefPtr stage, float aspectRatio,
    pxr::UsdTimeCode timeCode, const pxr::TfTokenVector &includedPurposes);

namespace {

pxr::SdfPathVector
collectLegacyRenderProductVarExclusions(const pxr::UsdStageRefPtr& stage)
{
    pxr::SdfPathVector excludedPaths;
    if (!stage) {
        return excludedPaths;
    }

    const pxr::TfToken renderProductType("RenderProduct");
    const pxr::TfToken renderVarType("RenderVar");
    for (const pxr::UsdPrim& prim : stage->Traverse()) {
        const pxr::TfToken typeName = prim.GetTypeName();
        if (typeName == renderProductType || typeName == renderVarType) {
            excludedPaths.push_back(prim.GetPath());
        }
    }
    return excludedPaths;
}

const pxr::TfToken sceneScaleToken("moonray:sceneVariable:scene_scale");
const pxr::TfToken houdiniSceneScaleToken("sceneVariable_scene_scale");
const pxr::TfToken usdFilenameToken("usdFilename");

bool
getAuthoredSceneScale(const pxr::UsdPrim& settings, pxr::VtValue* value)
{
    if (!settings) {
        return false;
    }
    for (const pxr::TfToken& token : { sceneScaleToken, houdiniSceneScaleToken }) {
        pxr::UsdAttribute attr = settings.GetAttribute(token);
        if (attr && attr.HasAuthoredValueOpinion() && attr.Get(value)) {
            return true;
        }
    }
    return false;
}

void
applyStageSceneScale(pxr::HdRenderDelegate* renderDelegate,
                     const pxr::UsdStageRefPtr& stage,
                     const std::string& inputSceneFile,
                     const pxr::UsdPrim& settings)
{
    if (!renderDelegate || !stage) {
        return;
    }

    renderDelegate->SetRenderSetting(usdFilenameToken, pxr::VtValue(inputSceneFile));
    pxr::VtValue authoredSceneScale;
    if (getAuthoredSceneScale(settings, &authoredSceneScale)) {
        renderDelegate->SetRenderSetting(sceneScaleToken, authoredSceneScale);
        std::cerr << "hd_usd2rdl: preserving authored SceneVariables.scene_scale"
                  << std::endl;
        return;
    }
    if (!renderDelegate->GetRenderSetting(sceneScaleToken).IsEmpty() ||
        !renderDelegate->GetRenderSetting(houdiniSceneScaleToken).IsEmpty()) {
        return;
    }

    const double metersPerUnit = pxr::UsdGeomGetStageMetersPerUnit(stage);
    renderDelegate->SetRenderSetting(
            sceneScaleToken, pxr::VtValue(static_cast<float>(metersPerUnit)));
    std::cerr << "hd_usd2rdl: using USD metersPerUnit "
              << metersPerUnit
              << " for SceneVariables.scene_scale"
              << std::endl;
}

pxr::SdfPath
pathFromOption(const std::string& value)
{
    if (value.empty()) {
        return pxr::SdfPath();
    }
    pxr::SdfPath path(value);
    if (path.IsAbsolutePath()) {
        return path;
    }
    if (path.IsPrimPath()) {
        return pxr::SdfPath::AbsoluteRootPath().AppendPath(path);
    }
    return pxr::SdfPath();
}

bool
isRenderSettingsPrim(const pxr::UsdPrim& prim)
{
    return prim && prim.GetTypeName() == pxr::TfToken("RenderSettings");
}

pxr::UsdPrim
findRenderSettings(const pxr::UsdStageRefPtr& stage, const std::string& requestedPath)
{
    if (!stage) {
        return pxr::UsdPrim();
    }
    const pxr::SdfPath explicitPath = pathFromOption(requestedPath);
    if (!explicitPath.IsEmpty()) {
        pxr::UsdPrim prim = stage->GetPrimAtPath(explicitPath);
        return isRenderSettingsPrim(prim) ? prim : pxr::UsdPrim();
    }
    pxr::UsdPrim settings = stage->GetPrimAtPath(pxr::SdfPath("/Render/rendersettings"));
    if (isRenderSettingsPrim(settings)) {
        return settings;
    }
    for (const pxr::UsdPrim& prim : stage->Traverse()) {
        if (isRenderSettingsPrim(prim)) {
            return prim;
        }
    }
    return pxr::UsdPrim();
}

pxr::SdfPath
firstRelationshipTarget(const pxr::UsdRelationship& rel)
{
    pxr::SdfPathVector targets;
    if (rel && rel.GetTargets(&targets) && !targets.empty()) {
        return targets.front();
    }
    return pxr::SdfPath();
}

pxr::UsdPrim
firstRenderProduct(const pxr::UsdPrim& settings)
{
    if (!settings) {
        return pxr::UsdPrim();
    }
    pxr::SdfPath productPath = firstRelationshipTarget(
        settings.GetRelationship(pxr::TfToken("products")));
    if (!productPath.IsEmpty() && settings.GetStage()) {
        pxr::UsdPrim product = settings.GetStage()->GetPrimAtPath(productPath);
        if (product && product.GetTypeName() == pxr::TfToken("RenderProduct")) {
            return product;
        }
    }
    return pxr::UsdPrim();
}

bool
readResolution(const pxr::UsdPrim& prim,
               pxr::UsdTimeCode timeCode,
               unsigned int* width,
               unsigned int* height)
{
    pxr::GfVec2i resolution(0, 0);
    if (prim && prim.GetAttribute(pxr::TfToken("resolution")).Get(&resolution, timeCode) &&
        resolution[0] > 0 && resolution[1] > 0) {
        *width = static_cast<unsigned int>(resolution[0]);
        *height = static_cast<unsigned int>(resolution[1]);
        return true;
    }
    return false;
}

void
applyRenderSettingsResolution(const pxr::UsdPrim& settings,
                              pxr::UsdTimeCode timeCode,
                              unsigned int* width,
                              unsigned int* height)
{
    if (!settings) {
        return;
    }
    pxr::UsdPrim product = firstRenderProduct(settings);
    if (product && readResolution(product, timeCode, width, height)) {
        return;
    }
    readResolution(settings, timeCode, width, height);
}

pxr::SdfPath
findCameraByName(const pxr::UsdStageRefPtr& stage, const std::string& name)
{
    pxr::SdfPath match;
    for (const pxr::UsdPrim& prim : stage->Traverse()) {
        if (!pxr::UsdGeomCamera(prim)) {
            continue;
        }
        const std::string primName = prim.GetName().GetString();
        const std::string path = prim.GetPath().GetString();
        const std::string relativePath = path.size() > 1 && path[0] == '/' ? path.substr(1) : path;
        if (name == primName || name == path || name == relativePath) {
            if (!match.IsEmpty()) {
                return pxr::SdfPath();
            }
            match = prim.GetPath();
        }
    }
    return match;
}

pxr::SdfPath
resolveCameraPath(const pxr::UsdStageRefPtr& stage,
                  const pxr::UsdPrim& settings,
                  const std::string& requestedCamera)
{
    if (!requestedCamera.empty()) {
        const pxr::SdfPath explicitPath = pathFromOption(requestedCamera);
        if (!explicitPath.IsEmpty() && pxr::UsdGeomCamera(stage->GetPrimAtPath(explicitPath))) {
            return explicitPath;
        }
        return findCameraByName(stage, requestedCamera);
    }
    if (settings) {
        pxr::UsdPrim product = firstRenderProduct(settings);
        pxr::SdfPath productCamera = product ? firstRelationshipTarget(
                                                  product.GetRelationship(pxr::TfToken("camera")))
                                             : pxr::SdfPath();
        if (!productCamera.IsEmpty()) {
            return productCamera;
        }
        return firstRelationshipTarget(settings.GetRelationship(pxr::TfToken("camera")));
    }
    return pxr::SdfPath();
}

} // namespace

int
main(int argc, char *argv[])
{
    // Parse arguments
    hd_usd2rdl::RenderOptions options(argc, argv);
    if (options.helpRequested()) {
        std::cout << options.usage(argv[0]);
        return 0;
    } else if (!options.allFlagsValid()){
        std::cerr << "Invalid option\n";
        std::cerr << options.usage(argv[0]);
        return -1;
    } else if (!options.getMissingRequiredFlags().empty()) {
        for (const std::string &flag : options.getMissingRequiredFlags()) {
            std::cerr << flag << " is a required option\n";
        }
        std::cerr << options.usage(argv[0]);
        return -1;
    }

    // Load the hydra renderer plugin and create the render delegate
    pxr::HdRendererPluginRegistry &registry(pxr::HdRendererPluginRegistry::GetInstance());
    
    auto releasePlugin = [&](pxr::HdRendererPlugin *plugin) {
        registry.ReleasePlugin(plugin);
    };

    Py_Initialize(); // HDM-133: plugin loader assumes this has been done
    pxr::HdRenderSettingsMap initialSettings;
    initialSettings[pxr::TfToken("disableRender")] = true;
    initialSettings[pxr::TfToken("rdlOutput")] = options.getOutputRdlFile();
    
    std::unique_ptr<pxr::HdRendererPlugin, decltype(releasePlugin)>
        rendererPlugin(registry.GetRendererPlugin(sRendererId), releasePlugin);
    MNRY_ASSERT_REQUIRE(rendererPlugin);
    std::unique_ptr<pxr::HdRenderDelegate>
        renderDelegate(rendererPlugin->CreateRenderDelegate(initialSettings));
    MNRY_ASSERT_REQUIRE(renderDelegate);

    std::unique_ptr<pxr::HdRenderIndex>
#if PXR_VERSION >= 2005
        renderIndex(pxr::HdRenderIndex::New(renderDelegate.get(), {}));
#else
        renderIndex(pxr::HdRenderIndex::New(renderDelegate.get()));
#endif
    MNRY_ASSERT_REQUIRE(renderIndex);

    const pxr::TfToken aov(sAov);
    const pxr::HdAovDescriptor aovDesc = renderDelegate->GetDefaultAovDescriptor(aov);
    if (aovDesc.format == pxr::HdFormatInvalid) {
        std::cerr << "Can't render aov " << aov << '\n';
        return -1;
    }

    // Get the available render settings
    hd_usd2rdl::RenderSettings renderSettings(renderDelegate.get());
    if (options.getPrintRenderSettingsRequested()) {
        std::cout << "Available render settings:\n";
        std::cout << renderSettings.printAvailableSettings();
        return -1;
    }
    for (const hd_usd2rdl::RenderOptions::RenderSetting &setting : options.getRenderSettings()) {
        if (renderSettings.setRenderSetting(setting.first, setting.second)) {
            std::cerr << "Unable to set \"" << setting.first << "\" to \"" << setting.second << "\"\n";
            return -1;
        }
    }
    renderSettings.setRenderSetting("disableRender","true");
    renderSettings.setRenderSetting("rdlaOutput",options.getOutputRdlFile());

    pxr::TfTokenVector purposes = { pxr::UsdGeomTokens->default_ };
    for (const std::string& p : options.getPurposes()) {
        const pxr::TfToken tp = pxr::TfToken(p);
        if (tp == pxr::UsdGeomTokens->render ||
            tp == pxr::UsdGeomTokens->proxy ||
            tp == pxr::UsdGeomTokens->guide) {
            purposes.push_back(tp);
        } else {
            std::cerr << "Unsupported purpose " << p << '\n';
            return -1;
        }
    }

    const pxr::SdfPath usdSceneDelegateId = pxr::SdfPath::AbsoluteRootPath();
    pxr::UsdImagingDelegate usdSceneDelegate(renderIndex.get(), usdSceneDelegateId);

    pxr::UsdTimeCode timeCode = pxr::UsdTimeCode::Default();
    if (options.getTimeType() == hd_usd2rdl::TimeType::Earliest) {
        timeCode = pxr::UsdTimeCode::EarliestTime();
    } else if (options.getTimeType() == hd_usd2rdl::TimeType::DoubleValue) {
        timeCode = options.getTime();
    }
    usdSceneDelegate.SetTime(timeCode);

    // This is the value set by "Complexity/Medium" in usdview:
    usdSceneDelegate.SetRefineLevelFallback(options.getRefineLevel());

    usdSceneDelegate.SetWindowPolicy(pxr::CameraUtilMatchHorizontally);

    pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(options.getInputSceneFile());

   if (!stage) {
        std::cerr << "Unable to open " << options.getInputSceneFile() << '\n';
        return -1;
    }
    const pxr::UsdPrim usdRenderSettings =
        findRenderSettings(stage, options.getRenderSettingsPath());
    applyStageSceneScale(renderDelegate.get(), stage, options.getInputSceneFile(), usdRenderSettings);
    const pxr::UsdPrim &pseudoRoot = stage->GetPseudoRoot();

    const pxr::SdfPathVector excludedRenderProductVarPaths =
        collectLegacyRenderProductVarExclusions(stage);
    usdSceneDelegate.Populate(pseudoRoot, excludedRenderProductVarPaths);

    const pxr::TfToken collectionName(pxr::HdTokens->geometry); // collection name is "geometry"
    const pxr::HdReprSelector reprSelector(pxr::HdReprTokens->refined);
    pxr::HdRprimCollection collection(collectionName, reprSelector);
    collection.SetRootPath(pseudoRoot.GetPath());

    const pxr::SdfPath appSceneDelegateId =
        pxr::SdfPath::AbsoluteRootPath().AppendChild(pxr::TfToken("app_scene"));
    hd_usd2rdl::SceneDelegate appSceneDelegate(renderIndex.get(), appSceneDelegateId);

    unsigned int width = options.getWidth();
    unsigned int height = options.getHeight();
    if (!options.isSizeSpecified()) {
        applyRenderSettingsResolution(usdRenderSettings, timeCode, &width, &height);
    }

    const pxr::SdfPath cameraPath =
        resolveCameraPath(stage, usdRenderSettings, options.getCamera());
    if (cameraPath.IsEmpty()) {
        // Define a free camera
        // and populate the app scene delegate.
        hd_usd2rdl::FreeCamera camera(stage,
                                      float(height) / float(width),
                                      timeCode,
                                      purposes);
        appSceneDelegate.populate(camera,
                                  width, height,
                                  collection,
                                  purposes,
                                  aov, aovDesc);
    } else {
        pxr::UsdGeomCamera usdCamera(stage->GetPrimAtPath(cameraPath));
        if (!usdCamera) {
            std::cerr << "Could not find camera " << cameraPath << " in " << stage << '\n';
            return -1;
        }
        hd_usd2rdl::FreeCamera camera(usdCamera, timeCode);
        appSceneDelegate.populate(camera,
                                  width, height,
                                  collection,
                                  purposes,
                                  aov, aovDesc);
    }

    // Render
    pxr::HdEngine engine;
    pxr::HdTaskSharedPtrVector tasks = { renderIndex->GetTask(appSceneDelegate.getTaskId()) };
    pxr::HdxRenderTask *renderTask = static_cast<pxr::HdxRenderTask *>(tasks[0].get());
    MNRY_ASSERT_REQUIRE(renderTask != nullptr);

    do {
        engine.Execute(renderIndex.get(), &tasks);
    } while (!renderTask->IsConverged());

    return 0;
}
