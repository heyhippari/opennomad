# Legacy global analyzer behavior has been removed. OpenNomad now uses target-scoped
# project options and checked-in presets to enable warnings, static analysis, and
# sanitizers explicitly. This file remains as a compatibility stub so older custom
# include paths fail loudly instead of silently altering the compiler environment.

if (DEFINED OPENNOMAD_LEGACY_STATIC_ANALYZERS)
  message(FATAL_ERROR
    "The old global StaticAnalyzers.cmake behavior is no longer supported. "
    "Use the project quality targets and CMake presets instead.")
endif ()

message(STATUS "StaticAnalyzers.cmake is intentionally inactive; use the target-based OpenNomad quality policy instead.")
