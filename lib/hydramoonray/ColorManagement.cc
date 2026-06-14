// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ColorManagement.h"

#include <OpenColorIO/OpenColorIO.h>

#include <scene_rdl2/render/logging/logging.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace OCIO = OCIO_NAMESPACE;

namespace {

std::string
normalize(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       if (c == '-' || c == ' ' || c == '.') return '_';
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool
isNoneToken(const pxr::TfToken& token)
{
    return token.IsEmpty() || token == pxr::TfToken("none") ||
           token == pxr::TfToken("raw");
}

std::vector<std::string>
candidateNamesForToken(const pxr::TfToken& token)
{
    const std::string text = token.GetString();
    const std::string key = normalize(text);

    if (key == "lin_rec709_scene" || key == "srgb_rec709_scene" ||
        key == "linear_rec709" || key == "linear_rec709_srgb" ||
        key == "scene_linear") {
        return {
            text,
            "lin_rec709_scene",
            "srgb_rec709_scene",
            "Linear Rec.709 (sRGB)",
            "Linear Rec.709",
            "scene_linear"
        };
    }
    if (key == "lin_ap1_scene" || key == "acescg" ||
        key == "acescg_scene_linear") {
        return {
            text,
            "lin_ap1_scene",
            "ACEScg",
            "ACES - ACEScg",
            "ACEScg - ACEScg"
        };
    }
    if (key == "lin_rec2020_scene" || key == "linear_rec2020" ||
        key == "rec2020_linear") {
        return {
            text,
            "lin_rec2020_scene",
            "Linear Rec.2020",
            "Linear Rec.2020 (Rec.2020)",
            "Rec.2020"
        };
    }
    return {text};
}

std::string
firstExistingColorSpace(const OCIO::ConstConfigRcPtr& config,
                        const std::vector<std::string>& candidates)
{
    for (const std::string& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        try {
            if (config->getColorSpace(candidate.c_str())) {
                return candidate;
            }
            // OCIO accepts roles in places where color space names are accepted.
            // Some configs do not expose the role as a named ColorSpace, so test
            // processor construction as the compatibility fallback.
            if (config->getProcessor(candidate.c_str(), candidate.c_str())) {
                return candidate;
            }
        } catch (const OCIO::Exception&) {
            continue;
        }
    }
    return {};
}

std::string
getColorSpaceName(const OCIO::ConstColorSpaceRcPtr& colorSpace)
{
    if (colorSpace) {
        const char* name = colorSpace->getName();
        if (name && name[0]) {
            return name;
        }
    }
    return {};
}

OCIO::ConstConfigRcPtr
currentOcioConfig()
{
    try {
        return OCIO::GetCurrentConfig();
    } catch (const OCIO::Exception& e) {
        scene_rdl2::logging::Logger::warn(
            "Failed to load current OCIO config: ", e.what(),
            "; hdMoonray renderingColorSpace conversion is disabled");
    }

    try {
        return OCIO::Config::CreateRaw();
    } catch (const OCIO::Exception& e) {
        scene_rdl2::logging::Logger::warn(
            "Failed to create raw OCIO fallback config: ", e.what());
    }
    return {};
}

std::string
sceneLinearColorSpaceName(const OCIO::ConstConfigRcPtr& config)
{
    if (!config) {
        return {};
    }

    try {
        std::string roleName = getColorSpaceName(config->getColorSpace(OCIO::ROLE_SCENE_LINEAR));
        if (!roleName.empty()) {
            return roleName;
        }
    } catch (const OCIO::Exception&) {
        // Fall back to common config names below.
    }

    std::string resolved = firstExistingColorSpace(config, {
        OCIO::ROLE_SCENE_LINEAR,
        "scene_linear",
        "lin_rec709_scene",
        "srgb_rec709_scene",
        "Linear Rec.709 (sRGB)",
        "Linear Rec.709"
    });
    if (!resolved.empty()) {
        return resolved;
    }

    return {};
}

bool
hasNoOpProcessor(const OCIO::ConstConfigRcPtr& config,
                 const std::string& colorSpace)
{
    if (!config || colorSpace.empty()) {
        return false;
    }
    try {
        return static_cast<bool>(config->getProcessor(colorSpace.c_str(),
                                                      colorSpace.c_str()));
    } catch (const OCIO::Exception&) {
        return false;
    }
}

std::string
firstValidProcessorEndpoint(const OCIO::ConstConfigRcPtr& config,
                            const std::vector<std::string>& candidates)
{
    for (const std::string& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        if (hasNoOpProcessor(config, candidate)) {
            return candidate;
        }
    }
    return {};
}

} // namespace

namespace hdMoonray {

using scene_rdl2::logging::Logger;

struct ColorManagement::Impl
{
    Impl()
        : colorConfig(currentOcioConfig())
    {}

    std::string resolveColorSpace(const pxr::TfToken& token) const;
    void rebuildProcessor();
    void transform(float* color, int channels) const;

