// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ValueConverter.h"
#include "ColorManagement.h"

#include <scene_rdl2/render/logging/logging.h>

#include <cstdint>
#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

#include <pxr/usd/sdf/assetPath.h>

namespace hdMoonray {

using scene_rdl2::logging::Logger;

static void _clearBinding(SceneObject* sceneObj, const Attribute* attribute)
{
    if (attribute->isBindable())
        sceneObj->setBinding(*attribute,nullptr);
}

static bool _extractIntegral64(const pxr::VtValue& val, std::int64_t* out)
{
    if (val.IsHolding<int>()) {
        *out = static_cast<std::int64_t>(val.UncheckedGet<int>());
        return true;
    }
    if (val.IsHolding<long>()) {
        *out = static_cast<std::int64_t>(val.UncheckedGet<long>());
        return true;
    }
    if (val.IsHolding<long long>()) {
        *out = static_cast<std::int64_t>(val.UncheckedGet<long long>());
        return true;
    }
    return false;
}

static bool _narrowIntChecked(SceneObject* sceneObj,
                              const Attribute* attribute,
                              std::int64_t value,
                              Int* out)
{
    constexpr std::int64_t kMin = static_cast<std::int64_t>(std::numeric_limits<Int>::min());
    constexpr std::int64_t kMax = static_cast<std::int64_t>(std::numeric_limits<Int>::max());
    if (value < kMin || value > kMax) {
        Logger::error(sceneObj->getName(), '.', attribute->getName(),
                      ": integer value ", value, " out of Int range [", kMin, ", ", kMax, "]");
        return false;
    }
    *out = static_cast<Int>(value);
    return true;
}

static bool _setFloatFromIntegral64(SceneObject* sceneObj,
                                    const Attribute* attribute,
                                    std::int64_t value)
{
    const long double fMin = -static_cast<long double>(std::numeric_limits<Float>::max());
    const long double fMax = static_cast<long double>(std::numeric_limits<Float>::max());
    const long double wideValue = static_cast<long double>(value);
    if (wideValue < fMin || wideValue > fMax) {
        Logger::error(sceneObj->getName(), '.', attribute->getName(),
                      ": integer value ", value, " out of Float range");
        return false;
    }
    sceneObj->set(AttributeKey<Float>(*attribute), static_cast<Float>(value));
    _clearBinding(sceneObj, attribute);
    return true;
}

static std::string _normalizedToken(const std::string& value)
{
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) {
                       if (c == '-' || c == ' ') return '_';
                       return static_cast<char>(std::tolower(c));
                   });
    return result;
}

static bool _rampInterpolationTokenToInt(const std::string& token, Int* out)
{
    const std::string key = _normalizedToken(token);

    // MoonRay ramp interpolation enums are documented in the generated
    // scene class metadata for ramp IntVector attributes:
    // None: 0 | Linear: 1 | Exponential Up: 2 | Exponential Down: 3 |
    // Smooth: 4 | Catmull Rom: 5 | Monotone Cubic: 6
    if (key == "none" || key == "constant") {
        *out = 0;
        return true;
    }
    if (key == "linear") {
        *out = 1;
        return true;
    }
    if (key == "exponential_up" || key == "ease_in") {
        *out = 2;
        return true;
    }
    if (key == "exponential_down" || key == "ease_out") {
        *out = 3;
        return true;
    }
    // Houdini ramp menus can author Hermite, Bezier, and B-spline tokens.
    // MoonRay's native ramp metadata has one smooth/cubic interpolation slot,
    // so those Houdini spline-style bases map to Smooth.
    if (key == "smooth" || key == "smoothstep" || key == "hermite" ||
        key == "bezier" || key == "bspline" || key == "b_spline") {
        *out = 4;
        return true;
    }
    if (key == "catmull_rom" || key == "catmullrom") {
        *out = 5;
        return true;
    }
    if (key == "monotone_cubic" || key == "monotonecubic") {
        *out = 6;
        return true;
    }
    return false;
}

