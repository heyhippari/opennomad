# Set a default build type if none was specified
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Debug' as none was specified.")
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)

  # Set the possible values of build type for cmake-gui, ccmake
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo")
endif ()

find_program(CCACHE ccache)
if (CCACHE)
  message(STATUS "Using ccache")
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
else ()
  message(STATUS "Ccache not found")
endif ()

# Generate compile_commands.json to make it easier to work with clang based tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compile as strict C++23. CMAKE_CXX_EXTENSIONS OFF forces CMake to emit
# -std=c++23 into compile_commands.json: the compiler default (gnu++23)
# would otherwise satisfy the request without a flag, and clang-tidy would
# then parse the sources as its own default (C++17) and reject newer
# library features. An explicit flag keeps tidy and the compiler in sync.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The project does not use C++ modules. Disable CMake's module dependency
# scanning, which otherwise injects GCC "-fmodules-ts -fdeps-format=p1689r5"
# flags that clang-tidy (via __run_co_compile) cannot parse.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(DEACTIVATE_LOGGING "Disable logging" OFF)
if (DEACTIVATE_LOGGING)
  add_compile_definitions(APP_DEACTIVATE_LOGGING)
endif ()

option(DEBUG "Enable debug statements and asserts" OFF)
if (DEBUG OR CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_definitions(DEBUG APP_PROFILE)
endif ()

option(ENABLE_DEBUG_UI "Enable the in-app ImGui debugging/performance UI" ON)
if (ENABLE_DEBUG_UI AND (DEBUG OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
  add_compile_definitions(APP_DEBUG_UI)
endif ()
