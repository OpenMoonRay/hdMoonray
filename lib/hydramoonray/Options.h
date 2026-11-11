// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <set>

namespace hdMoonray {
    class HdMoonray_RenderDelegate;

class Options 
{
public:
    Options(HdMoonray_RenderDelegate& renderDelegate) : mRenderDelegate(renderDelegate) {}
    ~Options() {}

    const std::string& getRdlOutput() const { return mRdlOutput; }
    void setRdlOutput(const std::string& v) { mRdlOutput = v; }

    bool isHoudini() const { return mIsHoudini; }
    bool getDisableLighting() const { return mDisableLighting; }
    void setDisableLighting(bool v);
    bool isDoubleSided() const { return mDoubleSided; }
    void setDoubleSided(bool v);
    bool getDecodeNormals() const { return mDecodeNormals; }
    bool getDecodeNormalsChanged() const { return mDecodeNormalsChanged; }
    void setDecodeNormals(bool v);
    float getMaxMeshResolution() const { return mMaxMeshResolution; }
    void setMaxMeshResolution(float v) { mMaxMeshResolution = v; }
    bool getEnableMotionBlur() const { return mEnableMotionBlur; }
    void setEnableMotionBlur(bool v) { mEnableMotionBlur = v; }
    bool getForcePolygon() const {return mForcePolygon;}

    bool getPruneProcedural(const std::string& rdlName) const
        { return mPrunedProcedurals.count(rdlName) > 0; }
    void setPruneProcedural(const std::string& rdlName, bool prune);
    bool getPruneVolume() const {return mPruneVolume;}
    void setPruneVolume(bool v);
    void setForcePolygon(bool v);
    void setIsHoudini(bool v) { mIsHoudini = v; }
    void setDisableRender(bool v) {mDisableRender = v;}
    bool getDisableRender() {return mDisableRender;}
    void setDeepIdAttrName(std::string attrName) {mDeepIdAttrName = attrName;}
    std::string  getDeepIdAttrName() {return mDeepIdAttrName;}

    bool getSimplifyPaths() const { return mSimplifyPaths; }
    void setSimplifyPaths(bool v) { mSimplifyPaths = v; }

    bool getShowAllRenderSettings() const { return mShowAllRenderSettings; }
    void setShowAllRenderSettings(bool v) { mShowAllRenderSettings = v; }
    bool getShowRenderSettingChanges() const { return mShowRenderSettingChanges; }
    void setShowRenderSettingChanges(bool v) { mShowRenderSettingChanges = v; }

    bool getShowRenderPasses() const { return mShowRenderPasses; }
    void setShowRenderPasses(bool v) { mShowRenderPasses = v; }
    
    bool getGenerateOnly() const { return mGenerateOnly; }
    void setGenerateOnly(bool v) { mGenerateOnly = v; }
    
private:
    HdMoonray_RenderDelegate& mRenderDelegate;

    bool mDisableLighting = false;
    bool mDoubleSided = true;
    bool mDecodeNormals = false;
    bool mDecodeNormalsChanged = false;
    float mMaxMeshResolution = 0.0f;
    bool mEnableMotionBlur = false;
    bool mForcePolygon = false;
    std::set<std::string> mPrunedProcedurals; // stores RDL2 name
    bool mPruneVolume = false;
    bool mDisableRender = false;
    std::string mDeepIdAttrName;
    bool mSimplifyPaths = false;
    bool mShowRenderSettingChanges = false;
    bool mShowAllRenderSettings = false;
    bool mShowRenderPasses = false;
    bool mGenerateOnly = false;

    // true when settings contains "houdini:interactive"
    bool mIsHoudini = false;

    std::string mRdlOutput;
};
}