// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "renderDelegate.h"
#include "basisCurves.h"
#include "camera.h"
#include "instancer.h"
#include "light.h"
#include "MoonrayLightFilter.h"
#include "material.h"
#include "mesh.h"
#include "points.h"
#include "procedural.h"
#include "renderBuffer.h"
#include "renderVar.h"
#include "Renderer.h"
#include "renderPass.h"
#include "renderSettingsPrim.h"
#include "volume.h"
#include "tokens.h"

#include <pxr/imaging/hd/extComputation.h>
#include <pxr/base/gf/vec2f.h>

#include <iostream>
#include <cstdlib>

using namespace pxr;

namespace hdMoonray {


HdMoonray_RenderDelegate::HdMoonray_RenderDelegate(Renderer* renderer)
    : HdRenderDelegate(),
      mScene(*this, renderer),
      mOptions(*this),
      mRenderSettings(*this)
{
    _constructor();
}

HdMoonray_RenderDelegate::HdMoonray_RenderDelegate(Renderer* renderer, HdRenderSettingsMap const& settings)
    : HdRenderDelegate(settings),
      mScene(*this, renderer),
      mOptions(*this),
      mRenderSettings(*this)
{
    _constructor();
}

void
HdMoonray_RenderDelegate::_constructor()
{
    mRenderSettings.addDescriptors(mRenderSettingDescriptors);
    mScene.renderer()->addDescriptors(mRenderSettingDescriptors);
    _PopulateDefaultSettings(mRenderSettingDescriptors);
    renderParam.This = this;
    mScene.initialize();
}

HdMoonray_RenderDelegate::~HdMoonray_RenderDelegate()
{
}

void
HdMoonray_RenderDelegate::CommitResources(HdChangeTracker *tracker) 
{}

HdCommandDescriptors HdMoonray_RenderDelegate::GetCommandDescriptors() const
{
    HdCommandDescriptors descriptors;
    descriptors.emplace_back(HdMoonrayTokens->reload_textures, "Reload textures");
    descriptors.emplace_back(HdMoonrayTokens->restart_arras, "Restart Arras");
    descriptors.emplace_back(HdMoonrayTokens->output_rdl, "Output Rdl");
    return descriptors;
}

bool HdMoonray_RenderDelegate::InvokeCommand(const TfToken& command, const HdCommandArgs& args)
{
    if (command == HdMoonrayTokens->reload_textures) {
        mScene.renderer()->invalidateAllTextureResources();
        return true;
    } else if (command == HdMoonrayTokens->restart_arras) {
        mScene.renderer()->restartRenderer();
        return true;
    } else if (command == HdMoonrayTokens->output_rdl) {
        mScene.renderer()->outputRdl(options().getRdlOutput());
        return true;
    }
    return false;
}

// This token is repeated from usdVolImaging which we cannot access from here.
static TfToken openvdbAssetToken("openvdbAsset");

TfTokenVector const&
HdMoonray_RenderDelegate::GetSupportedRprimTypes() const
{
    static const TfTokenVector SUPPORTED_RPRIM_TYPES = {
        HdPrimTypeTokens->mesh,
        HdPrimTypeTokens->basisCurves,
        HdPrimTypeTokens->points,
        HdPrimTypeTokens->volume,
        HdMoonrayTokens->procedural
    };
    return SUPPORTED_RPRIM_TYPES;
}


TfTokenVector const&
HdMoonray_RenderDelegate::GetSupportedSprimTypes() const
{
    static const TfTokenVector SUPPORTED_SPRIM_TYPES = {
        HdPrimTypeTokens->camera,
        HdPrimTypeTokens->material,
        HdPrimTypeTokens->cylinderLight,
        HdPrimTypeTokens->diskLight,
        HdPrimTypeTokens->distantLight,
        HdPrimTypeTokens->domeLight,
        HdPrimTypeTokens->rectLight,
        HdPrimTypeTokens->sphereLight,
        HdMoonrayTokens->geometryLight,
        HdPrimTypeTokens->lightFilter,
        HdPrimTypeTokens->extComputation,
    };
    return SUPPORTED_SPRIM_TYPES;
}

TfTokenVector const&
HdMoonray_RenderDelegate::GetSupportedBprimTypes() const
{
    static const TfTokenVector SUPPORTED_BPRIM_TYPES = {
        HdPrimTypeTokens->renderBuffer,
        HdPrimTypeTokens->renderSettings,
        openvdbAssetToken,
    };
    return SUPPORTED_BPRIM_TYPES;
}

HdResourceRegistrySharedPtr
HdMoonray_RenderDelegate::GetResourceRegistry() const
{
    static HdResourceRegistrySharedPtr ptr;
    if (not ptr) ptr.reset(new HdResourceRegistry());
    return ptr;
}

// Result of this is passed to RenderBuffer::Allocate
HdAovDescriptor
HdMoonray_RenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
    return HdMoonray_RenderVar::getAovDescriptor(name);
}

