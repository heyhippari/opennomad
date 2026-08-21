# Where is what

Where to find what inside the project, from folder structure to configuration.

## Source code

All relevant source code is located in `src/`. The example setup is having one library, here called _"Core"_
under `src/core/`, and the application called _"App"_ under `src/app/`.

Core subsystems live under `src/core/Core/`: Omikron game-data parsers in `Core/Omikron/`, debug tooling in
`Core/Debug/`, and the action-based input system (actions, control schemes, `InputManager`) in `Core/Input/`.
`Camera` and its `CameraController` (free-fly, input-driven) sit at the top of `Core/`.
The Runtime-style sprite system lives in `Core/Sprite/`: `SpriteInstance`/`SpritePool` (stable-handle
instance pool), `SpriteFrame` (frame-descriptor resolution), `SpriteResource` (decoded embedded effects),
`SpriteRenderMode` (mode → GL state table) and `SpriteRenderer` (CPU billboard queue + GPU drawing).
Runtime character identity, AREA presence/transforms and shared CPU-side 3DO/3DT resources live in
`Core/Character/CharacterRuntime`; `WorldRenderer` owns only their per-world GPU presentation cache.

## Architecture ownership

OpenNomad maps recovered Runtime ownership onto four distinct responsibilities (this is an OpenNomad
architectural mapping, not a claim that Runtime itself had C++ classes with these names):

- **Scenario state** is owned by `ScenarioManager` / `ScenarioRuntime`: one gameplay-mode SCX slot
  (`aventure.scx`) plus two world-context SCX slots (`GRID.SCX` in context 0). Each slot owns its own
  parsed SCX, backing bytes, decor model (world contexts), runtime characters and a mutable
  `ScenarioRuntime`.
- **Simulation** is advanced by `ScenarioEngine` (the sole scheduler): the AREA/event runtime, then the
  gameplay-mode runtime, then every `LoadedActive` world runtime, each frame.
- **World presentation** is performed by `WorldScene`, the stable post-splash runtime scene. It observes
  the active world context (identity/generation) but never owns a runtime, never executes scripts and
  never updates audio.
- **I2D interfaces** are one presentation layer inside `WorldScene`, via `InterfacePresenter`. The generic
  `InterfaceManager` owns multiple resident `InterfaceInstance`s, tracks focus separately from residency,
  and queues completions; no interface is a `Scene`.

`ModelViewerScene` remains a development-only presentation tool (free-flight camera, standalone model
loading, SCX-effect testing, debug overlays). It is not the Runtime world.

## Tests

The test setup is done under `src/tests/`. Test implementations are under the respective source code unit, e.g. App
tests would be located under `src/app/Tests/`, where Core tests are located under `src/core/Tests/`.

## Static assets

Static assets like fonts and images are under `src/assets/`. This also includes all application icons
in `src/assets/icons/`.

## Manifest files

Manifest files contain operating system dependent configuration. They all are located under `src/app/Manifests/`.

- `src/app/Manifests/Info.plist` - Apple properties
  file ([ref](https://developer.apple.com/library/archive/documentation/General/Reference/InfoPlistKeyReference/Articles/AboutInformationPropertyListFiles.html#//apple_ref/doc/uid/TP40009254-SW1))
- `src/app/Manifests/App.manifest` - Windows manifest
  file ([ref](https://learn.microsoft.com/en-us/windows/win32/sbscs/application-manifests))
- `src/app/Manifests/app.rc` - Windows resource
  file ([ref](https://learn.microsoft.com/en-us/windows/win32/menurc/about-resource-files))
- `src/app/Manifests/App.desktop.in` - Linux app icon
  configuration ([ref](https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html))

## Dependencies

Dependencies are managed by [vcpkg](https://vcpkg.io) in manifest mode. The manifest `vcpkg.json` in the repository
root lists every dependency with pinned versions and features; the CMake presets route through the vcpkg toolchain,
which installs the manifest into `build/<config>/vcpkg_installed` on first configure. See
[Dependencies](Dependencies.md) for details.

## Configurations

### Project

General CMake project settings are defined under `cmake/StandardProjectSettings.cmake`, containing build types and
compiler flags.

### Compiler

Compiler warnings for all platforms are defined in `cmake/CompilerWarnings.cmake`.

### Static analyzers

Clang Tidy and Address Sanitizer setup is located in `cmake/StaticAnalyzers.cmake`. Clang-tidy is configured
through `.clang-tidy`.

### Code format

In the root of the project a `.clang-format` together with the `.editorconfig` define the code style of the project.

### Apple build

To configure how to build for Apple Silicon or Intel the `cmake/UniversalAppleBuild.cmake` defines the behavior on
release builds.

### Packaging

The main configuration to create distributable packages is in `packaging/`. Besides general files it also contains
platform dependent resources.

- `packaging/dmg/` - Apple DMG files
- `packaging/nsis/` - Windows NSIS files

Under `src/app/cmake/` are specific packaging files for the main executable.

***

Next up: [Make it your own](MakeItYourOwn.md)