static bool _setRampInterpolationVectorFromToken(SceneObject* sceneObj,
                                                 const Attribute* attribute,
                                                 const std::string& token)
{
    const std::string attrName = attribute->getName();
    if (attrName.find("interpolation") == std::string::npos) {
        return false;
    }

    Int interpolation = 0;
    if (!_rampInterpolationTokenToInt(token, &interpolation)) {
        Logger::warn(sceneObj->getName(), '.', attrName,
                     ": unsupported ramp interpolation token '", token,
                     "' for IntVector attribute; keeping the MoonRay default");
        return true;
    }

    IntVector values = attribute->getDefaultValue<IntVector>();
    if (values.empty()) {
        values.push_back(interpolation);
    } else {
        std::fill(values.begin(), values.end(), interpolation);
    }
    sceneObj->set(AttributeKey<IntVector>(*attribute), values);
    _clearBinding(sceneObj, attribute);
    return true;
}

template<typename T>
static void _set(SceneObject* sceneObj, const Attribute* attribute, const T& value) {
    sceneObj->set(AttributeKey<T>(*attribute), value);
}

// Need class wrapper to allow partial specialization
template <typename T, typename H>
struct RefConverter { static const T& _(const H&); };

// cases where hydra and rdl2 use the same type
template<typename T>
struct RefConverter<T,T> { static const T& _(const T& v) { return v; }};

// cases where the types can be cast in-place
#define PTRCAST(T,H)                                                     \
    template<> struct RefConverter<T,H> { static const T& _(const H& v) { return reinterpret_cast<const T&>(v); }}

PTRCAST(Bool, bool);
PTRCAST(Double, double);
PTRCAST(Float, float);
PTRCAST(Int, int);
PTRCAST(Long, long);
PTRCAST(String, std::string);

PTRCAST(Vec2f, pxr::GfVec2f);
PTRCAST(Vec2d, pxr::GfVec2d);
PTRCAST(Vec3f, pxr::GfVec3f);
PTRCAST(Rgb,   pxr::GfVec3f);
PTRCAST(Rgb,   pxr::GfVec4f); // does not work for vector<Rgb>!
PTRCAST(Vec3d, pxr::GfVec3d);
PTRCAST(Vec4f, pxr::GfVec4f);
PTRCAST(Rgba,  pxr::GfVec4f);
PTRCAST(Vec4d, pxr::GfVec4d);
PTRCAST(Mat3f, pxr::GfMatrix3f);
PTRCAST(Mat3d, pxr::GfMatrix3d);
PTRCAST(Mat4f, pxr::GfMatrix4f);
PTRCAST(Mat4d, pxr::GfMatrix4d);

// Token to string
template<> struct RefConverter<String, pxr::TfToken> {
    static const String& _(const pxr::TfToken& v) { return v.GetString(); }
};

// use first entry of vector
template<> struct RefConverter<Float, pxr::GfVec3f> {
    static const Float& _(const pxr::GfVec3f& v) { return v[0]; }
};

template <typename T, typename H>
static const T& _convertRef(const H& h) { return RefConverter<T,H>::_(h); }

template <typename T, typename H>
static bool _setAttributeRef(SceneObject* sceneObj, const Attribute* attribute, const pxr::VtValue& val)
{
    if (val.IsHolding<H>()) {
        _set(sceneObj, attribute, _convertRef<T,H>(val.UncheckedGet<H>()));
        _clearBinding(sceneObj,attribute);
        return true;
    } else {
        return false;
    }
}

// Non-reference version for when a new object must be constructed
template <typename T, typename H>
struct Converter { static T _(const H& v) { return v; } };

// Arrays of the types that can be cast in-place
template<typename T, typename H>
struct Converter<std::vector<T>, pxr::VtArray<H>>
{
    static std::vector<T> _(const pxr::VtArray<H>& v) {
        if (v.empty()) return {};
        const T* p = &(RefConverter<T,H>::_(v[0]));
        return std::vector<T>(p, p + v.size());
    }
};

// Rdl2 uses a deque rather than a vector for bool arrays
template<>
struct Converter<BoolVector, pxr::VtArray<bool>>
{
    static BoolVector _(const pxr::VtArray<bool>& v) {
        if (v.empty()) return {};
        return BoolVector(&v[0], &v[0] + v.size());
    }
};

// float->double converters
template<>
struct Converter<Vec3d, pxr::GfVec3f>
{
    static Vec3d _(const pxr::GfVec3f& v) {
        return Vec3d(v[0],v[1],v[2]);
    }
};

template<>
struct Converter<Mat3f, pxr::GfMatrix3d>
{
    static Mat3f _(const pxr::GfMatrix3d& v) {
        const double* d = v.GetArray();
        return Mat3f(d[0],d[1],d[2],
                     d[3],d[4],d[5],
                     d[6],d[7],d[8]);
    }
};

