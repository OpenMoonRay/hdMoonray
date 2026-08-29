// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file hd_render.cc

// render usd scene using hydra

#include "FreeCamera.h"
#include "OutputFile.h"
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

//#define DEBUG_MSG

scene_rdl2::rec_time::RecTime recTime;
std::vector<std::pair<std::string,float>> stepTimes;

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
        std::cerr << "hd_render: preserving authored SceneVariables.scene_scale"
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
    std::cerr << "hd_render: using USD metersPerUnit "
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

// Begins a code section that can be traced
// using "-trace name"
void beginTrace(const hd_render::RenderOptions opts,
                const std::string& name)
{
    recTime.start();
    if (opts.hasTraceOpt(name)) {
        pxr::TraceCollector::GetInstance().SetEnabled(true);
    }
}

// Ends a code section that can be traced
void endTrace(const hd_render::RenderOptions opts,
              const std::string& name)
{
    stepTimes.emplace_back(name, recTime.end());

    if (opts.hasTraceOpt(name)) {
        pxr::TraceCollector::GetInstance().SetEnabled(false);
        std::cout << "Writing " << name << " trace to "
                  << opts.getTraceFile(name) << " ..." << std::endl;
        std::ofstream report(opts.getTraceFile(name));
        if (opts.useChromeTraceFormat()) {
            pxr::TraceReporter::GetGlobalReporter()->ReportChromeTracing(report);
        } else {
            pxr::TraceReporter::GetGlobalReporter()->Report(report);
        }
        std::cout << "...done" << std::endl;
        pxr::TraceCollector::GetInstance().Clear();
        pxr::TraceReporter::GetGlobalReporter()->ClearTree();
    }
}

void writeTiming(std::ostream &out)
{

}

