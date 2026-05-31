# Copyright 2023-2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

set(target hd_usd2rdl)

add_executable(${target})

target_sources(${target}
    PRIVATE
        FreeCamera.cc
        hd_usd2rdl.cc
        RenderOptions.cc
        RenderSettings.cc
        SceneDelegate.cc
)

# Dirty compatibility workaround:
# Houdini 20.5 ships USD 24.03 headers that fail to compile under
# Xcode 26 / AppleClang 21 because pxr/usd/sdf/childrenProxy.h contains an
# invalid _ValueProxy::operator= path that calls SdfChildrenProxy::_Set(),
# which does not exist. This target directly compiles Houdini USD/UsdImaging
# headers, so keep the workaround target-local and remove it when the
# Houdini/USD/toolchain combination compiles normally.
target_include_directories(${target}
    BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../plugin/adapters/compat/houdini20_5_usd24_xcode26
)

if (NOT IsDarwinPlatform)
    set(PlatformSpecificLibs atomic)
endif()

target_link_libraries(${target}
    PRIVATE
        SceneRdl2::scene_rdl2

        # pxr libs
        gf tf trace vt work   # base
        cameraUtil hd hdx hf  # imaging
        sdf usd usdGeom       # usd
        usdImaging

        TBB::tbb
        Python::Python
        Boost::${BOOST_PYTHON_COMPONENT_NAME}
        ${PlatformSpecificLibs}
        ${MKL}
)

# Set standard compile/link options
HdMoonray_cxx_compile_definitions(${target})
HdMoonray_cxx_compile_features(${target})
HdMoonray_cxx_compile_options(${target})
HdMoonray_link_options(${target})

install(TARGETS ${target}
    RUNTIME DESTINATION bin)