template<>
struct Converter<Mat4f, pxr::GfMatrix4d>
{
    static Mat4f _(const pxr::GfMatrix4d& v) {
        const double* d = v.GetArray();
        return Mat4f(d[0], d[1], d[2], d[3],
                     d[4], d[5], d[6], d[7],
                     d[8], d[9], d[10],d[11],
                     d[12],d[13],d[14],d[15]);
    }
};

template <typename T, typename H>
static T _convert(const H& h) { return Converter<T,H>::_(h); }

template <typename T, typename H>
static bool _setAttribute(SceneObject* sceneObj, const Attribute* attribute, const pxr::VtValue& val)
{
    if (val.IsHolding<H>()) {
        _set(sceneObj, attribute, _convert<T,H>(val.UncheckedGet<H>()));
        _clearBinding(sceneObj,attribute);
        return true;
    } else {
        return false;
    }
}

static bool
_setRgbAttribute(SceneObject* sceneObj,
                 const Attribute* attribute,
                 const pxr::VtValue& val,
                 const ColorManagement* colorManagement)
{
    if (val.IsHolding<pxr::GfVec3f>()) {
        pxr::GfVec3f color = val.UncheckedGet<pxr::GfVec3f>();
        if (colorManagement) {
            color = colorManagement->toWorkingSpace(color);
        }
        sceneObj->set(AttributeKey<Rgb>(*attribute), Rgb(color[0], color[1], color[2]));
        _clearBinding(sceneObj, attribute);
        return true;
    }
    if (val.IsHolding<pxr::GfVec4f>()) {
        const pxr::GfVec4f rgba = val.UncheckedGet<pxr::GfVec4f>();
        pxr::GfVec3f color(rgba[0], rgba[1], rgba[2]);
        if (colorManagement) {
            color = colorManagement->toWorkingSpace(color);
        }
        sceneObj->set(AttributeKey<Rgb>(*attribute), Rgb(color[0], color[1], color[2]));
        _clearBinding(sceneObj, attribute);
        return true;
    }
    return false;
}

static bool
_setRgbaAttribute(SceneObject* sceneObj,
                  const Attribute* attribute,
                  const pxr::VtValue& val,
                  const ColorManagement* colorManagement)
{
    if (val.IsHolding<pxr::GfVec4f>()) {
        pxr::GfVec4f color = val.UncheckedGet<pxr::GfVec4f>();
        if (colorManagement) {
            color = colorManagement->toWorkingSpace(color);
        }
        sceneObj->set(AttributeKey<Rgba>(*attribute), Rgba(color[0], color[1], color[2], color[3]));
        _clearBinding(sceneObj, attribute);
        return true;
    }
    return false;
}

static bool
_setRgbVectorAttribute(SceneObject* sceneObj,
                       const Attribute* attribute,
                       const pxr::VtValue& val,
                       const ColorManagement* colorManagement)
{
    if (!val.IsHolding<pxr::VtArray<pxr::GfVec3f>>()) {
        return false;
    }
    const pxr::VtArray<pxr::GfVec3f>& input =
        val.UncheckedGet<pxr::VtArray<pxr::GfVec3f>>();
    RgbVector output;
    output.reserve(input.size());
    for (pxr::GfVec3f color : input) {
        if (colorManagement) {
            color = colorManagement->toWorkingSpace(color);
        }
        output.emplace_back(color[0], color[1], color[2]);
    }
    sceneObj->set(AttributeKey<RgbVector>(*attribute), output);
    _clearBinding(sceneObj, attribute);
    return true;
}

static bool
_setRgbaVectorAttribute(SceneObject* sceneObj,
                        const Attribute* attribute,
                        const pxr::VtValue& val,
                        const ColorManagement* colorManagement)
{
    if (!val.IsHolding<pxr::VtArray<pxr::GfVec4f>>()) {
        return false;
    }
    const pxr::VtArray<pxr::GfVec4f>& input =
        val.UncheckedGet<pxr::VtArray<pxr::GfVec4f>>();
    RgbaVector output;
    output.reserve(input.size());
    for (pxr::GfVec4f color : input) {
        if (colorManagement) {
            color = colorManagement->toWorkingSpace(color);
        }
        output.emplace_back(color[0], color[1], color[2], color[3]);
    }
    sceneObj->set(AttributeKey<RgbaVector>(*attribute), output);
    _clearBinding(sceneObj, attribute);
    return true;
}

