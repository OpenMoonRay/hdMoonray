// Copyright 2023-2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "renderVar.h"
#include "renderDelegate.h"
#include "ValueConverter.h"
#include "tokens.h"


#include <pxr/imaging/hd/renderVarSchema.h>

#include <iostream>
#include <cmath>

using namespace pxr;

namespace {

constexpr size_t INSTANCE_NESTING = 1; // number of extra outputs for nested instancers

// If the aov data type is not specified, we deduce the format from the source name
HdFormat getFormatForPrimvarName(const TfToken& sourceName)
{
    static std::map<TfToken, HdFormat> map = {
        {HdAovTokens->color, HdFormatFloat32Vec4},
        {HdAovTokens->primId, HdFormatInt32},
        {HdAovTokens->instanceId, HdFormatInt32},
        {HdAovTokens->elementId, HdFormatInt32},
        {HdAovTokens->edgeId, HdFormatInt32},
        {HdAovTokens->pointId, HdFormatInt32},
        {HdMoonrayTokens->cryptomatte, HdFormatFloat32},
        {HdAovTokens->depth, HdFormatFloat32},
        {HdAovTokens->cameraDepth, HdFormatFloat32},
        {HdAovTokens->normal, HdFormatFloat32Vec3},
        {HdMoonrayTokens->N, HdFormatFloat32Vec3},
        {HdMoonrayTokens->st, HdFormatFloat32Vec2},
        {HdMoonrayTokens->uv, HdFormatFloat32Vec2},
    };
    auto i = map.find(sourceName);
    return i != map.end() ? i->second : HdFormatFloat32;
}

// Get the HdFormat corresponding to an AOV specification:
HdFormat getFormatForAov(const TfToken& sourceName, 
                         const TfToken& sourceType,
                         const TfToken& dataType)
{
    // if the dataType is specified and one we understand, use that to determine the format
    if (!dataType.IsEmpty()) {
        // UsdImaging copies the dataType from UsdRenderVar, so it is an SDF type name
        // We handle a limited set.
        if (dataType == HdMoonrayTokens->_float) return HdFormatFloat32;
        if (dataType == HdMoonrayTokens->_float2) return HdFormatFloat32Vec2;
        if (dataType == HdMoonrayTokens->_float3) return HdFormatFloat32Vec3;
        if (dataType == HdMoonrayTokens->_float4) return HdFormatFloat32Vec4;
        if (dataType == HdMoonrayTokens->_int) return HdFormatInt32;
        if (dataType == HdMoonrayTokens->_color3) return HdFormatFloat32Vec3;
        if (dataType == HdMoonrayTokens->_color4) return HdFormatFloat32Vec4;
    }

    // otherwise use the sourceType and sourceName to determine the format
    if (sourceType.IsEmpty() || sourceType == HdMoonrayTokens->raw || sourceType == HdMoonrayTokens->primvars) {
        return getFormatForPrimvarName(sourceName);
    } else if (sourceType == HdMoonrayTokens->lpe) {
        return HdFormatFloat32Vec3;
    } else if (sourceType == HdMoonrayTokens->shader) {
        return HdFormatFloat32Vec3;
    }
    return HdFormatFloat32;
}

// given a full aov name, return the aov type (i.e. the prefix) and the name with any prefix removed.
// the type is empty if no prefix is found.
void splitAovName(const TfToken& fullName, TfToken& type, TfToken& name)
{
    const std::string s(fullName.GetString());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == ':') {
            type = TfToken(s.substr(0,i+1));
            name = TfToken(s.substr(i+1));
            return;
        }
        if (not isalnum(s[i])) break; // prefix has to be a plain word
    }
    type = TfToken();
    name = fullName;
}

}

