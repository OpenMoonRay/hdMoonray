// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayScene.h"
#include "Renderer.h"
#include "renderDelegate.h"
#include "hydra2_utils.h"
#include "tokens.h"

#include <pxr/imaging/hd/primOriginSchema.h>
#include <pxr/imaging/hd/instancerTopologySchema.h>

#include <scene_rdl2/scene/rdl2/GeometrySet.h>
#include <scene_rdl2/scene/rdl2/Layer.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <scene_rdl2/scene/rdl2/Light.h>
#include <scene_rdl2/scene/rdl2/LightFilter.h>
#include <scene_rdl2/scene/rdl2/LightFilterSet.h>
#include <scene_rdl2/scene/rdl2/LightSet.h>
#include <scene_rdl2/scene/rdl2/RenderOutput.h>
#include <scene_rdl2/scene/rdl2/Map.h>
#include <scene_rdl2/scene/rdl2/Material.h>
#include <scene_rdl2/scene/rdl2/ShadowSet.h>
#include <scene_rdl2/scene/rdl2/VolumeShader.h>


using namespace pxr;
using namespace scene_rdl2;

namespace {

    // Testing is much easier if the names of code-generated objects are 
    // consistent from run to run. Given the nature of Hydra, 
    // we can't rely on objects to always be processed in the same order.
    // Therefore we have to name them using a content hash, rather than 
    // sequentially. The hash algorithm has to produce the same value
    // from run-to-run, with collisions very unlikely.
    constexpr size_t HASH_M = 2<<24 - 3; // prime, fits in 6 hex digits
    constexpr size_t HASH_P = 127; 

    size_t repeatableStringHash(const std::string& s)
    {
        // standard polynomial rolling algoritm
        size_t hash = 0;
        size_t p = 1;
        for (unsigned char c : s) {
            hash = (hash + c * p) % HASH_M;
            p = (p * HASH_P) % HASH_M;
        }
        return hash;
    }

    unsigned hashObjectSet(std::set<hdMoonray::MoonrayObject> objSet)
    {
        unsigned hash = 0;
        for (auto& obj : objSet)
            hash += repeatableStringHash(obj.objectName());
        return hash;
    }
}

