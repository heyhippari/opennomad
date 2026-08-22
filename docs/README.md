# OpenNomad documentation

These guides describe the current OpenNomad implementation and its development workflow. OpenNomad is an early-stage,
open-source reimplementation of *Omikron: The Nomad Soul*; it does not distribute original game data.

## Getting started

- [Quick start](QuickStart.md) — build and run the current Linux development target.
- [Build and execution](BuildAndExecution.md) — presets, build modes, CMake options, platform-specific paths, and
  controls.
- [Dependencies](Dependencies.md) — vcpkg, system prerequisites, and dependency maintenance.
- [Testing](Testing.md) — focused tests, the full suite, sanitizers, and tests that use original game data.

## Implementation guides

- [Where is what?](WhereIsWhat.md) — repository map and subsystem ownership.
- [Rendering](Rendering.md) — runtime presentation, render layers, game-data resources, and coordinate boundaries.
- [Logging](Logging.md) — categories, sinks, levels, and usage.
- [Profiling](Profiling.md) — the built-in trace profiler and in-app profiler view.
- [Platform-dependent code](PlatformCode.md) — the small platform boundary used for resource lookup.
- [Packaging](Packaging.md) — current CPack infrastructure and maintainer notes.

## Reverse engineering

- [Runtime-to-OpenNomad overview](ReverseEngineering.md)
- [Reverse-engineering knowledge base](reverse-engineering/)

The reverse-engineering documents record recovered Runtime.exe behaviour, evidence, and unresolved questions. The
implementation guides describe OpenNomad's current architecture. When OpenNomad intentionally modernizes behaviour,
the two should be documented separately rather than making the modernization look like a recovered Runtime fact.

## Requirements

At minimum, development requires:

- a C++23 compiler;
- CMake 3.22 or newer;
- Ninja on Linux, or Xcode for the checked-in macOS presets;
- Git LFS for the checked-in fonts and images;
- a vcpkg checkout with `VCPKG_ROOT` pointing to it; and
- platform libraries required by SDL3. Linux also requires the system libdecor development package used by the SDL3
  overlay.

Run `git lfs pull` after cloning. A legal copy of *Omikron: The Nomad Soul* is required to run the application, but not
for the default unit-test suite. See [Dependencies](Dependencies.md) for setup details.
