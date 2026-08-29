# Houdini 20.5 hdMoonrayAdapters Compatibility Shim

This note documents a dirty compatibility workaround for building
`hdMoonrayAdapters` against Houdini 20.5.684 on macOS with Xcode 26 /
AppleClang 21.

This is not a MoonRay translation fix, a USD update, or a clean integration
solution. It is a target-local quarantine for a Houdini USD 24.03 header path
that prevents the existing MoonRay mesh-light and light-filter adapter
registration code from compiling under this toolchain.

## Scope

The shim is private to the `hdMoonrayAdapters` CMake target only:

```cmake
target_include_directories(${component}
    BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/compat/houdini20_5_usd24_xcode26
)
```

It must not be added to the global include path, exported includes, or any
other MoonRay, hdMoonray, USD, Hydra, scene_rdl2, or renderer plugin target.

`hdMoonrayAdapters` is the only target in this pass that must compile the
Houdini `usdImaging` adapter base classes used by MoonRay's mesh-light and
light-filter adapter registration path.

## Base Header

Houdini base header:

```text
/Applications/Houdini/Houdini20.5.684/Frameworks/Houdini.framework/Versions/Current/Resources/toolkit/include/pxr/usd/sdf/childrenProxy.h
```

Original Houdini header SHA-256:

```text
9932fe68aba068edf7be24ab7ad166935ab190590cadf4a4e173f6cd8bdf6fc9
```

Shim header:

```text
/Applications/MoonRay/source/openmoonray/moonray/hydra/hdMoonray/plugin/adapters/compat/houdini20_5_usd24_xcode26/pxr/usd/sdf/childrenProxy.h
```

Shim header SHA-256:

```text
a70408c2a77428b384bf593b7aafb2dd718c178e3698807cb720807cb60df08a
```

## Exact Diff From Houdini Header

```diff
--- /Applications/Houdini/Houdini20.5.684/Frameworks/Houdini.framework/Versions/Current/Resources/toolkit/include/pxr/usd/sdf/childrenProxy.h	2026-05-30 18:56:16
+++ /Applications/MoonRay/source/openmoonray/moonray/hydra/hdMoonray/plugin/adapters/compat/houdini20_5_usd24_xcode26/pxr/usd/sdf/childrenProxy.h	2026-05-31 00:58:58
@@ -25,6 +25,23 @@
 #define PXR_USD_SDF_CHILDREN_PROXY_H

 /// \file sdf/childrenProxy.h
+
+// Dirty compatibility workaround:
+// Houdini 20.5 ships USD 24.03 headers that fail to compile
+// through usdImaging lightAdapter/lightFilterAdapter under
+// Xcode 26 / AppleClang 21 because pxr/usd/sdf/childrenProxy.h
+// contains an invalid _ValueProxy::operator= path that calls
+// SdfChildrenProxy::_Set(), which does not exist.
+//
+// This shim is target-local to hdMoonrayAdapters only.
+// It does not represent a MoonRay integration fix, a USD update,
+// or a translation layer. It only quarantines a compiler/header
+// compatibility problem so the existing MoonRay mesh-light and
+// light-filter adapter registration path can still be built and
+// runtime-tested.
+//
+// Remove this workaround when building against a Houdini/USD/toolchain
+// combination where the usdImaging adapter headers compile normally.

 #include "pxr/pxr.h"
 #include "pxr/usd/sdf/api.h"
@@ -72,7 +89,8 @@
         template <class U>
         _ValueProxy& operator=(const U& x)
         {
-            _owner->_Set(*_pos, x);
+            _owner->erase(_owner->_view.key(*_pos));
+            _owner->insert(mapped_type(x));
             return *this;
         }
```

The adapter sources do not use `SdfChildrenProxy` directly. The failing path is
reached while parsing Houdini's `usdImaging` adapter headers.

## Toolchain

Compiler path:

```text
/usr/bin/clang++
```

AppleClang version:

```text
Apple clang version 21.0.0 (clang-2100.1.1.101)
```

Actual Xcode toolchain compiler:

```text
/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++
Apple clang version 21.0.0 (clang-2100.1.1.101)
```

SDK path:

```text
/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
```

SDK version:

```text
26.5
```

