// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "PrimTypeUtils.h"

#include <pxr/imaging/hd/tokens.h>

namespace {

const pxr::TfToken domeLight1Token("DomeLight_1", pxr::TfToken::Immortal);
const pxr::TfToken usdLuxDomeLight1Token("UsdLuxDomeLight_1", pxr::TfToken::Immortal);
const pxr::TfToken openvdbAssetToken("openvdbAsset", pxr::TfToken::Immortal);
const pxr::TfToken houdiniFieldAssetToken("houdiniFieldAsset", pxr::TfToken::Immortal);

}

namespace hdMoonray {

pxr::TfToken
canonicalSprimType(const pxr::TfToken& type)
{
    if (type == domeLight1Token || type == usdLuxDomeLight1Token) {
        return pxr::HdPrimTypeTokens->domeLight;
    }
    return type;
}

pxr::TfToken
canonicalBprimType(const pxr::TfToken& type)
{
    if (type == houdiniFieldAssetToken) {
        return openvdbAssetToken;
    }
    return type;
}

const pxr::TfTokenVector&
supportedSprimTypeAliases()
{
    static const pxr::TfTokenVector aliases = {
        domeLight1Token,
        usdLuxDomeLight1Token,
    };
    return aliases;
}

const pxr::TfTokenVector&
supportedBprimTypeAliases()
{
    static const pxr::TfTokenVector aliases = {
        houdiniFieldAssetToken,
    };
    return aliases;
}

}
