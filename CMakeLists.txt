cmake_minimum_required(VERSION 3.25...4.4.0)

include(cmake/UniversalAppleBuild.cmake)

if (NOT DEFINED ENV{VCPKG_ROOT} OR "$ENV{VCPKG_ROOT}" STREQUAL "")
  message(FATAL_ERROR
    "VCPKG_ROOT is not set. Clone vcpkg, then set VCPKG_ROOT to its root (for example, export VCPKG_ROOT=\"$HOME/.local/share/vcpkg\").")
endif ()

if (NOT EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  message(FATAL_ERROR
    "VCPKG_ROOT does not contain scripts/buildsystems/vcpkg.cmake: $ENV{VCPKG_ROOT}")
endif ()

project(
  OpenNomad
  DESCRIPTION "An open-source reimplementation of the Omikron: The Nomad Soul game engine."
  VERSION 0.0.1
  LANGUAGES CXX)

set(PROJECT_COMPANY_NAME "OpenNomad Project")
set(PROJECT_COMPANY_NAMESPACE "com.opennomad")

include(cmake/StandardProjectSettings.cmake)
include(cmake/ProjectQuality.cmake)
include(cmake/CompilerWarnings.cmake)
include(GNUInstallDirs)

# Keep both spellings to preserve the existing target names while also providing the
# explicit project-scoped names requested by the toolchain policy.
add_library(opennomad_project_warnings INTERFACE)
add_library(project_warnings ALIAS opennomad_project_warnings)
set_project_warnings(opennomad_project_warnings)

add_library(opennomad_project_options INTERFACE)
add_library(project_options ALIAS opennomad_project_options)
target_compile_features(opennomad_project_options INTERFACE cxx_std_23)
target_compile_definitions(opennomad_project_options INTERFACE
  $<$<OR:$<BOOL:${DEBUG}>,$<CONFIG:Debug>>:DEBUG;APP_PROFILE>
  $<$<AND:$<BOOL:${ENABLE_DEBUG_UI}>,$<OR:$<BOOL:${DEBUG}>,$<CONFIG:Debug>>>:APP_DEBUG_UI>
  $<$<BOOL:${DEACTIVATE_LOGGING}>:APP_DEACTIVATE_LOGGING>)
set_property(TARGET opennomad_project_options PROPERTY CXX_EXTENSIONS OFF)

if (OPENNOMAD_ENABLE_FORMAT_TARGETS)
  opennomad_add_format_targets()
endif ()

if (OPENNOMAD_ENABLE_CLANG_TIDY)
  opennomad_add_tidy_target()
endif ()

enable_testing()

# Dependencies are provided by vcpkg (see vcpkg.json). The presets point CMake at the
# vcpkg toolchain, which installs the manifest on first configure.
find_package(doctest CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(SDL3_mixer CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_subdirectory(packaging)
add_subdirectory(src)
