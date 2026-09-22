// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayOutput.h"
#include "MoonrayObject.h"

#include <scene_rdl2/scene/rdl2/RenderOutput.h>

namespace hdMoonray {

namespace rdl2 = scene_rdl2::rdl2;

const std::string& MoonrayOutput::objectName() const 
{ 
    return mRenderOutput->getName(); 
}

void MoonrayOutput::setAsPrimvar(const std::string& pvName, pxr::HdFormat format)
{
    if (mRenderOutput) {
        mRenderOutput->beginUpdate();
        mRenderOutput->setActive(true);
        mRenderOutput->setResult(rdl2::RenderOutput::RESULT_PRIMITIVE_ATTRIBUTE);
        mRenderOutput->setPrimitiveAttribute(pvName);
        switch (format) {
            // these are the only formats that are supported by Moonray for primvar outputs
            case pxr::HdFormatFloat32Vec2:
                mRenderOutput->setPrimitiveAttributeType(rdl2::RenderOutput::PRIMITIVE_ATTRIBUTE_TYPE_VEC2F);
                break;
            case pxr::HdFormatFloat32Vec3:
                mRenderOutput->setPrimitiveAttributeType(rdl2::RenderOutput::PRIMITIVE_ATTRIBUTE_TYPE_VEC3F);
                break;
            default:
                mRenderOutput->setPrimitiveAttributeType(rdl2::RenderOutput::PRIMITIVE_ATTRIBUTE_TYPE_FLOAT);
                break;
        }
        // int is generated as float, and converted back to int when the aov buffer is resolved. However we
        // also need to set the math filter to closest 
        if (format == pxr::HdFormatInt32) {
            mRenderOutput->setMathFilter(rdl2::RenderOutput::MATH_FILTER_CLOSEST);
        }
        mRenderOutput->endUpdate();
    }
}

std::string MoonrayOutput::getPrimvarName() const
{
    if (mRenderOutput) {
        return mRenderOutput->getPrimitiveAttribute();
    }
    return std::string("no output");
}

void MoonrayOutput::setAsCryptomatte()
{
    if (mRenderOutput) {
        mRenderOutput->beginUpdate();
        mRenderOutput->setActive(true);
        mRenderOutput->setResult(rdl2::RenderOutput::RESULT_CRYPTOMATTE);
        mRenderOutput->endUpdate();
    }
}
void MoonrayOutput::setAsDepth()
{
    if (mRenderOutput) {
        mRenderOutput->beginUpdate();
        mRenderOutput->setActive(true);
        mRenderOutput->setResult(rdl2::RenderOutput::RESULT_DEPTH);
        mRenderOutput->setMathFilter(rdl2::RenderOutput::MATH_FILTER_MIN);
        mRenderOutput->endUpdate();
    }
}

void MoonrayOutput::setAsNormal()
{
    if (mRenderOutput) {
        mRenderOutput->beginUpdate();
        mRenderOutput->setActive(true);
        mRenderOutput->setResult(rdl2::RenderOutput::RESULT_STATE_VARIABLE);
        mRenderOutput->setStateVariable(rdl2::RenderOutput::STATE_VARIABLE_N);
        mRenderOutput->endUpdate();
    }
}

void MoonrayOutput::setAsSt()
{
    if (mRenderOutput) {
        mRenderOutput->beginUpdate();
        mRenderOutput->setActive(true);
        mRenderOutput->setResult(rdl2::RenderOutput::RESULT_STATE_VARIABLE);
        mRenderOutput->setStateVariable(rdl2::RenderOutput::STATE_VARIABLE_ST);
        mRenderOutput->endUpdate();
    }
}

void MoonrayOutput::setAsLpe(const std::string& lpeName)
{
    if (mRenderOutput) {
        mRenderOutput->beginUpdate();
        mRenderOutput->setActive(true);
        mRenderOutput->setResult(rdl2::RenderOutput::RESULT_LIGHT_AOV);
        mRenderOutput->setLpe(lpeName);
        mRenderOutput->endUpdate();
    }
}

void MoonrayOutput::setAsShader(const std::string& shaderName)
{
    if (mRenderOutput) {
        mRenderOutput->beginUpdate();
        mRenderOutput->setActive(true);
        mRenderOutput->setResult(rdl2::RenderOutput::RESULT_MATERIAL_AOV);
        mRenderOutput->setMaterialAov(shaderName);
        mRenderOutput->endUpdate();
    }
}

void MoonrayOutput::applySettings(const pxr::VtDictionary& aovSettings)
{
    if (mRenderOutput) {
        MoonrayObject obj(mRenderOutput);
        obj.beginUpdate();
        for (auto attrIt = obj.beginAttributes(); attrIt != obj.endAttributes(); ++attrIt) {
            const std::string& attrName = (*attrIt).name();
            pxr::TfToken key = pxr::TfToken("parameters:moonray:" + attrName);
            const auto valueIt = aovSettings.find(key.GetString());
            if (valueIt != aovSettings.end() && !valueIt->second.IsEmpty()) {
                (*attrIt).set(valueIt->second);
            }
        }
        obj.endUpdate();
    }
}

void MoonrayOutput::deactivate() {
    if (mRenderOutput) {
        mRenderOutput->beginUpdate();
        mRenderOutput->setActive(false);
        mRenderOutput->endUpdate();
    }
}

void MoonrayOutput::setFileName(const std::string& fileName) {
    if (mRenderOutput) {
        mRenderOutput->beginUpdate();
        mRenderOutput->setFileName(fileName);
        mRenderOutput->endUpdate();
    }
}
} // namespace hdMoonray

