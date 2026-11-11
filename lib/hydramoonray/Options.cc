// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "Options.h"
#include "HdmLog.h"
#include "renderDelegate.h"

#include <pxr/imaging/hd/changeTracker.h>

namespace hdMoonray {

void Options::setDisableLighting(bool v)
{
    if (v != mDisableLighting) {
        mDisableLighting = v;
        mRenderDelegate.markAllLightsDirty(pxr::HdChangeTracker::AllDirty);
    }
}

void Options::setPruneProcedural(const std::string& name, bool prune)
{
    if (prune != getPruneProcedural(name)) {
        if (prune) mPrunedProcedurals.insert(name);
        else mPrunedProcedurals.erase(name);
        mRenderDelegate.markAllProceduralsDirty(pxr::HdChangeTracker::DirtyVisibility);
    }
}

void Options::setPruneVolume(bool v)
{
    if (v != mPruneVolume) {
        mPruneVolume = v;
        mRenderDelegate.markAllVolumesDirty(pxr::HdChangeTracker::DirtyVisibility);
    }
}

void Options::setDecodeNormals(bool v)
{
    mDecodeNormalsChanged = false;
    if (v != mDecodeNormals) {
        mDecodeNormals = v;
        mRenderDelegate.markAllRprimsDirty(pxr::HdChangeTracker::DirtyMaterialId);
        mDecodeNormalsChanged = true;
    }
}

void Options::setDoubleSided(bool v)
{
    if (v != mDoubleSided) {
        mDoubleSided = v;
        mRenderDelegate.markAllRprimsDirty(pxr::HdChangeTracker::DirtyDoubleSided);
    }
}

void Options::setForcePolygon(bool v)
{
    if (v != mForcePolygon) {
        mForcePolygon = v;
        mRenderDelegate.markAllRprimsDirty(pxr::HdChangeTracker::DirtySubdivTags);
    }
}

}