void
ValueConverter::setAttribute(SceneObject* sceneObj, const Attribute* attribute, const pxr::VtValue& val)
{
    setAttribute(sceneObj, attribute, val, nullptr);
}

void
ValueConverter::setAttribute(SceneObject* sceneObj, const Attribute* attribute, const pxr::VtValue& val,
                             const ColorManagement* colorManagement)
{
    switch(attribute->getType()) {
    case TYPE_BOOL:
        if (val.IsHolding<long>()) {
            sceneObj->set(AttributeKey<Bool>(*attribute), static_cast<bool>(val.UncheckedGet<long>()));
            return;
        }
        if (_setAttributeRef<Bool, bool>(sceneObj, attribute, val)) return;
        if (_setAttribute<Bool, int>(sceneObj, attribute, val)) return; // used by show_specular by Houdini
        break;
    case TYPE_INT:
        if (attribute->isEnumerable()) {
            const std::string* key = nullptr;
            if (val.IsHolding<pxr::TfToken>()) {
                key = &(val.UncheckedGet<pxr::TfToken>().GetString());
            } else if (val.IsHolding<std::string>()) {
                key = &(val.UncheckedGet<std::string>());
            } else {
                std::int64_t int64Val = 0;
                if (!_extractIntegral64(val, &int64Val)) {
                    break; // go print normal error message
                }
                Int intVal = 0;
                if (!_narrowIntChecked(sceneObj, attribute, int64Val, &intVal)) {
                    return;
                }
                int index = 0;
                for (auto it = attribute->beginEnumValues(); it != attribute->endEnumValues(); ++it) {
                    if (index == intVal) {
                        sceneObj->set(AttributeKey<Int>(*attribute), it->first);
                        return;
                    }
                    ++index;
                }
                break;
            }
            for (auto it = attribute->beginEnumValues(); it != attribute->endEnumValues(); ++it) {
                if (it->second == *key) {
                    _set(sceneObj, attribute, it->first);
                    _clearBinding(sceneObj,attribute);
                    return;
                }
            }
            Logger::error(sceneObj->getName(), '.', attribute->getName(),
                          ": Invalid enum key '", *key, "'");
            return;
        } else {
            std::int64_t int64Val = 0;
            if (_extractIntegral64(val, &int64Val)) {
                Int intVal = 0;
                if (!_narrowIntChecked(sceneObj, attribute, int64Val, &intVal)) {
                    return;
                }
                sceneObj->set(AttributeKey<Int>(*attribute), intVal);
                return;
            }
            if (_setAttributeRef<Int, int>(sceneObj, attribute, val)) return;
        }
        break;
    case TYPE_LONG:
        if (_setAttributeRef<Long, long>(sceneObj, attribute, val)) return;
        break;
    case TYPE_FLOAT:
        {
            std::int64_t int64Val = 0;
            if (_extractIntegral64(val, &int64Val)) {
                _setFloatFromIntegral64(sceneObj, attribute, int64Val);
                return;
            }
        }
        if (val.IsHolding<unsigned long long>()) {
            const unsigned long long ullVal = val.UncheckedGet<unsigned long long>();
            if (static_cast<long double>(ullVal) >
                    static_cast<long double>(std::numeric_limits<Float>::max())) {
                Logger::error(sceneObj->getName(), '.', attribute->getName(),
                              ": unsigned integer value ", ullVal, " out of Float range");
                return;
            }
            sceneObj->set(AttributeKey<Float>(*attribute), static_cast<Float>(ullVal));
            _clearBinding(sceneObj, attribute);
            return;
        }
        if (_setAttributeRef<Float, float>(sceneObj, attribute, val)) return;
        if (_setAttribute<Float, double>(sceneObj, attribute, val)) return;
        // handle incorrect types in Input bindings
        if (_setAttributeRef<Float, pxr::GfVec3f>(sceneObj, attribute, val)) return;
        break;
    case TYPE_DOUBLE:
        if (_setAttributeRef<Double, double>(sceneObj, attribute, val)) return;
        if (_setAttribute<Double, float>(sceneObj, attribute, val)) return;
        break;
    case TYPE_STRING:
        if (attribute->isFilename() &&
            val.IsHolding<pxr::SdfAssetPath>()) {
            const pxr::SdfAssetPath& assetPath = val.UncheckedGet<pxr::SdfAssetPath>();
            _set(sceneObj, attribute, assetPath.GetResolvedPath());
            return;
        }
        if (_setAttributeRef<String, std::string>(sceneObj, attribute, val)) return;
        if (_setAttributeRef<String, pxr::TfToken>(sceneObj, attribute, val)) return;
        break;
    case TYPE_RGB:
        if (_setRgbAttribute(sceneObj, attribute, val, colorManagement)) return;
        break;
    case TYPE_RGBA:
        if (_setRgbaAttribute(sceneObj, attribute, val, colorManagement)) return;
        break;
    case TYPE_VEC2F:
        if (_setAttributeRef<Vec2f, pxr::GfVec2f>(sceneObj, attribute, val)) return;
        break;
    case TYPE_VEC2D:
        if (_setAttributeRef<Vec2d, pxr::GfVec2d>(sceneObj, attribute, val)) return;
        break;
    case TYPE_VEC3F:
        if (_setAttributeRef<Vec3f, pxr::GfVec3f>(sceneObj, attribute, val)) return;
        break;
    case TYPE_VEC3D:
        if (_setAttributeRef<Vec3d, pxr::GfVec3d>(sceneObj, attribute, val)) return;
        if (_setAttribute<Vec3d, pxr::GfVec3f>(sceneObj, attribute, val)) return; // Sdf forces 3d attributes to be 3f
        break;
    case TYPE_VEC4F:
        if (_setAttributeRef<Vec4f, pxr::GfVec4f>(sceneObj, attribute, val)) return;
        break;
    case TYPE_VEC4D:
        if (_setAttributeRef<Vec4d, pxr::GfVec4d>(sceneObj, attribute, val)) return;
        break;
    case TYPE_MAT3F:
        if (_setAttributeRef<Mat3f, pxr::GfMatrix3f>(sceneObj, attribute, val)) return;
        if (_setAttribute<Mat3f, pxr::GfMatrix3d>(sceneObj, attribute, val)) return;
        break;
    case TYPE_MAT3D:
        if (_setAttributeRef<Mat3d, pxr::GfMatrix3d>(sceneObj, attribute, val)) return;
        break;
    case TYPE_MAT4F:
        if (_setAttributeRef<Mat4f, pxr::GfMatrix4f>(sceneObj, attribute, val)) return;
        break;
    case TYPE_MAT4D:
        if (_setAttributeRef<Mat4d, pxr::GfMatrix4d>(sceneObj, attribute, val)) return;
        break;
    case TYPE_SCENE_OBJECT:
        // not supported yet
        break;
    case TYPE_BOOL_VECTOR:
        if (_setAttribute<BoolVector, pxr::VtArray<bool>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_INT_VECTOR:
        if (_setAttribute<IntVector, pxr::VtArray<int>>(sceneObj, attribute, val)) return;
        if (val.IsHolding<pxr::TfToken>()) {
            const std::string token = val.UncheckedGet<pxr::TfToken>().GetString();
            if (_setRampInterpolationVectorFromToken(sceneObj, attribute, token)) return;
            Logger::warn(sceneObj->getName(), '.', attribute->getName(),
                         ": unsupported scalar token value '", token,
                         "' for IntVector attribute; keeping the MoonRay default");
            return;
        }
        if (val.IsHolding<std::string>()) {
            const std::string token = val.UncheckedGet<std::string>();
            if (_setRampInterpolationVectorFromToken(sceneObj, attribute, token)) return;
            Logger::warn(sceneObj->getName(), '.', attribute->getName(),
                         ": unsupported scalar string value '", token,
                         "' for IntVector attribute; keeping the MoonRay default");
            return;
        }
        break;
    case TYPE_LONG_VECTOR:
        if (_setAttribute<LongVector, pxr::VtArray<long>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_FLOAT_VECTOR:
        if (_setAttribute<FloatVector, pxr::VtArray<float>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_DOUBLE_VECTOR:
        if (_setAttribute<DoubleVector, pxr::VtArray<double>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_STRING_VECTOR:
        if (_setAttribute<StringVector, pxr::VtArray<std::string>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_RGB_VECTOR:
        if (_setRgbVectorAttribute(sceneObj, attribute, val, colorManagement)) return;
        break;
    case TYPE_RGBA_VECTOR:
        if (_setRgbaVectorAttribute(sceneObj, attribute, val, colorManagement)) return;
        break;
    case TYPE_VEC2F_VECTOR:
        if (_setAttribute<Vec2fVector, pxr::VtArray<pxr::GfVec2f>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_VEC2D_VECTOR:
        if (_setAttribute<Vec2dVector, pxr::VtArray<pxr::GfVec2d>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_VEC3F_VECTOR:
        if (_setAttribute<Vec3fVector, pxr::VtArray<pxr::GfVec3f>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_VEC3D_VECTOR:
        if (_setAttribute<Vec3dVector, pxr::VtArray<pxr::GfVec3d>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_VEC4F_VECTOR:
        if (_setAttribute<Vec4fVector, pxr::VtArray<pxr::GfVec4f>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_VEC4D_VECTOR:
        if (_setAttribute<Vec4dVector, pxr::VtArray<pxr::GfVec4d>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_MAT3F_VECTOR:
        if (_setAttribute<std::vector<Mat3f>, pxr::VtArray<pxr::GfMatrix3f>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_MAT3D_VECTOR:
        if (_setAttribute<std::vector<Mat3d>, pxr::VtArray<pxr::GfMatrix3d>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_MAT4F_VECTOR:
        if (_setAttribute<Mat4fVector, pxr::VtArray<pxr::GfMatrix4f>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_MAT4D_VECTOR:
        if (_setAttribute<Mat4dVector, pxr::VtArray<pxr::GfMatrix4d>>(sceneObj, attribute, val)) return;
        break;
    case TYPE_SCENE_OBJECT_VECTOR:
    case TYPE_SCENE_OBJECT_INDEXABLE:
        // not supported yet
        break;
    case TYPE_UNKNOWN:
        break;
    }
    Logger::error(sceneObj->getName(), '.', attribute->getName(),
                  ": cannot convert ", val.GetTypeName(), " to ", attributeTypeName(attribute->getType()));
}

void
ValueConverter::setDefault(SceneObject* sceneObj, const Attribute* attribute)
{
    sceneObj->resetToDefault(attribute);
    _clearBinding(sceneObj,attribute);
}

void _setUnit(SceneObject* sceneObj, const Attribute* attr)
{
     switch(attr->getType()) {
     case TYPE_BOOL:   _set(sceneObj, attr, true); break;
     case TYPE_INT:    _set(sceneObj, attr, 1); break;
     case TYPE_STRING: _set(sceneObj, attr, String("")); break;
     case TYPE_LONG:   _set(sceneObj, attr, (long)1); break;
     case TYPE_FLOAT:  _set(sceneObj, attr, 1.0f); break;
     case TYPE_DOUBLE: _set(sceneObj, attr, 1.0);  break;
     case TYPE_RGB:    _set(sceneObj, attr, Rgb(1,1,1)); break;
     case TYPE_RGBA:   _set(sceneObj, attr, Rgba(1,1,1,1)); break;
     case TYPE_VEC2F:  _set(sceneObj, attr, Vec2f(1,1)); break;
     case TYPE_VEC2D:  _set(sceneObj, attr, Vec2d(1,1)); break;
     case TYPE_VEC3F:  _set(sceneObj, attr, Vec3f(1,1,1)); break;
     case TYPE_VEC3D:  _set(sceneObj, attr, Vec3d(1,1,1));break;
     case TYPE_VEC4F:  _set(sceneObj, attr, Vec4f(1,1,1,1)); break;
     case TYPE_VEC4D:  _set(sceneObj, attr, Vec4d(1,1,1,1)); break;
     case TYPE_MAT3F:  _set(sceneObj, attr, Mat3f(1,0,0,0,1,0,0,0,1)); break;
     case TYPE_MAT3D:  _set(sceneObj, attr, Mat3d(1,0,0,0,1,0,0,0,1)); break;
     case TYPE_MAT4F:  _set(sceneObj, attr, Mat4f(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)); break;
     case TYPE_MAT4D:  _set(sceneObj, attr, Mat4d(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)); break;
     default:
         Logger::error("setUnit not implemented for ", attributeTypeName(attr->getType()));
         break;
     }
}

void ValueConverter::setBinding(SceneObject* sceneObj, const Attribute* attr, SceneObject* binding)
{
    _setUnit(sceneObj,attr);
    if (attr->isBindable())
        sceneObj->setBinding(attr->getName(),binding);
}

}
