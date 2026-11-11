// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// Handles any Moonray geometry shader : the RDL class name should be specified by
// procedural:class. Arbitrary RDL2 attribute "xyz" can be set by USD attribute 
// procedural:xyz. primvars:moonray:xyz should also work, and will override procedural:xyz
//
// handles parts, represented by (mis)using GeometrySubset, via ProceduralAdapter.
// Translates primvars to UserData, but only if the RDL class has the
// "primitive_attributes" attribute

#include "procedural.h"
#include "hydra2_utils.h"
#include "renderDelegate.h"
#include "ValueConverter.h"
#include "HdmLog.h"
#include "tokens.h"

#include <iostream>

using namespace pxr;

namespace {
    static const std::string rdlAttrPrimitiveAttributes("primitive_attributes");

}
namespace hdMoonray {

using PartMaterial = std::pair<SdfPath, SdfPath>;
using PerPartMaterials = std::vector<PartMaterial>;

HdMoonray_Procedural::HdMoonray_Procedural(SdfPath const& id) :
    HdRprim(id), 
    HdMoonray_GeometryBase(this) 
{}

void
HdMoonray_Procedural::Finalize(HdRenderParam* renderParam)
{
    // causes geometry to be hidden
    resetGeometryObject(HdMoonray_RenderDelegate::get(renderParam));
}

HdDirtyBits
HdMoonray_Procedural::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllDirty;
}

// This seems to be a GL thing to set up a shader. Sync is not called.
void
HdMoonray_Procedural::_InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits)
{
    // doing this seems to cause Sync() to be called:
    *dirtyBits |= HdChangeTracker::DirtyRepr;
}

void
HdMoonray_Procedural::syncAttributes(HdSceneDelegate* sceneDelegate, 
                           HdMoonray_RenderDelegate& renderDelegate,
                           HdDirtyBits* dirtyBits,
                           const TfToken& reprToken)
{
    PrimAccess access(GetId(), HdMoonrayTokens->procedural, sceneDelegate, renderDelegate);
    
    // sync all attributes "procedural:xyx" to RDL attr xyz
     for (auto attrIt = mGeometry.beginAttributes(); attrIt != mGeometry.endAttributes(); ++attrIt) {
        const std::string& attrName = (*attrIt).name();

        const TfToken primvarName("moonray:"+attrName);
        if (not isPrimvarUsed(primvarName)) {
            VtValue val = access.Get(TfToken(attrName));
            if (val.IsEmpty()) {
                (*attrIt).setToDefault();
            } else {
               (*attrIt).set(val);
            }
        }

        // Populate part lists
        partList.clear();
        partMaterials.clear();
        partPaths.clear();
        
        
        VtValue parts = access.Get(HdMoonrayTokens->parts);
        if (parts.IsHolding<PerPartMaterials>()) {
            const PerPartMaterials& ppm = parts.UncheckedGet<PerPartMaterials>();
            partList.reserve(ppm.size());
            partMaterials.reserve(ppm.size());
            partPaths.reserve(ppm.size());
    
            for (const auto& pm : ppm) {
                partList.push_back(pm.first.GetName());
                partPaths.push_back(pm.first);
                partMaterials.push_back(pm.second);
            }
        }
    }

    // base class handles transform and double-sided
    HdMoonray_GeometryBase::syncAttributes(sceneDelegate, renderDelegate, dirtyBits, reprToken);
}

bool
HdMoonray_Procedural::supportsUserData() 
{
    // check if the geometry supports user data, which means it has
    // a "primitive_attributes" attribute
    return mGeometry.isValid() && mGeometry.hasAttribute(rdlAttrPrimitiveAttributes);
}

void
HdMoonray_Procedural::Sync(HdSceneDelegate* sceneDelegate,
                HdRenderParam*   renderParam,
                HdDirtyBits*     dirtyBits,
                const TfToken&   reprToken)
{
    hdmLogSyncStart("Procedural", GetId(), dirtyBits);
    HdMoonray_RenderDelegate& renderDelegate(HdMoonray_RenderDelegate::get(renderParam));
    PrimAccess access(GetId(), HdMoonrayTokens->procedural, sceneDelegate, renderDelegate);
    VtValue classNameValue = access.Get(HdMoonrayTokens->_class);
    if (!classNameValue.IsHolding<TfToken>()) {
        Logger::error("No procedural:class specified for ", GetId());
        return;
    }
    const std::string& className = classNameValue.UncheckedGet<TfToken>().GetString();

    if (mGeometry.isValid() && 
        className != mGeometry.className()) {
        // class has changed, need to clear current object
        resetGeometryObject(renderDelegate);
        // force a re-sync of overything
        *dirtyBits = GetInitialDirtyBitsMask();
    }

    _UpdateVisibility(sceneDelegate, dirtyBits);
    _UpdateInstancer(sceneDelegate, dirtyBits);
    
    syncAll(className, sceneDelegate, renderDelegate, dirtyBits, reprToken);

    hdmLogSyncEnd(GetId());
}

const TfTokenVector&
HdMoonray_Procedural::GetBuiltinPrimvarNames() const
{
    static TfTokenVector none{};
    return none;
}

}