VtDictionary
HdMoonray_RenderDelegate::GetRenderStats() const
{
    VtDictionary stats;
    if (mScene.renderer() && !mScene.renderer()->isFrameComplete()) {
        auto progress = mScene.renderer()->getProgress();
        // This is the only value usdview reads, you have to turn on "View/Heads-Up Display/GPU Stats" to see it
        stats[HdPerfTokens->numCompletedSamples] = int(progress * 100000);
        // Values used by Houdini:
        // See $HFS/toolkit/include/HUSD/XUSD_Tokens.h for full list
        static const TfToken percentDone("percentDone");
        stats[percentDone] = progress * 100;
        static const TfToken totalClockTime("totalClockTime");
        stats[totalClockTime] = mScene.renderer()->getElapsedSeconds();
        static const TfToken rpAnn("renderProgressAnnotation");
        const std::string& status = mScene.renderer()->getStatusString();
        if (!status.empty()) {
            stats[rpAnn] = status;
        }
    }
    return stats;
}

bool
HdMoonray_RenderDelegate::IsPauseSupported() const
{
    return true;
}

bool
HdMoonray_RenderDelegate::Pause()
{
    if (mScene.renderer()) mScene.renderer()->pause();
    return true;
}

bool
HdMoonray_RenderDelegate::Resume()
{
    if (mScene.renderer()) mScene.renderer()->resume();
    return true;
}

HdRenderPassSharedPtr
HdMoonray_RenderDelegate::CreateRenderPass(HdRenderIndex *renderIndex,
                                 HdRprimCollection const& collection)
{
    mRenderIndex = renderIndex;
    return HdRenderPassSharedPtr(new HdMoonray_RenderPass(renderIndex, collection, this));
}

HdInstancer*
HdMoonray_RenderDelegate::CreateInstancer(HdSceneDelegate *sceneDelegate,
                                SdfPath const& id)
{
    return new HdMoonray_Instancer(sceneDelegate, id);
}

void
HdMoonray_RenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
    delete instancer;
}

HdRprim*
HdMoonray_RenderDelegate::CreateRprim(TfToken const& typeId,
                            SdfPath const& rprimId)
{
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdMoonray_Mesh(rprimId);
    } else if (typeId == HdPrimTypeTokens->basisCurves) {
        return new HdMoonray_BasisCurves(rprimId);
    } else  if (typeId == HdPrimTypeTokens->points) {
        return new HdMoonray_Points(rprimId);
    } else  if (typeId == HdPrimTypeTokens->volume) {
        auto p = new HdMoonray_Volume(rprimId);
        if (not rprimId.IsEmpty())
            mVolumes.insert(p);
        return p;
    } else  if (typeId == HdMoonrayTokens->procedural) {
        auto p = new HdMoonray_Procedural(rprimId);
        if (not rprimId.IsEmpty())
            mProcedurals.insert(p);
        return p;
    } else {
        Logger::warn(rprimId, ": unknown Rprim type ", typeId);
        return nullptr;
    }
}

void
HdMoonray_RenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    mProcedurals.erase(rPrim);
    mVolumes.erase(rPrim);

    delete rPrim;
}

HdSprim*
HdMoonray_RenderDelegate::CreateSprim(TfToken const& typeId,
                            SdfPath const& sprimId)
{
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdMoonray_Camera(sprimId);
    } else  if (typeId == HdPrimTypeTokens->material) {
        return new HdMoonray_Material(sprimId);
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(sprimId); // no subclass needed
    } else if (typeId == HdPrimTypeTokens->lightFilter) {
        return new MoonrayLightFilter(typeId, sprimId);
    } else if (typeId == HdMoonrayTokens->geometryLight) {
        auto p = new HdMoonray_Light(typeId, sprimId);
        if (not sprimId.IsEmpty())
            mLights.insert(p);
        return p;
    }
    else if (HdMoonray_Light::isSupportedType(typeId)) {
        auto p = new HdMoonray_Light(typeId, sprimId);
        if (not sprimId.IsEmpty())
            mLights.insert(p);
        return p;
    }
    Logger::warn(sprimId, ": unknown Sprim type ", typeId);
    return nullptr;
}

HdSprim *
HdMoonray_RenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    // all other sprims set the default values in the constructor
    return CreateSprim(typeId, SdfPath::EmptyPath());
}

void
HdMoonray_RenderDelegate::DestroySprim(HdSprim *sPrim)
{
    mLights.erase(sPrim);
    delete sPrim;
}

