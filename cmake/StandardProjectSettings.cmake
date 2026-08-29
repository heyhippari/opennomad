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

# Ninja stores compiler- and linker-discovered dependencies in .ninja_deps.
# Linker-generated depfiles have caused false dependency edges with current
# Ninja/linker combinations, including cycles between test executables and
# the libraries they consume. OpenNomad does not need linker-discovered
# dependencies: CMake already knows the target-level link graph.
#
# Keep normal compiler depfiles enabled so C/C++ header dependency tracking
# is unaffected; disable only the optional linker-generated dependency files.
if (CMAKE_GENERATOR MATCHES "^Ninja")
  set(CMAKE_LINK_DEPENDS_USE_LINKER FALSE)
endif ()

# The project options interface target selects C++23 for OpenNomad targets.
# Keep extensions disabled globally so CMake emits -std=c++23 for each target.
set(CMAKE_CXX_EXTENSIONS OFF)

# The project does not use C++ modules. Disable CMake's module dependency
# scanning, which otherwise injects GCC "-fmodules-ts -fdeps-format=p1689r5"
# flags that clang-tidy (via __run_co_compile) cannot parse.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(DEACTIVATE_LOGGING "Disable logging" OFF)
option(DEBUG "Enable debug statements and asserts" OFF)
option(ENABLE_DEBUG_UI "Enable the in-app ImGui debugging/performance UI" ON)