`DEVELOPER_DIR` was unset for these probes, so `xcrun` selected the default
Xcode installation.

Homebrew LLVM was checked and was unavailable:

```text
brew --prefix llvm
/opt/homebrew/opt/llvm

/opt/homebrew/opt/llvm/bin/clang++ --version
zsh: no such file or directory: /opt/homebrew/opt/llvm/bin/clang++
```

## Baseline Probe Without Shim

The probes were modeled after Houdini/HDK compile flags, not generic CMake
flags. They used Houdini toolkit includes, Houdini Python 3.11 includes,
C++17, the active macOS SDK, and the HDK-style preprocessor defines.

### lightAdapter.h

Command:

```zsh
SDK=$(xcrun --show-sdk-path)
HFS=/Applications/Houdini/Houdini20.5.684/Frameworks/Houdini.framework/Versions/Current/Resources
printf '#include <pxr/usdImaging/usdImaging/lightAdapter.h>\nint main(){return 0;}\n' | clang++ -fsyntax-only -x c++ - -std=c++17 -mmacosx-version-min=10.15 -fPIC -faligned-new -isysroot "$SDK" -I"$HFS/toolkit/include" -I"$HFS/toolkit/include/python3.11" -DVERSION=\"20.5.684\" -DARM64 -DSIZEOF_VOID_P=8 -DFBX_ENABLED=1 -DOPENCL_ENABLED=1 -DOPENVDB_ENABLED=1 -D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS=1 -DSESI_LITTLE_ENDIAN -DHBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT=1 -DHBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DUT_ASSERT_LEVEL=0 -DH_PYTHON_VERSION=3.11 -DGCC3 -DGCC4 -D_GNU_SOURCE -DENABLE_THREADS -DUSE_PTHREADS -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DMBSD -DMBSD_COCOA -DMBSD_ARM -DMAKING_DSO -D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION -DNEED_SPECIALIZATION_STORAGE
```

Result:

```text
FAIL: pxr/usd/sdf/childrenProxy.h:75:21: error: no member named '_Set' in 'SdfChildrenProxy<_View>'
```

### lightFilterAdapter.h

Command:

```zsh
SDK=$(xcrun --show-sdk-path)
HFS=/Applications/Houdini/Houdini20.5.684/Frameworks/Houdini.framework/Versions/Current/Resources
printf '#include <pxr/usdImaging/usdImaging/lightFilterAdapter.h>\nint main(){return 0;}\n' | clang++ -fsyntax-only -x c++ - -std=c++17 -mmacosx-version-min=10.15 -fPIC -faligned-new -isysroot "$SDK" -I"$HFS/toolkit/include" -I"$HFS/toolkit/include/python3.11" -DVERSION=\"20.5.684\" -DARM64 -DSIZEOF_VOID_P=8 -DFBX_ENABLED=1 -DOPENCL_ENABLED=1 -DOPENVDB_ENABLED=1 -D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS=1 -DSESI_LITTLE_ENDIAN -DHBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT=1 -DHBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DUT_ASSERT_LEVEL=0 -DH_PYTHON_VERSION=3.11 -DGCC3 -DGCC4 -D_GNU_SOURCE -DENABLE_THREADS -DUSE_PTHREADS -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DMBSD -DMBSD_COCOA -DMBSD_ARM -DMAKING_DSO -D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION -DNEED_SPECIALIZATION_STORAGE
```

Result:

```text
FAIL: pxr/usd/sdf/childrenProxy.h:75:21: error: no member named '_Set' in 'SdfChildrenProxy<_View>'
```

## Adapter-Target-Style Probe With Shim

These probes add only the target-local shim include directory before Houdini's
toolkit include path.

### lightAdapter.h

Command:

