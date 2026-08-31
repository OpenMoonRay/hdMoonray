// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MoonrayScene.h"
#include "RenderSettings.h"
#include "Options.h"

#include <pxr/base/vt/dictionary.h>
#include <pxr/imaging/hd/renderDelegate.h>

namespace hdMoonray {

class Renderer;

class HdMoonray_RenderDelegate final: public pxr::HdRenderDelegate
{
public:

    // HdRenderDelegate API implementation

    HdMoonray_RenderDelegate(Renderer* renderer);
    HdMoonray_RenderDelegate(Renderer* renderer, pxr::HdRenderSettingsMap const& settings);

    ~HdMoonray_RenderDelegate();

    pxr::HdRenderParam *GetRenderParam() const override { return const_cast<RenderParam*>(&renderParam); }
    static HdMoonray_RenderDelegate& get(pxr::HdRenderParam* p) { return *(((RenderParam*)p)->This); }

    const pxr::TfTokenVector &GetSupportedRprimTypes() const override;
    pxr::HdRprim *CreateRprim(pxr::TfToken const& typeId, pxr::SdfPath const& rprimId) override;
    void DestroyRprim(pxr::HdRprim *rPrim) override;

    const pxr::TfTokenVector &GetSupportedSprimTypes() const override;
    pxr::HdSprim *CreateSprim(pxr::TfToken const& typeId,pxr::SdfPath const& sprimId) override;
    pxr::HdSprim *CreateFallbackSprim(pxr::TfToken const& typeId) override;
    void DestroySprim(pxr::HdSprim *sPrim) override;

    const pxr::TfTokenVector &GetSupportedBprimTypes() const override;
    pxr::HdBprim *CreateBprim(pxr::TfToken const& typeId, pxr::SdfPath const& bprimId) override;
    pxr::HdBprim *CreateFallbackBprim(pxr::TfToken const& typeId) override;
    void DestroyBprim(pxr::HdBprim *bPrim) override;

    pxr::HdRenderPassSharedPtr CreateRenderPass(pxr::HdRenderIndex *index, pxr::HdRprimCollection const& collection) override;

    pxr::HdInstancer *CreateInstancer(pxr::HdSceneDelegate *delegate, pxr::SdfPath const& id) override;
    void DestroyInstancer(pxr::HdInstancer *instancer) override;
    pxr::HdResourceRegistrySharedPtr GetResourceRegistry() const override;
    
    void CommitResources(pxr::HdChangeTracker *tracker) override;
    
    pxr::HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override
    { return mRenderSettingDescriptors; }

    pxr::HdCommandDescriptors GetCommandDescriptors() const override;
    bool InvokeCommand(const pxr::TfToken& command, const pxr::HdCommandArgs& args = pxr::HdCommandArgs()) override;

    bool IsPauseSupported() const override;
    bool Pause() override;
    bool Resume() override;

    pxr::TfToken GetMaterialBindingPurpose() const override { return pxr::HdTokens->full; }
    pxr::TfToken GetMaterialNetworkSelector() const override
        { static pxr::TfToken tt("moonray"); return tt; }

    pxr::HdAovDescriptor GetDefaultAovDescriptor(pxr::TfToken const& name) const override;

    pxr::VtDictionary GetRenderStats() const override;

    void SetRenderSetting(pxr::TfToken const& key, pxr::VtValue const& value) override;

/// Moonray-specific api

    MoonrayScene& scene() { return mScene; }
    Options& options() { return mOptions; }
    Renderer& renderer() { return *mScene.renderer(); }

    /// call through to scene
    Renderer& renderer() const { return *mScene.renderer(); }
    const scene_rdl2::rdl2::SceneContext& sceneContext() const { return mScene.sceneContext(); }
    void beginUpdate() { mScene.beginUpdate(); }
    scene_rdl2::rdl2::SceneContext& acquireSceneContext() { return mScene.acquireSceneContext(); }
    
    void applySettings();
    void showAllRenderSettings();
    void resetSettingsToDefaults();
    void setRenderSettings(pxr::VtDictionary const& namespacedSettings);

    // Fix for Pixar bug https://github.com/PixarAnimationStudios/USD/issues/801
    bool setRenderTags(pxr::HdRenderIndex* index, const pxr::TfTokenVector&);

    const RenderSettings& renderSettings() const { return mRenderSettings; }

    void markAllLightsDirty(pxr::HdDirtyBits bits);
    void markAllProceduralsDirty(pxr::HdDirtyBits bits);
    void markAllVolumesDirty(pxr::HdDirtyBits bits);
    void markAllRprimsDirty(pxr::HdDirtyBits bits);

private:
    HdMoonray_RenderDelegate(const HdMoonray_RenderDelegate &)             = delete;
    HdMoonray_RenderDelegate &operator =(const HdMoonray_RenderDelegate &) = delete;

    class RenderParam final: public pxr::HdRenderParam {
    public:
        HdMoonray_RenderDelegate* This; // possibly this should be const
    } renderParam;

    void _constructor();
    MoonrayScene mScene;
    Options mOptions;
   
    RenderSettings mRenderSettings;
    unsigned mPreviousRenderSettings = 0;
    pxr::HdRenderSettingDescriptorList mRenderSettingDescriptors;

    
    pxr::TfTokenVector mRenderTags;
    pxr::HdRenderIndex *mRenderIndex = nullptr; // stored by CreateRenderPass

    std::set<pxr::HdSprim*> mLights;
    std::set<pxr::HdRprim*> mProcedurals;
    std::set<pxr::HdRprim*> mVolumes;
};

// Same as scene_rdl2::rdl2:::SceneObject::UpdateGuard but also stops the renderer
class UpdateGuard {
public:
    // this constructor stops the renderer
    UpdateGuard(HdMoonray_RenderDelegate& r, scene_rdl2::rdl2::SceneObject* obj):
        mSceneObject(obj) 
    {  r.beginUpdate();  obj->beginUpdate(); }
    UpdateGuard(HdMoonray_RenderDelegate& r, MoonrayObject& obj):
        mSceneObject(obj.sceneObject()) 
    {  r.beginUpdate();  obj.beginUpdate(); }
    // only use this constructor if you know rendering is stopped already
    UpdateGuard(scene_rdl2::rdl2::SceneObject* obj):
        mSceneObject(obj) { obj->beginUpdate(); }
    UpdateGuard(scene_rdl2::rdl2::SceneObject& obj):
        mSceneObject(&obj) { obj.beginUpdate(); }
    UpdateGuard(MoonrayObject& obj):
        mSceneObject(obj.sceneObject()) 
    {  obj.sceneObject()->beginUpdate(); }
    ~UpdateGuard() { mSceneObject->endUpdate(); }
    UpdateGuard(const UpdateGuard&) = delete;
    UpdateGuard& operator=(const UpdateGuard&) = delete;
private:
    scene_rdl2::rdl2::SceneObject* mSceneObject;
};

}
