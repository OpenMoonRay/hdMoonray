# Copyright 2023-2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required (VERSION 3.23.1)

include(OMR_PackageVersion)  # Sets versionString, projectString and PACKAGE_NAME
project(${projectString}
    VERSION ${versionString}
    LANGUAGES CXX C)

string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UPPER)
list(APPEND CMAKE_MESSAGE_CONTEXT ${PROJECT_NAME})
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(OMR_Platform)

# ================================================
# Find dependencies
# ================================================

find_package(Python REQUIRED COMPONENTS Interpreter Development)

set(BUILD_SHARED_LIBS ON)

# the DWA installation of pxrConfig.cmake lives in
# the usd_imaging Rez package. To work correctly, it
# requires env vars REZ_USD_CORE_ROOT, REZ_USD_IMAGING_ROOT
# and PXR_INCLUDE_DIRS to be set (e.g. in CMake preset).
# A standard installation should not require these...
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED)
endif()

# This is needed for linking against the USD 0.25.5 libraries
# as they depend on OpenGL but don't specify it in
# their cmake config.
# See:
# * https://github.com/PixarAnimationStudios/OpenUSD/issues/3310
# * https://github.com/PixarAnimationStudios/OpenUSD/pull/3205
find_package(OpenGL REQUIRED)
if(PXR_VERSION VERSION_GREATER_EQUAL 2505)
    find_package(Vulkan REQUIRED)
endif()

find_package(JPEG REQUIRED)
find_package(OpenImageIO REQUIRED)

set(HDMOONRAY_HOUDINI_OCIO_TARGET "")
set(HDMOONRAY_HOUDINI_OCIO_INCLUDE_DIR "")
set(HDMOONRAY_HOUDINI_INSTALL_DIR "$ENV{HOUDINI_INSTALL_DIR}" CACHE PATH
    "Houdini install root used to locate SideFX OpenColorIO for Houdini-facing hdMoonray")
if (APPLE AND HDMOONRAY_HOUDINI_INSTALL_DIR STREQUAL "")
    set(HDMOONRAY_HOUDINI_INSTALL_DIR "/Applications/Houdini/Houdini20.5.584")
endif()

if (APPLE AND HDMOONRAY_HOUDINI_INSTALL_DIR)
    set(_houdiniFrameworkRoot
        "${HDMOONRAY_HOUDINI_INSTALL_DIR}/Frameworks/Houdini.framework/Versions/Current")
    set(_houdiniOcioInclude
        "${_houdiniFrameworkRoot}/Resources/toolkit/include")
    set(_houdiniOcioLibrary
        "${_houdiniFrameworkRoot}/Libraries/libOpenColorIO_sidefx.2.3.0.dylib")

    if (EXISTS "${_houdiniOcioInclude}/OpenColorIO/OpenColorIO.h" AND
        EXISTS "${_houdiniOcioLibrary}")
        add_library(HdMoonrayHoudiniOpenColorIO SHARED IMPORTED GLOBAL)
        set_target_properties(HdMoonrayHoudiniOpenColorIO PROPERTIES
            IMPORTED_LOCATION "${_houdiniOcioLibrary}"
            INTERFACE_INCLUDE_DIRECTORIES "${_houdiniOcioInclude}")
        set(HDMOONRAY_HOUDINI_OCIO_TARGET HdMoonrayHoudiniOpenColorIO)
        set(HDMOONRAY_HOUDINI_OCIO_INCLUDE_DIR "${_houdiniOcioInclude}")
        message(STATUS "hdMoonray: using Houdini SideFX OpenColorIO: ${_houdiniOcioLibrary}")
    endif()
endif()

if (NOT HDMOONRAY_HOUDINI_OCIO_TARGET)
    find_package(OpenColorIO REQUIRED)
    set(HDMOONRAY_HOUDINI_OCIO_TARGET OpenColorIO::OpenColorIO)
    message(STATUS "hdMoonray: using bundled OpenColorIO target")
endif()

# Our USD config doesn't specify TBB or Boost dependencies,
# so we need to pull the in explicitly
find_package(TBB REQUIRED)

if (NOT DEFINED BOOST_PYTHON_COMPONENT_NAME)
    # may need to be, e.g. python36, python311
    set(BOOST_PYTHON_COMPONENT_NAME python)
endif()

set(BUILD_SHARED_LIBS ON)
find_package(Boost REQUIRED COMPONENTS "${BOOST_PYTHON_COMPONENT_NAME}")

if("${PROJECT_NAME}" STREQUAL "${CMAKE_PROJECT_NAME}")
    find_package(ArrasCore REQUIRED)
    find_package(McrtDataio REQUIRED)
    find_package(Moonshine REQUIRED)
    find_package(Moonray REQUIRED)
    find_package(SceneRdl2 REQUIRED)
endif()

# Set the RPATH for binaries in the install tree
set(CMAKE_INSTALL_RPATH ${GLOBAL_INSTALL_RPATH})
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# if OPT_LEVEL is set use its value to override the CMAKE_BUILD_TYPE because the
# rez cmake plugin does not have an easy way to set the build_target.
set(opt_level $ENV{OPT_LEVEL})
if(opt_level STREQUAL opt-debug)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
elseif(opt_level STREQUAL debug)
    set(CMAKE_BUILD_TYPE Debug)
elseif(opt_level STREQUAL opt)
    set(CMAKE_BUILD_TYPE Release)
endif()

# default to Release if no build type specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Create include/${PACKAGE_NAME} link in the build directory for generated headers.
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/include)
file(CREATE_LINK ../lib ${PROJECT_BINARY_DIR}/include/${PACKAGE_NAME} SYMBOLIC)

include(${PROJECT_NAME}CompileDefinitions)
include(${PROJECT_NAME}CompileFeatures)
include(${PROJECT_NAME}CompileOptions)
include(${PROJECT_NAME}LinkOptions)
include(SConscriptStub)

# ================================================
# Add project files
# ================================================
add_subdirectory(lib)
add_subdirectory(plugin)
add_subdirectory(cmd)
add_subdirectory(sessions)
add_subdirectory(hats)

# ================================================
# Install
# ================================================
include(GNUInstallDirs)
install(EXPORT ${PROJECT_NAME}Targets
        FILE ${PROJECT_NAME}Targets.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}-${PROJECT_VERSION}
        NAMESPACE ${PROJECT_NAME}::)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    INSTALL_DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}-${PROJECT_VERSION}
)

write_basic_package_version_file(
    ${PROJECT_NAME}ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}-${PROJECT_VERSION}
)

install(
    DIRECTORY testSuite
    DESTINATION .
    USE_SOURCE_PERMISSIONS
)
