# This file needs to be included before calling `project`.
if (APPLE)
  # Preserve the supported minimum deployment target whether the generator is Xcode or
  # Ninja; the CMake project is expected to run on both without a generator-only fix.
  set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "Minimum OS X deployment version" FORCE)

  if ("${CMAKE_GENERATOR}" STREQUAL "Xcode")
    # Generate universal executable for Apple hardware when using Xcode.
    set(CMAKE_OSX_ARCHITECTURES "$(ARCHS_STANDARD)" CACHE STRING "MacOS archs for Xcode builds" FORCE)
  endif ()
endif ()
