include_guard(GLOBAL)

set(OPENNOMAD_CLANG_TOOLCHAIN_VERSION "22.1.8" CACHE STRING "Canonical LLVM/Clang toolchain version")
set(OPENNOMAD_CLANG_TOOLS_MAJOR "22" CACHE STRING "Canonical LLVM/Clang major version for quality tooling")

option(OPENNOMAD_ENABLE_CLANG_TIDY "Run clang-tidy for OpenNomad targets" OFF)
option(OPENNOMAD_ENABLE_ASAN "Enable AddressSanitizer for OpenNomad targets" OFF)
option(OPENNOMAD_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for OpenNomad targets" OFF)
option(OPENNOMAD_ENABLE_FORMAT_TARGETS "Create clang-format validation targets" OFF)
option(OPENNOMAD_WARNINGS_AS_ERRORS "Treat project warnings as errors in strict builds" OFF)

function(opennomad_find_clang_tool tool output_variable)
  unset(tool_path)
  unset(tool_path CACHE)
  if (DEFINED ENV{OPENNOMAD_LLVM_ROOT} AND NOT "$ENV{OPENNOMAD_LLVM_ROOT}" STREQUAL "")
    list(APPEND _candidate_roots "$ENV{OPENNOMAD_LLVM_ROOT}/bin")
  endif ()
  list(APPEND _candidate_roots "$ENV{PATH}")

  find_program(tool_path
    NAMES "${tool}-${OPENNOMAD_CLANG_TOOLS_MAJOR}" "${tool}"
    HINTS ${_candidate_roots}
    PATH_SUFFIXES bin
    NO_CACHE)

  if (NOT tool_path)
    message(FATAL_ERROR
      "${tool} ${OPENNOMAD_CLANG_TOOLCHAIN_VERSION} is required for the canonical OpenNomad quality gate. "
      "No matching executable was found. Set OPENNOMAD_LLVM_ROOT=/path/to/llvm or add the correct tool to PATH.")
  endif ()

  execute_process(
    COMMAND "${tool_path}" --version
    OUTPUT_VARIABLE tool_version
    OUTPUT_STRIP_TRAILING_WHITESPACE)

  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" version_match "${tool_version}")
  if (NOT version_match)
    message(FATAL_ERROR
      "OpenNomad expected ${tool} ${OPENNOMAD_CLANG_TOOLCHAIN_VERSION} but found ${tool_path} with output: ${tool_version}. "
      "Set OPENNOMAD_LLVM_ROOT to a LLVM 22.1.8 installation or update PATH.")
  endif ()

  if (NOT version_match STREQUAL OPENNOMAD_CLANG_TOOLCHAIN_VERSION)
    message(FATAL_ERROR
      "OpenNomad requires ${tool} ${OPENNOMAD_CLANG_TOOLCHAIN_VERSION}, but found ${tool_path} version ${version_match}. "
      "Set OPENNOMAD_LLVM_ROOT or PATH to the canonical LLVM 22.1.8 toolchain.")
  endif ()

  set(${output_variable} "${tool_path}" PARENT_SCOPE)
endfunction()

function(opennomad_enable_quality target_name)
  if (OPENNOMAD_ENABLE_CLANG_TIDY)
    opennomad_find_clang_tool(clang-tidy clang_tidy)
    set_property(TARGET ${target_name} PROPERTY
      CXX_CLANG_TIDY "${clang_tidy};--extra-arg=-Wno-unknown-warning-option")
  endif ()

  if (OPENNOMAD_ENABLE_ASAN OR OPENNOMAD_ENABLE_UBSAN)
    if (MSVC)
      message(FATAL_ERROR "Address and UndefinedBehavior sanitizers are enabled only for GCC/Clang toolchains.")
    endif ()

    set(_sanitize_flags)
    if (OPENNOMAD_ENABLE_ASAN)
      list(APPEND _sanitize_flags -fsanitize=address)
    endif ()
    if (OPENNOMAD_ENABLE_UBSAN)
      list(APPEND _sanitize_flags -fsanitize=undefined)
    endif ()

    if (_sanitize_flags)
      list(APPEND _sanitize_flags -fno-omit-frame-pointer)
      target_compile_options(${target_name} PUBLIC ${_sanitize_flags})
      target_link_options(${target_name} PUBLIC ${_sanitize_flags})
    endif ()
  endif ()
endfunction()

function(opennomad_add_format_targets)
  opennomad_find_clang_tool(clang-format clang_format)
  file(GLOB_RECURSE format_sources CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/*.inl")

  add_custom_target(format
    COMMAND "${clang_format}" -i ${format_sources}
    USES_TERMINAL
    COMMENT "Formatting OpenNomad C++ sources")

  add_custom_target(check-format
    COMMAND "${clang_format}" --dry-run --Werror ${format_sources}
    USES_TERMINAL
    COMMENT "Checking OpenNomad C++ source formatting")
endfunction()

function(opennomad_add_tidy_target)
  if (NOT OPENNOMAD_ENABLE_CLANG_TIDY)
    return()
  endif ()

  opennomad_find_clang_tool(clang-tidy clang_tidy)
  find_program(run_clang_tidy
    NAMES "run-clang-tidy-${OPENNOMAD_CLANG_TOOLS_MAJOR}" run-clang-tidy
    HINTS "${OPENNOMAD_LLVM_ROOT}/bin" "$ENV{OPENNOMAD_LLVM_ROOT}/bin"
    REQUIRED)

  add_custom_target(tidy
    COMMAND "${run_clang_tidy}"
      -clang-tidy-binary "${clang_tidy}"
      -p "${CMAKE_BINARY_DIR}"
      -source-filter "^${PROJECT_SOURCE_DIR}/src/"
      -extra-arg=-Wno-unknown-warning-option
      -quiet
    USES_TERMINAL
    COMMENT "Running clang-tidy on OpenNomad sources")
endfunction()

