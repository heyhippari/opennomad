# https://github.com/lefticus/cppbestpractices/blob/master/02-Use_the_Tools_Available.md

option(OPENNOMAD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors for first-party OpenNomad targets" OFF)

function(set_project_warnings project_name)
  set(MSVC_WARNINGS
    /W4
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /utf-8
    /w14242
    /w14254
    /w14263
    /w14265
    /w14287
    /we4289
    /w14296
    /w14311
    /w14545
    /w14546
    /w14547
    /w14549
    /w14555
    /w14619
    /w14640
    /w14826
    /w14905
    /w14906
    /w14928)

  set(CLANG_COMMON_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wundef)

  set(CLANG_WARNINGS
    ${CLANG_COMMON_WARNINGS})

  set(GCC_WARNINGS
    ${CLANG_COMMON_WARNINGS}
    -Wmisleading-indentation
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
    -Wuseless-cast)

  if (OPENNOMAD_WARNINGS_AS_ERRORS)
    list(APPEND CLANG_WARNINGS -Werror)
    list(APPEND GCC_WARNINGS -Werror)
    list(APPEND MSVC_WARNINGS /WX)
  endif ()

  if (MSVC)
    set(PROJECT_WARNINGS ${MSVC_WARNINGS})
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    set(PROJECT_WARNINGS ${CLANG_WARNINGS})
  elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(PROJECT_WARNINGS ${GCC_WARNINGS})
  else ()
    message(AUTHOR_WARNING "No compiler warnings set for '${CMAKE_CXX_COMPILER_ID}' compiler.")
  endif ()

  target_compile_options(${project_name} INTERFACE ${PROJECT_WARNINGS})
endfunction()
