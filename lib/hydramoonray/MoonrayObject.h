// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ValueConverter.h"
#include "HdmLog.h"

#include <scene_rdl2/scene/rdl2/SceneClass.h>
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/UserData.h>
#include <scene_rdl2/scene/rdl2/Material.h>
#include <scene_rdl2/scene/rdl2/LightSet.h>
#include <scene_rdl2/scene/rdl2/Displacement.h>
#include <scene_rdl2/scene/rdl2/VolumeShader.h>
#include <scene_rdl2/scene/rdl2/LightFilterSet.h>
#include <scene_rdl2/scene/rdl2/ShadowSet.h>
#include <scene_rdl2/scene/rdl2/ShadowReceiverSet.h>
#include <scene_rdl2/scene/rdl2/Layer.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>

#include <pxr/base/gf/quath.h>
#include <pxr/imaging/hd/tokens.h>
#include <string>


namespace hdMoonray {

namespace rdl2 = scene_rdl2::rdl2;

class MoonrayObject;

class MoonrayAttribute 
{
public:
    using Type = rdl2::AttributeType;
    using InterfaceType = rdl2::SceneObjectInterface;

    MoonrayAttribute(rdl2::SceneObject* sceneObject, 
                     const rdl2::Attribute* attribute) :
                     mSceneObject(sceneObject), mAttribute(attribute) {}
    MoonrayAttribute(rdl2::SceneObject* sceneObject, 
                     const std::string& attributeName) : 
                     mSceneObject(sceneObject), 
                     mAttribute(sceneObject->getSceneClass().getAttribute(attributeName)) 
    {}
    MoonrayAttribute() : mSceneObject(nullptr), mAttribute(nullptr) {}

    bool isNull() const { return mAttribute == nullptr; }
    bool isValid() const { return mAttribute != nullptr; }
    const std::string& name() const { return mAttribute->getName(); }
    const std::string& objectName() const { return mSceneObject->getName(); }

    template <typename T> void set(const T& value);
    template <typename T> void set(const T& value0, const T& value1);

    void setColor(const pxr::GfVec3f& value) { set(reinterpret_cast<const rdl2::Rgb&>(value)); }
    void setToDefault() { ValueConverter::setDefault(mSceneObject, mAttribute); }    
    void bind(const MoonrayObject& obj);

    Type type() const { return mAttribute->getType(); }
    bool hasInterfaceType(InterfaceType interfaceType) const { return (mAttribute->getObjectType() & interfaceType) != 0; }

private:
    rdl2::SceneObject* mSceneObject;
    const rdl2::Attribute* mAttribute;
};

class MoonrayAttrIterator
{
public:
    
    using iterator_category = std::forward_iterator_tag;
    using value_type        = MoonrayAttribute;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const MoonrayAttribute*;
    using reference         = MoonrayAttribute;

    explicit MoonrayAttrIterator(rdl2::SceneObject* sceneObject, 
                                 rdl2::SceneClass::AttributeConstIterator attributeIter) 
        : mSceneObject(sceneObject), mAttributeIter(attributeIter) 
        {}


    value_type operator*() const {
        return MoonrayAttribute(mSceneObject, *mAttributeIter);
    }

    MoonrayAttrIterator& operator++() {
        ++mAttributeIter;
        return *this;
    }

    MoonrayAttrIterator operator++(int) {
        MoonrayAttrIterator temp = *this;
        ++mAttributeIter;
        return temp;
    }

    // Equality comparisons
    bool operator==(const MoonrayAttrIterator& other) const {
        return mAttributeIter == other.mAttributeIter;
    }

    bool operator!=(const MoonrayAttrIterator& other) const {
        return mAttributeIter != other.mAttributeIter;
    }


private:
    rdl2::SceneObject* mSceneObject;
    rdl2::SceneClass::AttributeConstIterator mAttributeIter;
};

class MoonrayAssignment;

class MoonrayObject
{
public:

    using DataRate = rdl2::UserData::Rate;
    using InterfaceType = rdl2::SceneObjectInterface;

    MoonrayObject() : mSceneObject(nullptr) {}
    MoonrayObject(rdl2::SceneObject* sceneObject) : mSceneObject(sceneObject) {}

    const std::string& objectName() const { return mSceneObject->getName(); }
    const std::string& className() const { return mSceneObject->getSceneClass().getName(); }
    

    bool isNull() const { return mSceneObject == nullptr; }
    bool isValid() const { return mSceneObject != nullptr; }

