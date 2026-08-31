// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <string>
#include <set>

#include "MoonrayObject.h"
#include "MoonrayOutput.h"

#include "pxr/imaging/hd/sceneDelegate.h"
#include <pxr/usd/sdf/path.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/tf/token.h>

namespace scene_rdl2 { namespace rdl2 {
    class Camera;
    class Geometry;
    class GeometrySet;
    class Layer;
    class Light;
    class LightFilterSet;
    class LightSet;
    class SceneContext;
    class ShadowSet;
    class Material;
    class SceneObject;
    class VolumeShader;
    class RenderOutput;
    class UserData;
}}

namespace hdMoonray {

class HdMoonray_RenderDelegate;
class Renderer;

enum CategoryType { LightCategory, ShadowCategory, FilterCategory };
static const int numCategories = 3;
static const pxr::TfToken defaultCategory;

// Provides API to access the Moonray (RDL2) scene
// - create and get SceneObjects by name
// - manage categories, used to specify which lights and filters affect which geometry
// - manage default objects (primary camera, default material, etc) that are automatically created as needed
// - manage automatically created sets of lights and filters, with caching to reuse them if the same set is requested again

class MoonrayScene 
{
public:
    MoonrayScene(HdMoonray_RenderDelegate& renderDelegate, Renderer* renderer);
    ~MoonrayScene();

    
    void initialize();

    // read only access to the scene context
    const scene_rdl2::rdl2::SceneContext& sceneContext() const;
    // beginUpdate() must be called before modifying the scene context
    // acquireSceneContext() automatically calls beginUpdate() and returns a non-const reference to the scene context for modification.
    void beginUpdate();
    scene_rdl2::rdl2::SceneContext& acquireSceneContext();

    // Create a new SceneObject of the given class and id, or return an existing one if it already exists.
    // If an object exists with the given id but a different class, then a modified name may be used.
    scene_rdl2::rdl2::SceneObject* create(const std::string& className, const pxr::SdfPath& id, const std::string& suffix = std::string());
    MoonrayObject createObject(const std::string& className, const pxr::SdfPath& id, const std::string& suffix = std::string())
        { return MoonrayObject(create(className, id, suffix)); }

    MoonrayOutput createRenderOutput(const std::string& name);

    // Get an existing SceneObject by id, or return nullptr if it doesn't exist.
    scene_rdl2::rdl2::SceneObject* get(const pxr::SdfPath& id, const std::string& suffix = std::string());
    MoonrayObject getObject(const pxr::SdfPath& id, const std::string& suffix = std::string())
        { return MoonrayObject(get(id, suffix)); }

    // Check the interface type of a given class
    bool checkClassInterface(const std::string& className, MoonrayAttribute::InterfaceType interfaceType);

    // Apply the given assignment to the geometry, placing in the default layer and geometry set.
    // Unassigned geometry is not rendered
    void assign(const MoonrayObject& obj, const MoonrayAssignment& assignment);
    void assign(const MoonrayObject& obj, const std::string& partName, const MoonrayAssignment& assignment);
    void addUnassigned(const MoonrayObject& obj);

    // Categories are used to specify which lights and light filters affect which geometry
    // Each category is a token associated with a set of lights or filters. Each light or filter must call 
    // setCategory() for each category it belongs to, and releaseCategory() when it is removed from a category or deleted.
    // lights and filters with no category must be placed in the default category.
    void setCategory(const MoonrayObject& obj, CategoryType type, const pxr::TfToken& category);
    void releaseCategory(const MoonrayObject& obj, CategoryType type, const pxr::TfToken& category);

    // Given the categories assigned to an (unspecified) piece of geometry, fill in the assignment with appropriate
    // light sets, shadow sets and filter sets, based on the contents of the categories that have been set by the above functions.
    void updateAssignmentFromCategories(MoonrayAssignment&,
                                        const pxr::VtArray<pxr::TfToken>& categories);

    // used to track if a default light is needed
    void addLight();
    void removeLight();
    
    // Default objects that are automatically created as needed. These are shared and should not be modified.
    MoonrayObject defaultMaterial();
    MoonrayObject errorMaterial();
    MoonrayObject defaultVolumeShader();
    MoonrayObject primaryCamera() const;
    MoonrayObject emptyLightSet();
    MoonrayObject emptyLightFilterSet();

    // Automatically create sets, using a cache to reuse them if the same set is requested again.
    MoonrayObject getLightSet(const std::set<MoonrayObject>& lights);
    MoonrayObject getLightFilterSet(const std::set<MoonrayObject>& filters);
    MoonrayObject getShadowSet(const std::set<MoonrayObject>& shadows);

    bool isTimeSamplingIntervalEnabled() const { return mTimeSamplingIntervalEnabled; }
    void enableTimeSamplingInterval(bool enable) { mTimeSamplingIntervalEnabled = enable; }
    std::pair<float, float> getTimeSamplingInterval() const;

    Renderer* renderer() const { return mRenderer.get(); }

    pxr::SdfPath simplifyPath(const pxr::SdfPath& path, pxr::HdSceneDelegate* sceneDelegate);
    pxr::SdfPath getSimplePath(const pxr::SdfPath& path);

private:
    void addCategoryContents(const pxr::TfToken& category, CategoryType type, std::set<MoonrayObject>& contents);
    void removeCategoryContents(const pxr::TfToken& category, CategoryType type, std::set<MoonrayObject>& contents);
    void createDefaultLightIfNeeded();

    HdMoonray_RenderDelegate& mRenderDelegate;
    std::unique_ptr<Renderer> mRenderer;

    // CategoryType -> category -> set of objects in that category
    std::map<pxr::TfToken, std::set<MoonrayObject>> mCategoryContents[numCategories];
    
     // Cache of automatically created sets
    std::map<size_t, scene_rdl2::rdl2::LightSet*> mLightSets;
    std::map<size_t, scene_rdl2::rdl2::ShadowSet*> mShadowSets;
    std::map<size_t, scene_rdl2::rdl2::LightFilterSet*> mLightFilterSets;

    MoonrayObject mPrimaryCamera;
    MoonrayObject mAllGeometry;
    MoonrayObject mDefaultLayer;
    MoonrayObject mDefaultMaterial;
    MoonrayObject mErrorMaterial;
    MoonrayObject mDefaultVolumeShader;
    MoonrayObject mDefaultLight;
    MoonrayObject mEmptyLightSet;
    MoonrayObject mEmptyLightFilterSet;

    bool mTimeSamplingIntervalEnabled = false;
  
    int mNumLights = 0; 
    std::mutex mCreateMutex;
    std::mutex mLayerMutex;
    std::mutex mCategoriesMutex;
    std::mutex mCacheMutex;

    std::mutex mSimplifyPathMutex;
    std::map<pxr::SdfPath, pxr::SdfPath> mOriginalToSimple;
    std::set<pxr::SdfPath> mSimplePaths;

};

}