int
main(int argc, char *argv[])
{
    // Parse arguments
    hd_render::RenderOptions options(argc, argv);
    if (options.helpRequested()) {
        std::cout << options.usage(argv[0]);
        return 0;
    } else if (!options.allFlagsValid()){
        std::cerr << "Invalid option\n";
        std::cerr << options.usage(argv[0]);
        return -1;
    }

    // Load the hydra renderer plugin and create the render delegate
    pxr::TfToken rendererId;
    pxr::HfPluginDescVector pluginDescriptors;
    pxr::HdRendererPluginRegistry &registry(pxr::HdRendererPluginRegistry::GetInstance());
    registry.GetPluginDescs(&pluginDescriptors);
    for (const pxr::HfPluginDesc &pluginDesc : pluginDescriptors) {
        // skip "GL" since we don't have OpenGL setup
        if (pluginDesc.displayName == "GL") continue;
        if (pluginDesc.displayName == options.getRenderer() ||
            pluginDesc.id == pxr::TfToken(options.getRenderer())) {
            rendererId = pluginDesc.id;
        }
    }
    if (rendererId.IsEmpty()) {
        const bool helpRequested = options.getRenderer().empty();
        std::ostream &ost = helpRequested ? std::cout : std::cerr;
        if (!helpRequested) {
            std::cerr << "Unable to load the requested renderer \"" << options.getRenderer() << "\"\n";
        }
        ost << "Available renderers include:\n";
        for (const pxr::HfPluginDesc &pluginDesc : pluginDescriptors) {
            // skip "GL" since we don't have OpenGL setup
            if (pluginDesc.displayName == "GL") continue;
            ost << "\t" << pluginDesc.displayName << '\n';
        }
        return options.getRenderer().empty() ? 0 : -1;
    }
    auto releasePlugin = [&](pxr::HdRendererPlugin *plugin) {
        registry.ReleasePlugin(plugin);
    };

    beginTrace(options,"load_plugin");
    Py_Initialize(); // HDM-133: plugin loader assumes this has been done
    pxr::HdRenderSettingsMap initialSettings;
    if (options.getDisableRender()) {
        initialSettings[pxr::TfToken("disableRender")] = true;
    }
    if (!options.getRdlOutput().empty()) {
        initialSettings[pxr::TfToken("rdlOutput")] = options.getRdlOutput();
    }
    std::unique_ptr<pxr::HdRendererPlugin, decltype(releasePlugin)>
        rendererPlugin(registry.GetRendererPlugin(rendererId), releasePlugin);
    MNRY_ASSERT_REQUIRE(rendererPlugin);
    std::unique_ptr<pxr::HdRenderDelegate>
        renderDelegate(rendererPlugin->CreateRenderDelegate(initialSettings));
    MNRY_ASSERT_REQUIRE(renderDelegate);
    endTrace(options,"load_plugin");

    // Create the hydra render index and tie the render delegate to it.
    // There is a 1-1 relationship between render delegate and render index.
    std::unique_ptr<pxr::HdRenderIndex>
#if PXR_VERSION >= 2005
        renderIndex(pxr::HdRenderIndex::New(renderDelegate.get(), {}));
#else
        renderIndex(pxr::HdRenderIndex::New(renderDelegate.get()));
#endif
    MNRY_ASSERT_REQUIRE(renderIndex);

    // Does the renderer know how to render the requested aov?
    const pxr::TfToken aov(options.getAov());
    const pxr::HdAovDescriptor aovDesc = renderDelegate->GetDefaultAovDescriptor(aov);
    if (aovDesc.format == pxr::HdFormatInvalid) {
        std::cerr << options.getRenderer() << " can't render aov " << aov << '\n';
        return -1;
    }

    // Get the available render settings
    hd_render::RenderSettings renderSettings(renderDelegate.get());
    if (options.getPrintRenderSettingsRequested()) {
        std::cout << "Avaiable " << options.getRenderer() << " render settings:\n";
        std::cout << renderSettings.printAvailableSettings();
        return -1;
    }
    for (const hd_render::RenderOptions::RenderSetting &setting : options.getRenderSettings()) {
        if (renderSettings.setRenderSetting(setting.first, setting.second)) {
            std::cerr << "Unable to set \"" << setting.first << "\" to \"" << setting.second << "\"\n";
            return -1;
        }
    }

    // At this point, we can check if all required options were passed
    if (!options.getMissingRequiredFlags().empty()) {
        for (const std::string &flag : options.getMissingRequiredFlags()) {
            std::cerr << flag << " is a required option\n";
        }
        std::cerr << options.usage(argv[0]);
        return -1;
    }

    // Fill out the hydra "purpose".  We always include the default purpose.  We support 3
    // other purposes: "render", "proxy", and "guide"
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

    // In hydra, there can be a many to one relationship between
    // scene delegates and the render index.  The only restriction is that
    // any primitive in the render index can be backed by only a single
    // scene delegate.  For the primitives that are in the input usd
    // file, we'll use a UsdImaging scene delegate.  We'll root
    // everything in the usd file at the path "/"
    const pxr::SdfPath usdSceneDelegateId = pxr::SdfPath::AbsoluteRootPath();
    pxr::UsdImagingDelegate usdSceneDelegate(renderIndex.get(), usdSceneDelegateId);

    pxr::UsdTimeCode timeCode = pxr::UsdTimeCode::Default();
    if (options.getTimeType() == hd_render::TimeType::Earliest) {
        timeCode = pxr::UsdTimeCode::EarliestTime();
    } else if (options.getTimeType() == hd_render::TimeType::DoubleValue) {
        timeCode = options.getTime();
    }
    usdSceneDelegate.SetTime(timeCode);

    // This is the value set by "Complexity/Medium" in usdview:
    usdSceneDelegate.SetRefineLevelFallback(options.getRefineLevel());

    usdSceneDelegate.SetWindowPolicy(pxr::CameraUtilMatchHorizontally);

    beginTrace(options,"open_stage");
    pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(options.getInputSceneFile());
    endTrace(options,"open_stage");

   if (!stage) {
        std::cerr << "Unable to open " << options.getInputSceneFile() << '\n';
        return -1;
    }
    const pxr::UsdPrim usdRenderSettings =
        findRenderSettings(stage, options.getRenderSettingsPath());
    applyStageSceneScale(renderDelegate.get(), stage, options.getInputSceneFile(), usdRenderSettings);
    const pxr::UsdPrim &pseudoRoot = stage->GetPseudoRoot();

    beginTrace(options,"populate");
    const pxr::SdfPathVector excludedRenderProductVarPaths =
        collectLegacyRenderProductVarExclusions(stage);
    usdSceneDelegate.Populate(pseudoRoot, excludedRenderProductVarPaths);
    endTrace(options,"populate");

    // Build an Rprim collection of the primitives we want to render
    // For now, we'll just grab all the rprims in the usd scene
    const pxr::TfToken collectionName(pxr::HdTokens->geometry); // collection name is "geometry"
    const pxr::HdReprSelector reprSelector(pxr::HdReprTokens->refined);
    pxr::HdRprimCollection collection(collectionName, reprSelector);
    collection.SetRootPath(pseudoRoot.GetPath());

    // Build a second scene delegate to manage the free camera,
    // render buffer and render task.  We'll root
    // everything that is app specific at "/app_scene"
    const pxr::SdfPath appSceneDelegateId =
        pxr::SdfPath::AbsoluteRootPath().AppendChild(pxr::TfToken("app_scene"));
    hd_render::SceneDelegate appSceneDelegate(renderIndex.get(), appSceneDelegateId);

    // image size is size / res
    unsigned int width = options.getWidth();
    unsigned int height = options.getHeight();
    if (!options.isSizeSpecified()) {
        applyRenderSettingsResolution(usdRenderSettings, timeCode, &width, &height);
    }
    const unsigned int resWidth = static_cast<unsigned int>(
        static_cast<float>(width) / options.getRes());
    const unsigned int resHeight = static_cast<unsigned int>(
        static_cast<float>(height) / options.getRes());

    const pxr::SdfPath cameraPath =
        resolveCameraPath(stage, usdRenderSettings, options.getCamera());
    if (cameraPath.IsEmpty()) {
        // Define a viewing frustum for the free camera
        // and populate the app scene delegate.
        hd_render::FreeCamera camera(stage,
                                     float(resHeight) / float(resWidth),
                                     timeCode,
                                     purposes);
        appSceneDelegate.populate(camera,
                                  resWidth, resHeight,
                                  collection,
                                  purposes,
                                  aov, aovDesc);
    } else {
        pxr::UsdGeomCamera usdCamera(stage->GetPrimAtPath(cameraPath));
        if (!usdCamera) {
            std::cerr << "Could not find camera " << cameraPath << " in " << stage << '\n';
            return -1;
        }
        hd_render::FreeCamera camera(usdCamera, timeCode);
        appSceneDelegate.populate(camera,
                                  resWidth, resHeight,
                                  collection,
                                  purposes,
                                  aov, aovDesc);
    }

    // Render
    pxr::HdEngine engine;
    pxr::HdTaskSharedPtrVector tasks = { renderIndex->GetTask(appSceneDelegate.getTaskId()) };
    pxr::HdxRenderTask *renderTask = static_cast<pxr::HdxRenderTask *>(tasks[0].get());
    MNRY_ASSERT_REQUIRE(renderTask != nullptr);

    beginTrace(options,"render");

    do {
        engine.Execute(renderIndex.get(), &tasks);
    } while (!renderTask->IsConverged());

    endTrace(options,"render");

    // Image output
    
    pxr::HdRenderBuffer *renderBuffer = dynamic_cast<pxr::HdRenderBuffer *>
            (renderIndex->GetBprim(pxr::HdPrimTypeTokens->renderBuffer, appSceneDelegate.getRenderBufferId()));

    hd_render::OutputFile outputFile(renderBuffer);
    if (!options.getDisableRender()) {
        if (outputFile.write(options.getOutputExrFile())) {
            std::cerr << "Failed to write " << options.getOutputExrFile() << '\n';
            return -1;
        }
        std::cout << "Wrote " << options.getOutputExrFile() << '\n';
    }

    // Now handle deltas, if requested.  If the delta usd file is non-empty
    // or the delta set options are non-empty, then the output delta file
    // must be non-empty, and we should process the requested delta.
    const bool doDelta = !options.getDeltaInputSceneFile().empty() 
                        || !options.getDeltaRenderSettings().empty();
    
    if (doDelta) {
        // First handle render settings
        for (hd_render::RenderOptions::RenderSetting setting : options.getDeltaRenderSettings()) {
            if (renderSettings.setRenderSetting(setting.first, setting.second)) {
                std::cerr << "Unable to set \"" << setting.first << "\" to \"" << setting.second << "\"\n";
                return -1;
            }
        }

        // Update the USD scene
        if (!options.getDeltaInputSceneFile().empty()) {
            // verify that we can open this file without error
            {
                pxr::UsdStageRefPtr deltaStage = pxr::UsdStage::Open(options.getDeltaInputSceneFile());
                if (!deltaStage) {
                    std::cerr << "Unable to open " << options.getDeltaInputSceneFile() << '\n';
                    return -1;
                }
            }
            beginTrace(options,"open_delta_stage");
            stage->GetSessionLayer()->InsertSubLayerPath(options.getDeltaInputSceneFile(), 0);
            usdSceneDelegate.ApplyPendingUpdates();
            endTrace(options,"open_delta_stage");
        }

        // Render
        beginTrace(options,"delta_render");
        do {
            engine.Execute(renderIndex.get(), &tasks);
        } while (!renderTask->IsConverged());
        endTrace(options,"delta_render");

        if (!options.getDisableRender() && !options.getDeltaOutputExrFile().empty()) {
        
            if (outputFile.write(options.getDeltaOutputExrFile())) {
                std::cerr << "Failed to write " << options.getDeltaOutputExrFile() << '\n';
                return -1;
            }
            std::cout << "Wrote " << options.getDeltaOutputExrFile() << '\n';
        }
    }

    if (options.isTimingEnabled()) {
        std::ostream* out;
        std::string timingFile = options.getTimingFile();
        if (timingFile.empty()) {
            out = &std::cout;
        } else {
            std::cout << "Writing timing to " << timingFile << std::endl;
            out = new std::ofstream(timingFile);
        }
        for (const auto& entry : stepTimes) {
            (*out) << entry.first << " " << entry.second << std::endl;
        }
        if (!timingFile.empty()) delete out;
    }
    return 0;
}