    bool hasInterfaceType(InterfaceType interfaceType) const { return (mSceneObject->getSceneClass().getDeclaredInterface() & interfaceType) != 0; }

    // null object is also used to represent the beauty render output, which is not a SceneObject
    bool isBeautyOutput() const { return mSceneObject == nullptr; }

    rdl2::SceneObject* sceneObject() const { return mSceneObject; }
    template <typename T> T* sceneObjectAs() const { return mSceneObject ? mSceneObject->asA<T>() : nullptr; }

    template <typename T> void set(const std::string& name, const T& value) { getAttribute(name).set(value); }
    template <typename T> void set(const std::string& name, const T& value0, const T& value1) { getAttribute(name).set(value0, value1); }
    void setToDefault(const std::string& name) { getAttribute(name).setToDefault(); }

    MoonrayAttrIterator beginAttributes() { return MoonrayAttrIterator(mSceneObject, mSceneObject->getSceneClass().beginAttributes()); }
    MoonrayAttrIterator endAttributes() {  return MoonrayAttrIterator(mSceneObject, mSceneObject->getSceneClass().endAttributes());}
    MoonrayAttribute getAttribute(const std::string& name) { return MoonrayAttribute(mSceneObject, name); }
    bool hasAttribute(const std::string& name) const { return mSceneObject->getSceneClass().getAttribute(name) != nullptr; }

    MoonrayObject getInstanceIdData() const;
    void assign(const MoonrayObject& obj, const std::string& partName, const MoonrayAssignment& assignment);

    template <typename T> void setData(const std::string& name, const T& value, const pxr::TfToken& role) = delete;
    void setDataRate(DataRate rate) { mSceneObject->asA<rdl2::UserData>()->setRate(rate); }

    void beginUpdate() { mSceneObject->beginUpdate(); }
    void endUpdate() { mSceneObject->endUpdate(); }

    void copyFrom(const MoonrayObject& other) { mSceneObject->copyAll(*other.mSceneObject); }
    void resetToDefault() { mSceneObject->resetAllToDefault(); }
    
    // allow use of MoonrayObject in std::set and std::map
    bool operator<(const MoonrayObject& other) const { return mSceneObject < other.mSceneObject; }

protected:
    rdl2::SceneObject* mSceneObject;
};

class MoonrayObjectVector
{
public:
    friend class MoonrayAttribute;

    MoonrayObjectVector() = default;
    MoonrayObjectVector(const std::vector<MoonrayObject>& moonrayObjects) {
        mSceneObjects.reserve(moonrayObjects.size());
        for (const auto& obj : moonrayObjects) {
            mSceneObjects.push_back(obj.sceneObject());
        }
    }
    MoonrayObjectVector(MoonrayObject obj) :
        mSceneObjects{obj.sceneObject()} 
    {}