HdBprim *
HdMoonray_RenderDelegate::CreateBprim(TfToken const& typeId,
                            SdfPath const& bprimId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdMoonray_RenderBuffer(bprimId);
    } else if (typeId == HdPrimTypeTokens->renderSettings) {
        return new HdMoonray_RenderSettings(bprimId);
    } else if (typeId == openvdbAssetToken) {
        return new HdMoonray_OpenVdbAsset(bprimId);
    } else {
        Logger::warn(bprimId, ": unknown Bprim type ", typeId);
        return nullptr;
    }
}

HdBprim *
HdMoonray_RenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    // all bprims set the default values in the constructor
    return CreateBprim(typeId, SdfPath::EmptyPath());
}

void
HdMoonray_RenderDelegate::DestroyBprim(HdBprim *bPrim)
{
    delete bPrim;
}

void 
HdMoonray_RenderDelegate::SetRenderSetting(TfToken const& key, VtValue const& value)
{
    // allow moonray: prefixed settings, since this may appear in a RenderSettings prim
    const std::string strippedKey = SdfPath::StripPrefixNamespace(key.GetString(),"moonray").first;
    HdRenderDelegate::SetRenderSetting(TfToken(strippedKey), value);

    if (options().getShowRenderSettingChanges()) {
        std::cout << "Render setting changed: " << strippedKey << " = " << value << std::endl;
    }
}

////////////////////////////////////////////////////////////////////////////////

void
HdMoonray_RenderDelegate::applySettings()
{
    unsigned v = GetRenderSettingsVersion();
    if (v != mPreviousRenderSettings) {
        mPreviousRenderSettings = v;
        mRenderSettings.apply(); 
        if (options().getShowAllRenderSettings()) {
            showAllRenderSettings();
            options().setShowAllRenderSettings(false);
        }
        mScene.renderer()->applySettings(mRenderSettings);
        mScene.renderer()->setIsHoudini(options().isHoudini());
    }
}

void
HdMoonray_RenderDelegate::resetSettingsToDefaults()
{
    _settingsMap.clear();
    _PopulateDefaultSettings(mRenderSettingDescriptors);
    _settingsVersion++; // force update on next applySettings() call
    if (options().getShowRenderSettingChanges()) {
        std::cout << "Render settings reset to defaults" << std::endl;
    }
}

void
HdMoonray_RenderDelegate::setRenderSettings(VtDictionary const& namespacedSettings)
{
    // namespacedSettings comes from a RenderSettingsBase prim.
    // we could choose to only apply settings in the "moonray:" namespace, but it's unclear
    // whether this standard is followed everywhere.
    for (const auto& entry : namespacedSettings) {
        SetRenderSetting(TfToken(entry.first), entry.second);
    }
}

void
HdMoonray_RenderDelegate::showAllRenderSettings()
{
    std::cout << "---------------------------------" << std::endl;
    std::cout << "Render Settings:" << std::endl;
    for (const auto& entry : _settingsMap) {
        std::cout << "  " << entry.first << ": " << entry.second.GetTypeName() << " = " << entry.second << std::endl;
    }
    std::cout << "---------------------------------" << std::endl;
}


static bool
contains(const TfTokenVector& tags, const TfToken& tag)
{
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

bool
HdMoonray_RenderDelegate::setRenderTags(HdRenderIndex* index, const TfTokenVector& tags)
{
    if (tags == mRenderTags) return false;
    // fix Pixar bug https://github.com/PixarAnimationStudios/USD/issues/801
    // by turning off any rprims with a removed purpose
    TfTokenVector removed;
    for (auto& t : mRenderTags)
        if (not contains(tags, t)) removed.push_back(t);
    mRenderTags = tags;
    if (not removed.empty()) {
        for (auto& id : index->GetRprimIds()) {
            if (contains(removed, index->GetRenderTag(id)))
                (const_cast<HdRprim*>(index->GetRprim(id)))->Finalize(&renderParam);
        }
    }
    return true;
}


void HdMoonray_RenderDelegate::markAllLightsDirty(HdDirtyBits bits)
{
    if (!mRenderIndex) return;
    for (auto& p : mLights)
        mRenderIndex->GetChangeTracker().MarkSprimDirty(p->GetId(), bits);
}

void HdMoonray_RenderDelegate::markAllProceduralsDirty(HdDirtyBits bits)
{
    if (!mRenderIndex) return;
    for (auto& p : mProcedurals)
        mRenderIndex->GetChangeTracker().MarkRprimDirty(p->GetId(), bits);
}

void HdMoonray_RenderDelegate::markAllVolumesDirty(HdDirtyBits bits)
{
    if (!mRenderIndex) return;
    for (auto& p : mVolumes)
        mRenderIndex->GetChangeTracker().MarkRprimDirty(p->GetId(), bits);
}

void HdMoonray_RenderDelegate::markAllRprimsDirty(HdDirtyBits bits)
{
    if (mRenderIndex) mRenderIndex->GetChangeTracker().MarkAllRprimsDirty(bits);
}

}
