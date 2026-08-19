cmake_minimum_required(VERSION 3.22...4.4.0)

include(cmake/UniversalAppleBuild.cmake)

project(
  OpenNomad
  DESCRIPTION "An open-source reimplementation of the Omikron: The Nomad Soul game engine."
  VERSION 0.0.1
  LANGUAGES CXX)

set(PROJECT_COMPANY_NAME "OpenNomad Project")
set(PROJECT_COMPANY_NAMESPACE "com.opennomad")

include(cmake/StandardProjectSettings.cmake)
include(GNUInstallDirs)

# Link this "library" to use the warnings specified in CompilerWarnings.cmake.
add_library(project_warnings INTERFACE)
include(cmake/CompilerWarnings.cmake)
set_project_warnings(project_warnings)

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
