// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "PrimTypeUtils.h"

#include <pxr/imaging/hd/tokens.h>

#include <cstdlib>
#include <iostream>

namespace {

const pxr::TfToken domeLight1Token("DomeLight_1", pxr::TfToken::Immortal);
const pxr::TfToken usdLuxDomeLight1Token("UsdLuxDomeLight_1", pxr::TfToken::Immortal);
const pxr::TfToken pluginLightFilterToken("PluginLightFilter", pxr::TfToken::Immortal);
const pxr::TfToken usdLuxPluginLightFilterToken("UsdLuxPluginLightFilter", pxr::TfToken::Immortal);
const pxr::TfToken lightFilterToken("lightFilter", pxr::TfToken::Immortal);
const pxr::TfToken openvdbAssetToken("openvdbAsset", pxr::TfToken::Immortal);
const pxr::TfToken houdiniFieldAssetToken("houdiniFieldAsset", pxr::TfToken::Immortal);

const pxr::TfToken usdRenderSettingsToken("RenderSettings", pxr::TfToken::Immortal);
const pxr::TfToken hdRenderSettingsToken("renderSettings", pxr::TfToken::Immortal);

bool
primTypeDiagEnabled()
{
    static const bool enabled = std::getenv("HDMR_PRIMTYPE_DIAG") != nullptr;
    return enabled;
}

std::string
renderSchemaFlags(const pxr::TfToken& token)
{
    return std::string("isUsdRenderSettings=") + (token == usdRenderSettingsToken ? "1" : "0") +
           " isHdRenderSettings=" + (token == hdRenderSettingsToken ? "1" : "0");
}

void
logCanonicalPrimType(const char* functionName,
                     const pxr::TfToken& input,
                     const pxr::TfToken& canonical)
{
    if (!primTypeDiagEnabled()) {
        return;
    }

    std::cerr << "[HDMR_PRIMTYPE_DIAG] "
              << functionName
              << " input=" << input
              << " canonical=" << canonical
              << " inputFlags={" << renderSchemaFlags(input) << "}"
              << " canonicalFlags={" << renderSchemaFlags(canonical) << "}"
              << std::endl;
}

}

namespace hdMoonray {

pxr::TfToken
canonicalSprimType(const pxr::TfToken& type)
{
    pxr::TfToken canonical = type;
    if (type == domeLight1Token || type == usdLuxDomeLight1Token) {
        canonical = pxr::HdPrimTypeTokens->domeLight;
    } else if (type == pluginLightFilterToken || type == usdLuxPluginLightFilterToken) {
        canonical = lightFilterToken;
    }
    logCanonicalPrimType("canonicalSprimType", type, canonical);
    return canonical;
}

pxr::TfToken
canonicalBprimType(const pxr::TfToken& type)
{
    pxr::TfToken canonical = type;
    if (type == usdRenderSettingsToken) {
        canonical = pxr::HdPrimTypeTokens->renderSettings;
    } else if (type == houdiniFieldAssetToken) {
        canonical = openvdbAssetToken;
    }
    logCanonicalPrimType("canonicalBprimType", type, canonical);
    return canonical;
}

const pxr::TfTokenVector&
supportedSprimTypeAliases()
{
    static const pxr::TfTokenVector aliases = {
        domeLight1Token,
        usdLuxDomeLight1Token,
        pluginLightFilterToken,
        usdLuxPluginLightFilterToken,
    };
    return aliases;
}

const pxr::TfTokenVector&
supportedBprimTypeAliases()
{
    static const pxr::TfTokenVector aliases = {
        usdRenderSettingsToken,
        houdiniFieldAssetToken,
    };
    return aliases;
}

}