namespace hdMoonray {

// static
// the RenderDelegate API requires us to provide an HdAovDescriptor given just the aov name.
HdAovDescriptor
HdMoonray_RenderVar::getAovDescriptor(TfToken const& aovName)
{
    TfToken type, name; 
    splitAovName(aovName, type, name);
    HdFormat format = getFormatForAov(name, type, TfToken());
    return HdAovDescriptor(format, false, VtValue());
}

// Set up the RenderOutput using a HdRenderPassAovBinding.
// This is used in interactive rendering. There is no explicit RenderProduct or RenderVar prim,
// and we may be required to deduce the sourceName, sourceType and dataType from the aovName.
void
HdMoonray_RenderVar::setAov(const HdRenderPassAovBinding& aovBinding,
                            HdMoonray_RenderDelegate* renderDelegate)
{
    if (mOutput.isValid()) {
        // assumes settings don't change after the first call to setAov. 
        // If they do, you will need to reset mOutput to force an update 
        return;
    }

    // We will need these three values to construct the Moonray RenderOutput.
    TfToken sourceName, sourceType, dataType;

    // We can get a sourceName and sourceType by splitting aovBinding.aovName
    splitAovName(aovBinding.aovName, sourceType, sourceName);

    // However, Houdini works around the missing RenderVar prim by copying values into the settings dictionary of
    // the binding. So we will allow any sourceName, sourceType and dataType in this dict
    // to override the values we just determined from aovBinding.aovName.
    auto sourceNameIt = aovBinding.aovSettings.find(HdRenderVarSchemaTokens->sourceName);
    if (sourceNameIt != aovBinding.aovSettings.end()) {
        if (sourceNameIt->second.IsHolding<TfToken>()) {
            sourceName = sourceNameIt->second.UncheckedGet<TfToken>();
        }
        else if (sourceNameIt->second.IsHolding<std::string>()) {
            sourceName = TfToken(sourceNameIt->second.UncheckedGet<std::string>());
        }
    }

    auto sourceTypeIt = aovBinding.aovSettings.find(HdRenderVarSchemaTokens->sourceType);
    if (sourceTypeIt != aovBinding.aovSettings.end()) {
        if (sourceTypeIt->second.IsHolding<TfToken>()) {
            sourceType = sourceTypeIt->second.UncheckedGet<TfToken>();
        }
        else if (sourceTypeIt->second.IsHolding<std::string>()) {
                sourceType = TfToken(sourceTypeIt->second.UncheckedGet<std::string>());
        }
    }

    auto dataTypeIt = aovBinding.aovSettings.find(HdRenderVarSchemaTokens->dataType);
    if (dataTypeIt != aovBinding.aovSettings.end()) {
        if (dataTypeIt->second.IsHolding<TfToken>()) {
            dataType = dataTypeIt->second.UncheckedGet<TfToken>();
        }
        else if (dataTypeIt->second.IsHolding<std::string>()) {
            dataType = TfToken(dataTypeIt->second.UncheckedGet<std::string>());
        }
    }

    // Given these values, we can determine the HdFormat to use for this AOV.
    HdFormat targetFormat = getFormatForAov(sourceName, sourceType, dataType);
    VtDictionary aovSettings(aovBinding.aovSettings.begin(), aovBinding.aovSettings.end());
    
    // We don't want moonray to write the var to file in an interactive render, 
    // so in theory we specify the fileName as ""
    // per MOONRAY-5301 this will cause a crash, so use a dummy file for now.
    const std::string fileName = "/tmp/scene.exr";

    // build the RenderOutput and any extra outputs for this AOV
    buildOutputs(sourceName, sourceType, targetFormat, aovSettings, fileName, renderDelegate);
}

// Set up the RenderOutput from RenderProduct and RenderVar prims.
void
HdMoonray_RenderVar::setAov(const HdRenderSettings::RenderProduct& product,
                            const HdRenderSettings::RenderProduct::RenderVar& renderVar,
                            HdMoonray_RenderDelegate* renderDelegate)
{
    HdFormat targetFormat = getFormatForAov(TfToken(renderVar.sourceName), renderVar.sourceType, renderVar.dataType);
    const std::string outputFile = product.name.GetString();
    buildOutputs(TfToken(renderVar.sourceName), 
                 renderVar.sourceType, 
                 targetFormat,
                 renderVar.namespacedSettings, 
                 outputFile, 
                 renderDelegate);
}

// create mOutput, if needed, and configure it for the given AOV name and settings
void
HdMoonray_RenderVar::buildOutputs(const TfToken& sourceName,
                                  const TfToken& sourceType,
                                  HdFormat targetFormat,
                                  const VtDictionary& aovSettings,
                                  const std::string& fileName,
                                  HdMoonray_RenderDelegate* renderDelegate)
{
    if (sourceName == HdAovTokens->color) {
        mOutput = MoonrayOutput::beautyOutput();
        return;
    }

    // create a RenderOutput for this AOV
    const std::string outputName = "/_outputs/" + sourceName.GetString();
    mOutput = renderDelegate->scene().createRenderOutput(outputName);
    if (mOutput.isNull()) return;

    renderDelegate->scene().beginUpdate();

    mIsOpenGLDepth = false;
    mEmptyValue = 0.0f;

    // fill in the RenderOutput settings based on the AOV name
    // this is a default that can be overridden by the aovSettings dictionary
    if (sourceType == HdAovTokens->primvars || 
        sourceType == HdMoonrayTokens->raw || 
        sourceType.IsEmpty()) {

        // there are several special cases...
        if (sourceName == HdAovTokens->primId) {

            mOutput.setAsPrimvar(sourceName.GetString(), HdFormatInt32);
            mEmptyValue = -1.0f; // usdview expects -1 in primId for empty areas

        } else if (sourceName == HdAovTokens->instanceId) {

            mOutput.setAsPrimvar(sourceName.GetString(), HdFormatInt32);

            // instanceId generates multiple outputs for nested instancers: instanceIdA, instanceIdB...
            // these will be added to form a single output buffer in the resolve stage
            for (size_t i = 0; i < INSTANCE_NESTING; ++i) {
                char letter = 'A'+i;
                const std::string outputName = "/_outputs/" + sourceName.GetString() + letter;
                MoonrayOutput extra = renderDelegate->scene().createRenderOutput(outputName);
                if (extra.isValid()) {
                    extra.setAsPrimvar(sourceName.GetString() + letter, HdFormatInt32);
                    mExtraOutputs.push_back(extra);
                }
            }
        } else if (sourceName == HdMoonrayTokens->cryptomatte) {
        
            // cryptomatte requires a primvar output as well as the main cryptomatte output
            std::string deepIdName = renderDelegate->options().getDeepIdAttrName();
            if (deepIdName.empty()) deepIdName = "cryptoId";
            MoonrayOutput dummyOutput = renderDelegate->scene().createRenderOutput("/_outputs/cryptomatte_primvar");
            dummyOutput.setAsPrimvar(deepIdName, HdFormatFloat32);

            mOutput = renderDelegate->scene().createRenderOutput("/_outputs/cryptomatte");
            mOutput.setAsCryptomatte();

        } else if (sourceName == HdAovTokens->depth || sourceName == HdAovTokens->cameraDepth) {

            mOutput.setAsDepth();
            mIsOpenGLDepth = (sourceName == HdAovTokens->depth);

        } else if (sourceName == HdAovTokens->normal || sourceName == HdMoonrayTokens->N) {

            mOutput.setAsNormal();

        } else if (sourceName == HdMoonrayTokens->st || sourceName == HdMoonrayTokens->uv) {

            mOutput.setAsSt();

        } else {

            mOutput.setAsPrimvar(sourceName.GetString(), targetFormat);
        }

    } else if (sourceType == HdAovTokens->lpe) {

        mOutput.setAsLpe(sourceName.GetString());

    } else if (sourceType == HdAovTokens->shader) {
        
        mOutput.setAsShader(sourceName.GetString());
    }
    
    mOutput.setFileName(fileName);
    mOutput.applySettings(aovSettings);
}

void 
HdMoonray_RenderVar::finalize() 
{
    if (mOutput.isValid()) {
        mOutput.deactivate();
    }
    for (auto& output : mExtraOutputs) {
        if (output.isValid()) {
            output.deactivate();
        }
    }
}

} // namespace hdMoonray
