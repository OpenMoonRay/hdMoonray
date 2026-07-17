// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <pxr/base/tf/token.h>

namespace hdMoonray {

/// Return the canonical Hydra sprim token MoonRay uses internally.
pxr::TfToken canonicalSprimType(const pxr::TfToken& type);

/// Return the canonical Hydra bprim token MoonRay uses internally.
pxr::TfToken canonicalBprimType(const pxr::TfToken& type);

/// Houdini/USD schema-version aliases that should be advertised as supported.
const pxr::TfTokenVector& supportedSprimTypeAliases();

/// Houdini-specific field aliases that should be advertised as supported.
const pxr::TfTokenVector& supportedBprimTypeAliases();

}