    void append(const MoonrayObject& obj) { mSceneObjects.push_back(obj.sceneObject()); }
    size_t size() const { return mSceneObjects.size(); }
    bool empty() const { return mSceneObjects.empty(); }
    MoonrayObject operator[](size_t index) const { return MoonrayObject(mSceneObjects[index]); }

private:
    MoonrayObjectVector(const rdl2::SceneObjectVector& sceneObjects) : mSceneObjects(sceneObjects) {}
    rdl2::SceneObjectVector mSceneObjects;
};

class MoonrayAssignment 
{
public: 
    MoonrayObject material;
    MoonrayObject lightSet;
    MoonrayObject displacement;
    MoonrayObject volumeShader;
    MoonrayObject lightFilterSet;
    MoonrayObject shadowSet;
    MoonrayObject shadowReceiverSet;
};

inline void 
MoonrayObject::assign(const MoonrayObject& obj, const std::string& partName, const MoonrayAssignment& assignment)
{
    rdl2::LayerAssignment la;
    if (assignment.material.isValid()) la.mMaterial = assignment.material.sceneObjectAs<rdl2::Material>();
    if (assignment.lightSet.isValid()) la.mLightSet = assignment.lightSet.sceneObjectAs<rdl2::LightSet>();
    if (assignment.displacement.isValid()) la.mDisplacement = assignment.displacement.sceneObjectAs<rdl2::Displacement>();
    if (assignment.volumeShader.isValid()) la.mVolumeShader = assignment.volumeShader.sceneObjectAs<rdl2::VolumeShader>();
    if (assignment.lightFilterSet.isValid()) la.mLightFilterSet = assignment.lightFilterSet.sceneObjectAs<rdl2::LightFilterSet>();
    if (assignment.shadowSet.isValid()) la.mShadowSet = assignment.shadowSet.sceneObjectAs<rdl2::ShadowSet>();
    if (assignment.shadowReceiverSet.isValid()) la.mShadowReceiverSet = assignment.shadowReceiverSet.sceneObjectAs<rdl2::ShadowReceiverSet>();
    mSceneObject->asA<rdl2::Layer>()->assign(obj.sceneObjectAs<rdl2::Geometry>(), partName, la);
}

inline MoonrayObject 
MoonrayObject::getInstanceIdData() const {
    rdl2::SceneObjectVector primitiveAttributes = mSceneObject->get<rdl2::SceneObjectVector>("primitive_attributes");
    if (primitiveAttributes.empty()) {
        return MoonrayObject();
    }
    return MoonrayObject(primitiveAttributes[0]);
}

inline void 
MoonrayAttribute::bind(const MoonrayObject& obj) 
{ 
    ValueConverter::setBinding(mSceneObject, mAttribute, obj.sceneObject()); 
}

template <typename T> void 
MoonrayAttribute::set(const T& value) 
{ 
    mSceneObject->set(rdl2::AttributeKey<T>(*mAttribute), value); 
}

template <typename T> void 
MoonrayAttribute::set(const T& value0, const T& value1)
{
    mSceneObject->set(rdl2::AttributeKey<T>(*mAttribute), value0, rdl2::TIMESTEP_BEGIN);
    mSceneObject->set(rdl2::AttributeKey<T>(*mAttribute), value1, rdl2::TIMESTEP_END);
}

template <>
inline void MoonrayAttribute::set<pxr::VtValue>(const pxr::VtValue& value) {
   ValueConverter::setAttribute(mSceneObject, mAttribute, value); 
}

template <>
inline void MoonrayAttribute::set<MoonrayObject>(const MoonrayObject& value) { 
    if (type() == Type::TYPE_SCENE_OBJECT) {
        set(value.sceneObject());
    } else {
        bind(value);
    }
}

template <>
inline void MoonrayAttribute::set<MoonrayObjectVector>(const MoonrayObjectVector& value) { 
    set(value.mSceneObjects);
}

template <>
inline void MoonrayAttribute::set<pxr::GfVec3f>(const pxr::GfVec3f& value) {
    set(reinterpret_cast<const rdl2::Vec3f&>(value));
}

template <>
inline void MoonrayAttribute::set<pxr::GfMatrix4d>(const pxr::GfMatrix4d& value) {
    set(reinterpret_cast<const rdl2::Mat4d&>(value));
}

template <>
inline void MoonrayAttribute::set<pxr::GfMatrix4d>(const pxr::GfMatrix4d& value0, const pxr::GfMatrix4d& value1) {
    set(reinterpret_cast<const rdl2::Mat4d&>(value0), reinterpret_cast<const rdl2::Mat4d&>(value1));
}

template <typename HTYPE,typename RTYPE>
inline void setRdlVec(MoonrayAttribute& attr, const HTYPE& value) {
    // warning: don't use this for BoolVector, which is std::deque<bool>
    if (value.empty()) {
        attr.set(std::vector<RTYPE>()); 
    } else {
        const RTYPE* p = reinterpret_cast<const RTYPE*>(&value[0]);
        attr.set(std::vector<RTYPE>(p, p + value.size()));
    }
}

template <>
inline void MoonrayAttribute::set<pxr::VtIntArray>(const pxr::VtIntArray& value) {
    setRdlVec<pxr::VtIntArray,int>(*this, value);
}

template <>
inline void MoonrayAttribute::set<pxr::VtFloatArray>(const pxr::VtFloatArray& value) {
    setRdlVec<pxr::VtFloatArray,float>(*this, value);
}

template <>
inline void MoonrayAttribute::set<pxr::VtVec2fArray>(const pxr::VtVec2fArray& value) {
    setRdlVec<pxr::VtVec2fArray,rdl2::Vec2f>(*this, value);
}

template <>
inline void MoonrayAttribute::set<pxr::VtVec3fArray>(const pxr::VtVec3fArray& value) {
    setRdlVec<pxr::VtVec3fArray,rdl2::Vec3f>(*this, value);
}

template <>
inline void MoonrayAttribute::set<pxr::VtVec4fArray>(const pxr::VtVec4fArray& value) {
    setRdlVec<pxr::VtVec4fArray,rdl2::Vec4f>(*this, value);
}

template <>
inline void MoonrayAttribute::set<pxr::VtMatrix4dArray>(const pxr::VtMatrix4dArray& value) {
    setRdlVec<pxr::VtMatrix4dArray,rdl2::Mat4d>(*this, value);
}
template <>
inline void MoonrayAttribute::set<pxr::VtQuathArray>(const pxr::VtQuathArray& value) {
    rdl2::Vec4fVector vecs;
    for (const auto& quat : value) {
        vecs.push_back(rdl2::Vec4f(float(quat.GetImaginary()[0]),
                                   float(quat.GetImaginary()[1]),
                                   float(quat.GetImaginary()[2]),
                                   float(quat.GetReal())));
    }
    set(vecs);
}

// Note: int user data cannot be output into RenderOutput
// buffers, so id-type primvars must be of float type. If the
// RenderBuffer is requested as an int format, it is translated
// from float to int in RenderBuffer::Resolve() [q.v.]

template <> 
inline void MoonrayObject::setData<float>(const std::string& name, const float& value, const pxr::TfToken&) {
    rdl2::FloatVector v{static_cast<float>(value)};
    mSceneObject->asA<rdl2::UserData>()->setFloatData(name, v);
}
template <> 
inline void MoonrayObject::setData<int>(const std::string& name, const int& value, const pxr::TfToken&) {
    rdl2::FloatVector v{static_cast<float>(value)};
    mSceneObject->asA<rdl2::UserData>()->setFloatData(name, v);
}
template <> 
inline void MoonrayObject::setData<std::string>(const std::string& name, const std::string& value, const pxr::TfToken&) {
    rdl2::StringVector v{value};
    mSceneObject->asA<rdl2::UserData>()->setStringData(name, v);
}
template <> 
inline void MoonrayObject::setData<pxr::VtFloatArray>(const std::string& name, const pxr::VtFloatArray& value, const pxr::TfToken&) {
    if (value.empty()) {
        mSceneObject->asA<rdl2::UserData>()->setFloatData(name, rdl2::FloatVector());
    } else {
        const float* p = &value[0];
        mSceneObject->asA<rdl2::UserData>()->setFloatData(name, rdl2::FloatVector(p, p + value.size()));
    }
}
template <> 
inline void MoonrayObject::setData<pxr::VtIntArray>(const std::string& name, const pxr::VtIntArray& value, const pxr::TfToken&) {
    // HDM-266 moonray does not support attribute type Int for face varying attribute, 
    // cast to float
    if (value.empty()) {
        mSceneObject->asA<rdl2::UserData>()->setFloatData(name, rdl2::FloatVector());
    } else {
        const float* p = reinterpret_cast<const float*>(&value[0]);
        mSceneObject->asA<rdl2::UserData>()->setFloatData(name, rdl2::FloatVector(p, p + value.size()));
    }
}
template <> 
inline void MoonrayObject::setData<pxr::VtUIntArray>(const std::string& name, const pxr::VtUIntArray& value, const pxr::TfToken&) {
    // HDM-266 moonray does not support attribute type Int for face varying attribute, 
    // cast to float
    if (value.empty()) {
        mSceneObject->asA<rdl2::UserData>()->setFloatData(name, rdl2::FloatVector());
    } else {
        const float* p = reinterpret_cast<const float*>(&value[0]);
        mSceneObject->asA<rdl2::UserData>()->setFloatData(name, rdl2::FloatVector(p, p + value.size()));
    }
}

template <> 
inline void MoonrayObject::setData<pxr::VtVec2fArray>(const std::string& name, const pxr::VtVec2fArray& value, const pxr::TfToken&) {
    if (value.empty()) {
        mSceneObject->asA<rdl2::UserData>()->setVec2fData(name, rdl2::Vec2fVector());
    } else {
        const rdl2::Vec2f* p = reinterpret_cast<const rdl2::Vec2f*>(&value[0]);
        mSceneObject->asA<rdl2::UserData>()->setVec2fData(name, rdl2::Vec2fVector(p, p + value.size()));
    }
}
template <> 
inline void MoonrayObject::setData<pxr::VtVec3fArray>(const std::string& name, const pxr::VtVec3fArray& value, const pxr::TfToken& role) {
    if (role == pxr::HdPrimvarRoleTokens->color) {
        if (value.empty()) {
            mSceneObject->asA<rdl2::UserData>()->setColorData(name, rdl2::RgbVector());
        } else {
            const rdl2::Rgb* p = reinterpret_cast<const rdl2::Rgb*>(&value[0]);
            mSceneObject->asA<rdl2::UserData>()->setColorData(name, rdl2::RgbVector(p, p + value.size()));
        }
    } else {
        if (value.empty()) {
            mSceneObject->asA<rdl2::UserData>()->setVec3fData(name, rdl2::Vec3fVector());
        } else {
            const rdl2::Vec3f* p = reinterpret_cast<const rdl2::Vec3f*>(&value[0]);
            mSceneObject->asA<rdl2::UserData>()->setVec3fData(name, rdl2::Vec3fVector(p, p + value.size()));
        }
    }
}

template <> 
inline void MoonrayObject::setData<pxr::VtStringArray>(const std::string& name, const pxr::VtStringArray& value, const pxr::TfToken&) {
    if (value.empty()) {
        mSceneObject->asA<rdl2::UserData>()->setStringData(name, rdl2::StringVector());
    } else {    
        const std::string* p = &value[0];
        mSceneObject->asA<rdl2::UserData>()->setStringData(name, rdl2::StringVector(p, p + value.size()));
    }
}

template <> 
inline void MoonrayObject::setData<pxr::VtBoolArray>(const std::string& name, const pxr::VtBoolArray& value, const pxr::TfToken&) {
    if (value.empty()) {
        mSceneObject->asA<rdl2::UserData>()->setBoolData(name, rdl2::BoolVector());
    } else {
        const bool* p = &value[0];
        mSceneObject->asA<rdl2::UserData>()->setBoolData(name, rdl2::BoolVector(p, p + value.size()));
    }
}

template <> 
inline void MoonrayObject::setData<pxr::GfVec3f>(const std::string& name, const pxr::GfVec3f& value, const pxr::TfToken& role) {
    if (role == pxr::HdPrimvarRoleTokens->color) {
        rdl2::RgbVector v{reinterpret_cast<const rdl2::Rgb&>(value)};
        mSceneObject->asA<rdl2::UserData>()->setColorData(name, v);
    } else {
        rdl2::Vec3fVector v{reinterpret_cast<const rdl2::Vec3f&>(value)};
        mSceneObject->asA<rdl2::UserData>()->setVec3fData(name, v);
    }
}

template <> 
inline void MoonrayObject::setData<pxr::VtValue>(const std::string& name, const pxr::VtValue& value, const pxr::TfToken& role) {
    if (value.IsHolding<pxr::VtFloatArray>()) {
        setData(name, value.UncheckedGet<pxr::VtFloatArray>(), role);
    } else if (value.IsHolding<float>()) {
        setData(name, value.UncheckedGet<float>(), role);
    } else if (value.IsHolding<double>()) {
        setData(name, float(value.UncheckedGet<double>()), role);
    } else if (value.IsHolding<pxr::VtVec2fArray>()) {
        setData(name, value.UncheckedGet<pxr::VtVec2fArray>(), role);
    } else if (value.IsHolding<pxr::VtVec3fArray>()) {
        setData(name, value.UncheckedGet<pxr::VtVec3fArray>(), role);
    } else if (value.IsHolding<pxr::GfVec3f>()) {
        setData(name, value.UncheckedGet<pxr::GfVec3f>(), role);
    } else if (value.IsHolding<pxr::VtStringArray>()) {
        setData(name, value.UncheckedGet<pxr::VtStringArray>(), role);
    } else if (value.IsHolding<std::string>()) {
        const std::string& v = value.Get<std::string>();
        setData(name, v, role);
    } else if (value.IsHolding<pxr::VtUIntArray>()) {
        // MoonrayObject converts to float data (see above)
        setData(name, value.UncheckedGet<pxr::VtUIntArray>(), role);
    } else if (value.IsHolding<pxr::VtIntArray>()) {
        // MoonrayObject converts to float data (see above)
        setData(name, value.UncheckedGet<pxr::VtIntArray>(), role);
    } else if (value.IsHolding<int>()) {
        setData(name, value.UncheckedGet<int>(), role);
    } else if (value.IsHolding<long>()) {
        setData(name, int(value.UncheckedGet<long>()), role);
    } else if (value.IsHolding<pxr::VtBoolArray>()) {
        setData(name, value.UncheckedGet<pxr::VtBoolArray>(), role);
    } else {
        Logger::warn(objectName(), ": ", value.GetTypeName(), " not translated");
    }
}



} // namespace hdMoonray