```zsh
SDK=$(xcrun --show-sdk-path)
HFS=/Applications/Houdini/Houdini20.5.684/Frameworks/Houdini.framework/Versions/Current/Resources
SHIM=/Applications/MoonRay/source/openmoonray/moonray/hydra/hdMoonray/plugin/adapters/compat/houdini20_5_usd24_xcode26
printf '#include <pxr/usdImaging/usdImaging/lightAdapter.h>\nint main(){return 0;}\n' | clang++ -fsyntax-only -x c++ - -std=c++17 -mmacosx-version-min=10.15 -fPIC -faligned-new -isysroot "$SDK" -I"$SHIM" -I"$HFS/toolkit/include" -I"$HFS/toolkit/include/python3.11" -DVERSION=\"20.5.684\" -DARM64 -DSIZEOF_VOID_P=8 -DFBX_ENABLED=1 -DOPENCL_ENABLED=1 -DOPENVDB_ENABLED=1 -D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS=1 -DSESI_LITTLE_ENDIAN -DHBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT=1 -DHBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DUT_ASSERT_LEVEL=0 -DH_PYTHON_VERSION=3.11 -DGCC3 -DGCC4 -D_GNU_SOURCE -DENABLE_THREADS -DUSE_PTHREADS -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DMBSD -DMBSD_COCOA -DMBSD_ARM -DMAKING_DSO -D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION -DNEED_SPECIALIZATION_STORAGE
```

Result:

```text
PASS with one TBB deprecation warning.
```

### lightFilterAdapter.h

Command:

```zsh
SDK=$(xcrun --show-sdk-path)
HFS=/Applications/Houdini/Houdini20.5.684/Frameworks/Houdini.framework/Versions/Current/Resources
SHIM=/Applications/MoonRay/source/openmoonray/moonray/hydra/hdMoonray/plugin/adapters/compat/houdini20_5_usd24_xcode26
printf '#include <pxr/usdImaging/usdImaging/lightFilterAdapter.h>\nint main(){return 0;}\n' | clang++ -fsyntax-only -x c++ - -std=c++17 -mmacosx-version-min=10.15 -fPIC -faligned-new -isysroot "$SDK" -I"$SHIM" -I"$HFS/toolkit/include" -I"$HFS/toolkit/include/python3.11" -DVERSION=\"20.5.684\" -DARM64 -DSIZEOF_VOID_P=8 -DFBX_ENABLED=1 -DOPENCL_ENABLED=1 -DOPENVDB_ENABLED=1 -D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS=1 -DSESI_LITTLE_ENDIAN -DHBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT=1 -DHBOOST_BIND_GLOBAL_PLACEHOLDERS=1 -DUT_ASSERT_LEVEL=0 -DH_PYTHON_VERSION=3.11 -DGCC3 -DGCC4 -D_GNU_SOURCE -DENABLE_THREADS -DUSE_PTHREADS -D_REENTRANT -D_FILE_OFFSET_BITS=64 -DMBSD -DMBSD_COCOA -DMBSD_ARM -DMAKING_DSO -D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION -DNEED_SPECIALIZATION_STORAGE
```

Result:

```text
PASS with one TBB deprecation warning.
```

## Required Validation

The shim only proves that the Houdini `usdImaging` adapter headers can be
parsed for the `hdMoonrayAdapters` target under the current compiler. It does
not prove adapter runtime behavior.

Before treating `hdMoonrayAdapters` behavior as working, validate:

1. Configure and build:

   ```zsh
   cd /Applications/MoonRay/openmoonray
   cmake --preset macos-houdini-release
   cmake --build /Applications/MoonRay/build --target hdMoonrayAdapters --config Release --verbose
   ```

2. Install/discovery:

   ```zsh
   find /Applications/MoonRay/installs/openmoonray -iname "*hdMoonrayAdapters*" -o -path "*/plugin/pxr/hdMoonrayAdapters/plugInfo.json"
   source /Applications/MoonRay/openmoonray/scripts/macOS/setupHoudini.sh
   echo "$PXR_PLUGINPATH_NAME"
   ```

3. Runtime mesh-light fixture:

   ```text
   USD relationship -> UsdImaging adapter -> Hydra geometryLight sprim
   -> hdMoonray Light::syncParams -> native MoonRay MeshLight geometry
   ```

4. Runtime light-filter fixture:

   ```text
   USD moonray:projector / moonray:light_filters relationships
   -> UsdImaging adapter -> Hydra lightFilter sprim
   -> hdMoonray LightFilter::syncProjector/syncCombineFilters
   -> native MoonRay LightFilterSet
   ```

Remove the shim when a Houdini/USD/toolchain combination is available where
`pxr/usdImaging/usdImaging/lightAdapter.h` and
`pxr/usdImaging/usdImaging/lightFilterAdapter.h` compile without it.