namespace hdMoonray {

MoonrayScene::MoonrayScene(HdMoonray_RenderDelegate& renderDelegate,
                         Renderer* renderer) 
    : mRenderDelegate(renderDelegate),
      mRenderer(renderer)
{
}

MoonrayScene::~MoonrayScene() 
{
}

// read only access to the scene context
const rdl2::SceneContext& 
MoonrayScene::sceneContext() 
{ 
    return mRenderer->getSceneContext(); 
}

// we must notify the renderer before modifying the scene context, since an in-process renderer may need to stop rendering.
void 
MoonrayScene::beginUpdate() 
{ 
    mRenderer->beginUpdate(); 
}

rdl2::SceneContext& 
MoonrayScene::acquireSceneContext() 
{ 
    mRenderer->beginUpdate(); 
    return mRenderer->getSceneContext(); 
}

rdl2::SceneObject*
MoonrayScene::create(const std::string& className, const SdfPath& id, const std::string& suffix)
{
    const SdfPath simplePath = getSimplePath(id);
    std::string rdlName = simplePath.GetString() + suffix;
    try {
        return acquireSceneContext().createSceneObject(className, rdlName);
    } catch (const except::TypeError& e) {
        // assume this error is a className collision, try again with a different name
        Logger::info(e.what());
        return create(className, id, suffix + "_" + className);
    } catch (const std::exception& e) {
        Logger::error(rdlName, ": ", e.what());
        return nullptr;
    }
}

MoonrayOutput
MoonrayScene::createRenderOutput(const std::string& name)
{
    try {
        rdl2::RenderOutput* ro = acquireSceneContext().createSceneObject("RenderOutput", name)->asA<rdl2::RenderOutput>();
        return MoonrayOutput(ro);
    } catch (const std::exception& e) {
        Logger::error(name, ": ", e.what());
        return MoonrayOutput();
    }
}

rdl2::SceneObject*
MoonrayScene::get(const SdfPath& id, const std::string& suffix)
{
    const SdfPath simplePath = getSimplePath(id);
    std::string rdlName = simplePath.GetString() + suffix;
    try {
        return sceneContext().getSceneObject(rdlName);
    } catch (const std::exception& e) {
        // caller can print a more informative error message
        return nullptr;
    }
}
bool
MoonrayScene::checkClassInterface(const std::string& className, MoonrayAttribute::InterfaceType interfaceType)
{
    if (className.empty()) {
        return false;
    }
    try {
        const rdl2::SceneClass* sceneClass = acquireSceneContext().createSceneClass(className);
        return sceneClass && (sceneClass->getDeclaredInterface() & interfaceType);
    } catch (const std::exception&) {
        return false;
    }
}

void
MoonrayScene::assign(const MoonrayObject& obj,
                       const MoonrayAssignment& assignment)
{
    static const std::string nopart;
    assign(obj, nopart, assignment);
}

void
MoonrayScene::assign(const MoonrayObject& obj, const std::string& partName,
                     const MoonrayAssignment& assignment)
{
    std::lock_guard<std::mutex> guard(mLayerMutex);
    if (partName.empty()) {
        mAllGeometry.beginUpdate();
        mAllGeometry.sceneObjectAs<rdl2::GeometrySet>()->add(obj.sceneObjectAs<rdl2::Geometry>());
        mAllGeometry.endUpdate();
    }
    mDefaultLayer.beginUpdate();
    mDefaultLayer.assign(obj, partName, assignment);
    mDefaultLayer.endUpdate();
}

void
MoonrayScene::addUnassigned(const MoonrayObject& obj)
{
    std::lock_guard<std::mutex> guard(mLayerMutex);
    mAllGeometry.beginUpdate();
    mAllGeometry.sceneObjectAs<rdl2::GeometrySet>()->add(obj.sceneObjectAs<rdl2::Geometry>());
    mAllGeometry.endUpdate();
}

void
MoonrayScene::setCategory(const MoonrayObject& obj,
                          CategoryType type,
                          const TfToken& category)
{
    std::lock_guard<std::mutex> lock(mCategoriesMutex);
    mCategoryContents[type][category].emplace(obj);
}

void
MoonrayScene::releaseCategory(const MoonrayObject& obj,
                              CategoryType type,
                              const TfToken& category)
{
    std::lock_guard<std::mutex> lock(mCategoriesMutex);
    mCategoryContents[type][category].erase(obj);
}

void
MoonrayScene::addCategoryContents(const TfToken& category, CategoryType type, std::set<MoonrayObject>& contents)
{
    auto i = mCategoryContents[type].find(category);
    if (i != mCategoryContents[type].end()) {
        for (const MoonrayObject& item : i->second)
            contents.insert(item);
    }
}

void
MoonrayScene::removeCategoryContents(const TfToken& category, CategoryType type, std::set<MoonrayObject>& contents)
{
    auto i = mCategoryContents[type].find(category);
    if (i != mCategoryContents[type].end()) {
        for (const MoonrayObject& item : i->second)
            contents.erase(item);
    }
}

void
MoonrayScene::updateAssignmentFromCategories(
                    MoonrayAssignment& assignment,
                    const VtArray<TfToken>& categories)
{

    // Lock is not needed here as it appears Hydra has already called Sync() on all lights and filters

    std::set<MoonrayObject> contents;

    createDefaultLightIfNeeded();

    addCategoryContents(defaultCategory, LightCategory, contents);
    for (auto& category : categories) {
        addCategoryContents(category, LightCategory, contents);
    }
    assignment.lightSet = getLightSet(contents);

    // shadow set is inverted
    removeCategoryContents(defaultCategory, ShadowCategory, contents);
    for (auto& category : categories) {
        removeCategoryContents(category, ShadowCategory, contents);
    }
    assignment.shadowSet = getShadowSet(contents);

    contents.clear();
    addCategoryContents(defaultCategory, FilterCategory, contents);
    for (auto& category : categories) {
        addCategoryContents(category, FilterCategory, contents);
    }
    assignment.lightFilterSet = getLightFilterSet(contents);
}

void
MoonrayScene::createDefaultLightIfNeeded()
{
    if (not mNumLights && mDefaultLight.isNull()) {        
        std::lock_guard<std::mutex> lock(mCreateMutex);
        if (mDefaultLight.isNull()) {
            rdl2::SceneContext& wsc = acquireSceneContext();
            rdl2::Light* defaultLight = wsc.createSceneObject("EnvLight", "/DEFAULT/defaultLight")->asA<rdl2::Light>();
            mDefaultLight = MoonrayObject(defaultLight);
            setCategory(mDefaultLight, LightCategory, pxr::TfToken());
            setCategory(mDefaultLight, ShadowCategory, pxr::TfToken());
            mDefaultLight.beginUpdate();
            mDefaultLight.set("max_shadow_distance", 100.0f);
            mDefaultLight.endUpdate();
           
        }
    }
}

void 
MoonrayScene::addLight()
{
    std::lock_guard<std::mutex> lock(mCategoriesMutex); 
    if (++mNumLights == 1 && mDefaultLight.isValid()) {
        // turn off default light as soon as a real light is added
        mDefaultLight.beginUpdate();
        mDefaultLight.set("on", false);
        mDefaultLight.endUpdate();
    }
}

void 
MoonrayScene::removeLight()
{
    std::lock_guard<std::mutex> lock(mCategoriesMutex);
    if (--mNumLights == 0) { 
        if (mDefaultLight.isValid()) {
            // turn on default light when last real light is removed
            mDefaultLight.beginUpdate();
            mDefaultLight.set("on", true);
            mDefaultLight.endUpdate();
        } else {
            // this will cause updateAssignmentFromCategories to create mDefaultLight:
            mRenderDelegate.markAllRprimsDirty(pxr::HdChangeTracker::DirtyCategories);
        }
    }
}

void
MoonrayScene::initialize()
{
    rdl2::SceneContext& wsc = acquireSceneContext();

    mAllGeometry = MoonrayObject(wsc.createSceneObject("GeometrySet", "/DEFAULT/allGeometry"));
    mDefaultLayer = MoonrayObject(wsc.createSceneObject("Layer", "/DEFAULT/defaultLayer"));
    rdl2::SceneVariables& wsv = wsc.getSceneVariables();
    wsv.beginUpdate();
    wsv.set(wsv.sLayer, mDefaultLayer.sceneObjectAs<rdl2::Layer>());
    wsv.endUpdate();

    mPrimaryCamera = MoonrayObject(wsc.createSceneObject("PerspectiveCamera", "/DEFAULT/primaryCamera"));
}

MoonrayObject
MoonrayScene::defaultMaterial()
{
    if (mDefaultMaterial.isNull()) {
        std::lock_guard<std::mutex> lock(mCreateMutex);
        if (mDefaultMaterial.isNull()) {
            rdl2::SceneContext& wsc = acquireSceneContext();

            // The default material is a UsdPreviewSurface with displayColor and displayOpacity bound to attribute maps
            rdl2::Map* displayColor = wsc.createSceneObject("AttributeMap", "/DEFAULT/displayColorMap")->asA<rdl2::Map>();
            displayColor->beginUpdate();
            displayColor->set("primitive_attribute_name", std::string("displayColor"));
            displayColor->set("default_value", rdl2::Rgb(0.5f,0.5f,0.5f)); // default from hdSt/shaders/mesh.glslfx
            displayColor->endUpdate();
            
            rdl2::Map* displayOpacity = wsc.createSceneObject("AttributeMap", "/DEFAULT/displayOpacityMap")->asA<rdl2::Map>();
            displayOpacity->beginUpdate();
            displayOpacity->set("primitive_attribute_name", std::string("displayOpacity"));
            displayOpacity->set("primitive_attribute_type", 0); // FLOAT
            displayOpacity->endUpdate();

            rdl2::SceneObject* obj = wsc.createSceneObject("UsdPreviewSurface", "/DEFAULT/defaultMaterial")->asA<rdl2::Material>();
            obj->beginUpdate();
            obj->set("diffuseColor", rdl2::Rgb(1.0f,1.0f,1.0f));
            obj->setBinding("diffuseColor", displayColor);
            obj->setBinding("opacity", displayOpacity);
            obj->set("roughness", 0.3f);
            obj->endUpdate();
            mDefaultMaterial = MoonrayObject(obj);
        }
    }
    return mDefaultMaterial;
}

MoonrayObject
MoonrayScene::errorMaterial()
{
    if (mErrorMaterial.isNull()) {
        std::lock_guard<std::mutex> lock(mCreateMutex);
        if (mErrorMaterial.isNull()) {
            rdl2::SceneContext& wsc = acquireSceneContext();

            // The error material is a UsdPreviewSurface with a bright magenta color
            rdl2::SceneObject* obj = wsc.createSceneObject("UsdPreviewSurface", "/DEFAULT/errorMaterial")->asA<rdl2::Material>();
            obj->beginUpdate();
            obj->set("diffuseColor", rdl2::Rgb(1.0f, 0.0f, 1.0f));
            obj->endUpdate();
            mErrorMaterial = MoonrayObject(obj);
        }
    }
    return mErrorMaterial;
}

MoonrayObject
MoonrayScene::defaultVolumeShader()
{
    if (mDefaultVolumeShader.isNull()) {
        std::lock_guard<std::mutex> lock(mCreateMutex);
        if (mDefaultVolumeShader.isNull()) {
            rdl2::SceneContext& wsc = acquireSceneContext();
            rdl2::SceneObject* obj = wsc.createSceneObject("BaseVolume", "/DEFAULT/defaultVolumeShader")->asA<rdl2::VolumeShader>();
            mDefaultVolumeShader = MoonrayObject(obj);
        }
    }
    return mDefaultVolumeShader;
}

MoonrayObject
MoonrayScene::emptyLightSet()
{
    if (mEmptyLightSet.isNull()) {
        std::lock_guard<std::mutex> lock(mCreateMutex);
        if (mEmptyLightSet.isNull()) {
            rdl2::SceneContext& wsc = acquireSceneContext();
            rdl2::SceneObject* obj = wsc.createSceneObject("LightSet", "/DEFAULT/emptyLightSet")->asA<rdl2::LightSet>();
            mEmptyLightSet = MoonrayObject(obj);
        }
    }
    return mEmptyLightSet;
}

MoonrayObject
MoonrayScene::emptyLightFilterSet()
{
    if (mEmptyLightFilterSet.isNull()) {
        std::lock_guard<std::mutex> lock(mCreateMutex);
        if (mEmptyLightFilterSet.isNull()) {
            rdl2::SceneContext& wsc = acquireSceneContext();
            rdl2::SceneObject* obj = wsc.createSceneObject("LightFilterSet", "/DEFAULT/emptyLightFilterSet")->asA<rdl2::LightFilterSet>();
            mEmptyLightFilterSet = MoonrayObject(obj);
        }
    }
    return mEmptyLightFilterSet;
}

MoonrayObject MoonrayScene::primaryCamera() const
{
    return mPrimaryCamera;
}

MoonrayObject
MoonrayScene::getLightSet(const std::set<MoonrayObject>& lights)
{
    if (lights.empty()) {
        return emptyLightSet(); // null light set causes crash (MOONRAY-4854)
    } else {
        unsigned h = hashObjectSet(lights);
        std::lock_guard<std::mutex> lock(mCacheMutex);
        rdl2::LightSet*& set = mLightSets[h];
        if (not set) {
            char name[20]; snprintf(name, 20, "LightSet%06X", h);
            rdl2::SceneContext& wsc = acquireSceneContext();
            set = wsc.createSceneObject("LightSet", name)->asA<rdl2::LightSet>();
            set->beginUpdate();
            for (auto& i : lights)
                set->add(i.sceneObjectAs<rdl2::Light>());
            set->endUpdate();

        }
        return MoonrayObject(set);
    }
}

MoonrayObject
MoonrayScene::getLightFilterSet(const std::set<MoonrayObject>& filters)
{
    if (filters.empty()) {
        return emptyLightFilterSet(); // null light filter set can be equivalent "all filters", not what we want
    } else {
        unsigned h = hashObjectSet(filters);
        std::lock_guard<std::mutex> lock(mCacheMutex);
        rdl2::LightFilterSet*& set = mLightFilterSets[h];
        if (not set) {
            char name[20]; snprintf(name, 20, "LightFilterSet%06X", h);
            rdl2::SceneContext& wsc = acquireSceneContext();
            set = wsc.createSceneObject("LightFilterSet", name)->asA<rdl2::LightFilterSet>();
            set->beginUpdate();
            for (auto& i : filters)
                set->add(i.sceneObjectAs<rdl2::LightFilter>());
            set->endUpdate();
        }
        return MoonrayObject(set);
    }
 
}

MoonrayObject
MoonrayScene::getShadowSet(const std::set<MoonrayObject>& lights)
{
    if (lights.empty()) {
        return MoonrayObject(); // null shadow set in assignment is equivalent to an empty shadow set
    } else {
        unsigned h = hashObjectSet(lights);
        std::lock_guard<std::mutex> lock(mCacheMutex);
        rdl2::ShadowSet*& set = mShadowSets[h];
        if (not set) {
            char name[20]; snprintf(name, 20, "ShadowSet%06X", h);
            rdl2::SceneContext& wsc = acquireSceneContext();
            set = wsc.createSceneObject("ShadowSet", name)->asA<rdl2::ShadowSet>();
            set->beginUpdate();
            for (auto& i : lights)
                set->add(i.sceneObjectAs<rdl2::Light>());
            set->endUpdate();
        }
        return MoonrayObject(set);
    }
}

std::pair<float, float> 
MoonrayScene::getTimeSamplingInterval() const
{
    // This provides the time points at which we should sample for motion blur : called "motion steps" in Moonray.
    // Moonray will interpolate/extrapolate from these values to get the values at the exact shutter open and close times. 
    static const std::pair<float, float> interval(-1, 0); 
    return interval;
}

// "Simplified Paths" are used to avoid the complex paths that can be generated by Hydra instancing
// (for example, /UsdNiPropagatedPrototypes/NoPrimvars_purposed94fac4b63817ce9/__Prototype_1/UsdNiInstancer/UsdNiPrototype/pointInstancer/prototype/ForInstancer32e32c1a7de27bf8)
// These can make testing and debugging difficult. 
// Simplified paths are off by default, but can be enabled with an option. 
// When simplifyPath is called, MoonrayScene attempts to generate a simpler path for each prim, based on its HdPrimOriginSchema.
// A map records the simple path for each original path, and the simple path is used when creating objects in the scene context.
// If there is a collision in simple paths, a suffix is added to make them unique.
SdfPath
MoonrayScene::getSimplePath(const pxr::SdfPath& path)
{
    if (!mRenderDelegate.options().getSimplifyPaths()) {
        return path;
    }
    std::lock_guard<std::mutex> lock(mSimplifyPathMutex);
    auto i = mOriginalToSimple.find(path);
    if (i != mOriginalToSimple.end()) {
        return i->second;
    } else {
        return path;
    }
}

pxr::SdfPath 
MoonrayScene::simplifyPath(const pxr::SdfPath& path, HdSceneDelegate* sceneDelegate)
{
    if (!mRenderDelegate.options().getSimplifyPaths()) {
        return path;
    }

    std::lock_guard<std::mutex> lock(mSimplifyPathMutex);

    auto i = mOriginalToSimple.find(path);
    if (i != mOriginalToSimple.end()) {
        return i->second;
    }
    
    // generate the simple path from the prim origin path, with a suffix if needed to avoid collisions.
    HdContainerDataSourceHandle primDs = getPrimContainer(path, sceneDelegate);
    HdPrimOriginSchema primOrigin = HdPrimOriginSchema::GetFromParent(primDs);
    SdfPath origin = primOrigin.GetOriginPath(HdPrimOriginSchemaTokens->scenePath);
    SdfPath simple;
    if (origin.IsEmpty()) {
        // if it is a generated instancer, it will have no origin, try the first prototype location
        HdInstancerTopologySchema instancerTopology = HdInstancerTopologySchema::GetFromParent(primDs);
        HdPathArrayDataSourceHandle instanceLocations = instancerTopology.GetInstanceLocations();
        if (instanceLocations) {
            VtArray<SdfPath> paths = instanceLocations->GetTypedValue(0);
            if (!paths.empty()) {
                simple = paths[0];
                unsigned suffix = 1;
                while (mSimplePaths.count(simple) > 0) {
                    simple = paths[0].AppendChild(TfToken(std::to_string(suffix)));
                    ++suffix;
                }
            } else {
                simple = path;
            }
        } else {
            simple = path;
        }
    } else {
        simple = origin;
        unsigned suffix = 1;
        while (mSimplePaths.count(simple) > 0) {
            simple = origin.AppendChild(TfToken(std::to_string(suffix)));
            ++suffix;
        }
    }
    mOriginalToSimple[path] = simple;
    mSimplePaths.insert(simple);
    return simple;
}
    
}