    pxr::TfToken renderingColorSpaceToken;
    std::string sceneLinearColorSpace;
    std::string workingColorSpace;
    OCIO::ConstConfigRcPtr colorConfig;
    OCIO::ConstProcessorRcPtr sceneLinearToWorking;
    OCIO::ConstCPUProcessorRcPtr sceneLinearToWorkingCpu;
};

ColorManagement::ColorManagement()
    : mImpl(new Impl())
{
    mImpl->sceneLinearColorSpace = sceneLinearColorSpaceName(mImpl->colorConfig);

    if (mImpl->sceneLinearColorSpace.empty()) {
        Logger::warn("OCIO scene_linear role / Linear Rec.709 color space not found; "
                     "hdMoonray renderingColorSpace conversion is disabled");
    }
    mImpl->workingColorSpace = mImpl->resolveColorSpace(pxr::TfToken());
    mImpl->rebuildProcessor();
}

ColorManagement::~ColorManagement() = default;

bool
ColorManagement::setRenderingColorSpace(const pxr::TfToken& token)
{
    if (token == mImpl->renderingColorSpaceToken) {
        return false;
    }

    mImpl->renderingColorSpaceToken = token;
    mImpl->workingColorSpace = mImpl->resolveColorSpace(token);
    mImpl->rebuildProcessor();
    return true;
}

const pxr::TfToken&
ColorManagement::renderingColorSpaceToken() const
{
    return mImpl->renderingColorSpaceToken;
}

const std::string&
ColorManagement::workingColorSpace() const
{
    return mImpl->workingColorSpace;
}

bool
ColorManagement::hasWorkingColorTransform() const
{
    return static_cast<bool>(mImpl->sceneLinearToWorkingCpu);
}

std::string
ColorManagement::Impl::resolveColorSpace(const pxr::TfToken& token) const
{
    if (isNoneToken(token)) {
        return sceneLinearColorSpace;
    }

    std::string resolved = firstValidProcessorEndpoint(colorConfig, candidateNamesForToken(token));
    if (!resolved.empty()) {
        return resolved;
    }

    Logger::warn("Unsupported renderingColorSpace '", token,
                 "' for current OCIO config; falling back to '",
                 sceneLinearColorSpace, "'");
    return sceneLinearColorSpace;
}

void
ColorManagement::Impl::rebuildProcessor()
{
    sceneLinearToWorking.reset();
    sceneLinearToWorkingCpu.reset();

    if (sceneLinearColorSpace.empty() || workingColorSpace.empty() ||
        sceneLinearColorSpace == workingColorSpace) {
        return;
    }

    if (!colorConfig) {
        Logger::warn("Failed to create OCIO processor from '", sceneLinearColorSpace,
                     "' to '", workingColorSpace,
                     "'; hdMoonray renderingColorSpace conversion is disabled");
        return;
    }

    try {
        sceneLinearToWorking =
            colorConfig->getProcessor(sceneLinearColorSpace.c_str(),
                                      workingColorSpace.c_str());
        if (sceneLinearToWorking) {
            sceneLinearToWorkingCpu = sceneLinearToWorking->getDefaultCPUProcessor();
        }
    } catch (const OCIO::Exception& e) {
        Logger::warn("Failed to create OCIO processor from '", sceneLinearColorSpace,
                     "' to '", workingColorSpace, "': ", e.what(),
                     "; hdMoonray renderingColorSpace conversion is disabled");
    }
}

void
ColorManagement::Impl::transform(float* color, int channels) const
{
    if (!sceneLinearToWorkingCpu || channels < 3) {
        return;
    }
    try {
        if (channels >= 4) {
            sceneLinearToWorkingCpu->applyRGBA(color);
        } else {
            sceneLinearToWorkingCpu->applyRGB(color);
        }
    } catch (const OCIO::Exception& e) {
        Logger::warn("Failed to apply OCIO renderingColorSpace transform: ", e.what());
    }
}

scene_rdl2::rdl2::Rgb
ColorManagement::toWorkingSpace(const scene_rdl2::rdl2::Rgb& color) const
{
    std::array<float, 3> values = { color.r, color.g, color.b };
    mImpl->transform(values.data(), 3);
    return scene_rdl2::rdl2::Rgb(values[0], values[1], values[2]);
}

scene_rdl2::rdl2::Rgba
ColorManagement::toWorkingSpace(const scene_rdl2::rdl2::Rgba& color) const
{
    std::array<float, 4> values = { color.r, color.g, color.b, color.a };
    mImpl->transform(values.data(), 4);
    return scene_rdl2::rdl2::Rgba(values[0], values[1], values[2], values[3]);
}

pxr::GfVec3f
ColorManagement::toWorkingSpace(const pxr::GfVec3f& color) const
{
    std::array<float, 3> values = { color[0], color[1], color[2] };
    mImpl->transform(values.data(), 3);
    return pxr::GfVec3f(values[0], values[1], values[2]);
}

pxr::GfVec4f
ColorManagement::toWorkingSpace(const pxr::GfVec4f& color) const
{
    std::array<float, 4> values = { color[0], color[1], color[2], color[3] };
    mImpl->transform(values.data(), 4);
    return pxr::GfVec4f(values[0], values[1], values[2], values[3]);
}

} // namespace hdMoonray
