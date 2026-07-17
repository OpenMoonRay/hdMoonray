// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ColorManagement.h"

#include <OpenColorIO/OpenColorIO.h>

#include <scene_rdl2/render/logging/logging.h>

#include <array>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>

namespace OCIO = OCIO_NAMESPACE;

namespace {

bool
isNoneToken(const pxr::TfToken& token)
{
    return token.IsEmpty() || token == pxr::TfToken("none") ||
           token == pxr::TfToken("raw") ||
           token == pxr::TfToken("data") ||
           token == pxr::TfToken("auto");
}

bool
pathExists(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    struct stat st;
    return stat(path.c_str(), &st) == 0;
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

struct ResolvedColorSpace
{
    std::string name;
    std::string method;
};

ResolvedColorSpace
resolveConfigColorSpace(const OCIO::ConstConfigRcPtr& config,
                        const std::string& token,
                        const std::string& method)
{
    if (!config || token.empty()) {
        return {};
    }

    try {
        const std::string name = getColorSpaceName(config->getColorSpace(token.c_str()));
        if (!name.empty()) {
            return {name, method};
        }
    } catch (const OCIO::Exception&) {
    }
    return {};
}

ResolvedColorSpace
resolveRoleColorSpace(const OCIO::ConstConfigRcPtr& config,
                      const char* role,
                      bool allowDefaultWarning = false)
{
    ResolvedColorSpace resolved = resolveConfigColorSpace(
            config, role ? role : "", std::string("role:") + (role ? role : ""));
    if (allowDefaultWarning && !resolved.name.empty()) {
        resolved.method += ":warning";
    }
    return resolved;
}

struct OcioConfigResult
{
    OCIO::ConstConfigRcPtr config;
    std::string loadError;
    bool rawFallback = false;
};

OcioConfigResult
currentOcioConfig()
{
    try {
        return {OCIO::GetCurrentConfig(), std::string(), false};
    } catch (const OCIO::Exception& e) {
        scene_rdl2::logging::Logger::warn(
            "Failed to load current OCIO config: ", e.what(),
            "; hdMoonray renderingColorSpace conversion is disabled");
    }

    try {
        return {OCIO::Config::CreateRaw(), "OCIO current config load failed", true};
    } catch (const OCIO::Exception& e) {
        scene_rdl2::logging::Logger::warn(
            "Failed to create raw OCIO fallback config: ", e.what());
    }
    return {};
}

ResolvedColorSpace
defaultWorkingColorSpace(const OCIO::ConstConfigRcPtr& config)
{
    if (!config) {
        return {};
    }

    for (const char* role : {OCIO::ROLE_RENDERING,
                             OCIO::ROLE_SCENE_LINEAR,
                             "default_float",
                             "reference"}) {
        ResolvedColorSpace resolved = resolveRoleColorSpace(config, role);
        if (!resolved.name.empty()) {
            return resolved;
        }
    }

    return resolveRoleColorSpace(config, OCIO::ROLE_DEFAULT, true);
}

} // namespace

namespace hdMoonray {

using scene_rdl2::logging::Logger;

struct ColorManagement::Impl
{
    Impl();

    std::string resolveColorSpace(const pxr::TfToken& token);
    bool hasUsableConfig() const;
    void rebuildProcessor();
    void transform(float* color, int channels) const;
    std::string diagnosticSummary() const;
    bool diagnosticNeedsWarning() const;
    bool ocioEnvUnset() const { return ocioEnv.empty(); }

