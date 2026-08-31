# Copyright 2023 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

set(component hydramoonray)

set(buildIncludeDir ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(installIncludeDir ${PACKAGE_NAME}/${component})
set(exportGroup ${PROJECT_NAME}Targets)

add_library(${component} SHARED "")
add_library(${PROJECT_NAME}::${component} ALIAS ${component})

target_sources(${component}
    PRIVATE
        basisCurves.cc
        camera.cc
        geometryBase.cc
        HdmLog.cc
        hydra2_utils.cc
        instancer.cc
        light.cc
        material.cc
        mesh.cc
        MoonrayLightFilter.cc
        MoonrayOutput.cc
        MoonrayScene.cc
        MoonraySceneIndexPlugin.cc
        MurmurHash3.cc
        NullRenderer.cc
        Options.cc
        points.cc
        primvars.cc
        procedural.cc
        renderBuffer.cc
        renderDelegate.cc
        renderPass.cc
        RenderSettings.cc
        renderSettingsPrim.cc
        renderVar.cc
        shader_utils.cc
        tokens.cc
        ValueConverter.cc
        volume.cc
)

set_property(TARGET ${component}
    PROPERTY PUBLIC_HEADER
        basisCurves.h
        camera.h
        geometryBase.h
        HdmLog.h
        hydra2_utils.h
        instancer.h
        light.h
        material.h
        mesh.h
        MoonrayLightFilter.h
        MoonrayObject.h
        MoonrayOutput.h
        MoonrayScene.h
        MoonraySceneIndexPlugin.h
        NullRenderer.h
        Options.h
        PixelData.h
        points.h
        procedural.h
        renderBuffer.h
        renderDelegate.h
        Renderer.h
        renderPass.h
        RenderSettings.h
        renderSettingsPrim.h
        renderVar.h
        shader_utils.h
        tokens.h
        Utils.h
        ValueConverter.h
        volume.h
)

target_include_directories(${component}
    PUBLIC
        $<BUILD_INTERFACE:${buildIncludeDir}>
        $<INSTALL_INTERFACE:include>
)

if (NOT IsDarwinPlatform)
    set(PlatformSpecificLibs atomic)
endif()

target_link_libraries(${component}
    PUBLIC
        SceneRdl2::common_except
        SceneRdl2::common_fb_util
        SceneRdl2::common_math
        SceneRdl2::render_logging
        SceneRdl2::scene_rdl2
        TBB::tbb
        hd hdsi  # pxr imaging
        usdImaging
        Python::Module
        ${PlatformSpecificLibs}
)

# If at Dreamworks add a SConscript stub file so others can use this library.
SConscript_Stub(${component})

# Set standard compile/link options
HdMoonray_cxx_compile_definitions(${component})
HdMoonray_cxx_compile_features(${component})
HdMoonray_cxx_compile_options(${component})
HdMoonray_link_options(${component})

# -------------------------------------
# Install the target and the export set
# -------------------------------------
include(GNUInstallDirs)

# install the plugin lib
# .so is installed under plugin, so that it can
# use the RUNPATH ../lib64

# install the target
install(TARGETS ${component}
    COMPONENT ${component}
    EXPORT ${exportGroup}
    LIBRARY
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        NAMELINK_SKIP
    RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${installIncludeDir}
)