    std::string ocioLibraryVersion;
    std::string ocioEnv;
    bool ocioFileExists = false;
    std::string configLoadError;
    bool rawFallback = false;
    pxr::TfToken renderingColorSpaceToken;
    std::string sceneLinearColorSpace;
    std::string sceneLinearResolutionMethod;
    std::string workingColorSpace;
    std::string workingResolutionMethod;
    std::string renderingRoleColorSpace;
    std::string sceneLinearRoleColorSpace;
    bool unsupportedRequestedColorSpace = false;
    bool defaultRoleFallbackWarning = false;
    OCIO::ConstConfigRcPtr colorConfig;
    OCIO::ConstProcessorRcPtr sceneLinearToWorking;
    OCIO::ConstCPUProcessorRcPtr sceneLinearToWorkingCpu;
};

ColorManagement::Impl::Impl()
    : ocioLibraryVersion(OCIO::GetVersion())
{
    const char* env = std::getenv("OCIO");
    if (env) {
        ocioEnv = env;
        ocioFileExists = pathExists(ocioEnv);
    }

    OcioConfigResult result = currentOcioConfig();
    colorConfig = result.config;
    configLoadError = result.loadError;
    rawFallback = result.rawFallback;
}

ColorManagement::ColorManagement()
    : mImpl(new Impl())
{
    mImpl->renderingRoleColorSpace =
            resolveRoleColorSpace(mImpl->colorConfig, OCIO::ROLE_RENDERING).name;
    mImpl->sceneLinearRoleColorSpace =
            resolveRoleColorSpace(mImpl->colorConfig, OCIO::ROLE_SCENE_LINEAR).name;

    const ResolvedColorSpace defaultWorking =
            defaultWorkingColorSpace(mImpl->colorConfig);
    mImpl->sceneLinearColorSpace = defaultWorking.name;
    mImpl->sceneLinearResolutionMethod = defaultWorking.method;

    if (mImpl->sceneLinearColorSpace.empty()) {
        Logger::warn("OCIO render/working color space could not be resolved from "
                     "roles rendering, scene_linear, default_float, reference, or default; "
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
ColorManagement::diagnosticSummary() const
{
    return mImpl->diagnosticSummary();
}

bool
ColorManagement::diagnosticNeedsWarning() const
{
    return mImpl->diagnosticNeedsWarning();
}

bool
ColorManagement::ocioEnvUnset() const
{
    return mImpl->ocioEnvUnset();
}

bool
ColorManagement::Impl::hasUsableConfig() const
{
    return colorConfig && !ocioEnv.empty() && ocioFileExists && !rawFallback;
}

std::string
ColorManagement::Impl::resolveColorSpace(const pxr::TfToken& token)
{
    workingResolutionMethod.clear();
    unsupportedRequestedColorSpace = false;
    defaultRoleFallbackWarning = false;

    if (isNoneToken(token) || !hasUsableConfig()) {
        workingResolutionMethod = sceneLinearResolutionMethod;
        defaultRoleFallbackWarning =
                workingResolutionMethod.find(":warning") != std::string::npos;
        return sceneLinearColorSpace;
    }

    ResolvedColorSpace resolved = resolveConfigColorSpace(
            colorConfig, token.GetString(), "authored");
    if (!resolved.name.empty()) {
        workingResolutionMethod = resolved.method;
        return resolved.name;
    }

    unsupportedRequestedColorSpace = true;
    workingResolutionMethod = sceneLinearResolutionMethod;
    defaultRoleFallbackWarning =
            workingResolutionMethod.find(":warning") != std::string::npos;
    Logger::warn("Unsupported renderingColorSpace '", token,
                 "' for current OCIO config; falling back to resolved working space '",
                 sceneLinearColorSpace, "'. No config-specific fallback names were tried.");
    return sceneLinearColorSpace;
}

void
ColorManagement::Impl::rebuildProcessor()
{
    sceneLinearToWorking.reset();
    sceneLinearToWorkingCpu.reset();

    if (!hasUsableConfig() ||
        sceneLinearColorSpace.empty() || workingColorSpace.empty() ||
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

std::string
ColorManagement::Impl::diagnosticSummary() const
{
    std::ostringstream out;
    out << "ocioLibraryVersion=" << ocioLibraryVersion
        << " OCIO=" << (ocioEnv.empty() ? "<unset>" : ocioEnv)
        << " ocioFileExists=" << (ocioFileExists ? "1" : "0")
        << " rawFallback=" << (rawFallback ? "1" : "0")
        << " requestedRenderingColorSpace="
        << (renderingColorSpaceToken.IsEmpty() ? "<default>" : renderingColorSpaceToken.GetString())
        << " renderingRole="
        << (renderingRoleColorSpace.empty() ? "<unresolved>" : renderingRoleColorSpace)
        << " sceneLinearRole="
        << (sceneLinearRoleColorSpace.empty() ? "<unresolved>" : sceneLinearRoleColorSpace)
        << " sceneLinearColorSpace="
        << (sceneLinearColorSpace.empty() ? "<unresolved>" : sceneLinearColorSpace)
        << " resolvedRenderingColorSpace="
        << (workingColorSpace.empty() ? "<unresolved>" : workingColorSpace)
        << " resolvedBy="
        << (workingResolutionMethod.empty() ? "<unresolved>" : workingResolutionMethod)
        << " conversionEnabled=" << (sceneLinearToWorkingCpu ? "1" : "0");

    if (unsupportedRequestedColorSpace) {
        out << " fallbackUsed=1";
    }
    if (defaultRoleFallbackWarning) {
        out << " defaultRoleFallbackWarning=1";
    }
    if (!configLoadError.empty()) {
        out << " configLoadError=\"" << configLoadError << "\"";
    }
    if (ocioEnv.empty()) {
        out << " disabledReason=\"OCIO is unset\"";
    } else if (!ocioFileExists) {
        out << " disabledReason=\"OCIO path does not exist\"";
    } else if (sceneLinearColorSpace.empty()) {
        out << " disabledReason=\"scene linear color space unresolved\"";
    } else if (workingColorSpace.empty()) {
        out << " disabledReason=\"rendering color space unresolved\"";
    } else if (!sceneLinearToWorkingCpu && sceneLinearColorSpace != workingColorSpace) {
        out << " disabledReason=\"OCIO processor unavailable\"";
    } else if (!sceneLinearToWorkingCpu) {
        out << " disabledReason=\"source and target color spaces match\"";
    }

    return out.str();
}

bool
ColorManagement::Impl::diagnosticNeedsWarning() const
{
    return ocioEnv.empty() || !ocioFileExists || rawFallback ||
           sceneLinearColorSpace.empty() || workingColorSpace.empty() ||
           unsupportedRequestedColorSpace || defaultRoleFallbackWarning ||
           (!sceneLinearToWorkingCpu && sceneLinearColorSpace != workingColorSpace);
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
