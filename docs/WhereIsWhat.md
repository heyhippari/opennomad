# Where is what?

This map describes the current repository and the ownership boundaries between its subsystems.

## Repository root

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | Project metadata, dependency discovery, testing, and top-level subdirectories. |
| `CMakePresets.json` | Linux Ninja and macOS Xcode configure/build/package workflows. |
| `vcpkg.json` | Manifest dependencies, features, baseline, and selected overrides. |
| `vcpkg-configuration.json` | Repository-local vcpkg overlays. |
| `cmake/` | Compiler warnings, strict C++23 settings, analyzers, and Apple build settings. |
| `overlay-ports/sdl3/` | SDL3 port overlay that enables libdecor on Linux. |
| `triplets/` | The Linux dynamic-library/OpenGL-core vcpkg triplet. |
| `packaging/` | CPack metadata and platform installer assets. |
| `docs/` | Developer and implementation guides. |
| `docs/reverse-engineering/` | Recovered Runtime.exe behaviour, evidence, and open questions. |

Generated files belong under `build/<config>/`; source files and original game data do not.

## Source targets

| Path | CMake target | Responsibility |
|---|---|---|
| `src/app/` | `App` | Process entry point, platform manifests, application assets, and packaging hooks. |
| `src/core/` | `Core` | Engine, loaders, runtimes, presentation, input, audio, video, and debug tooling. |
| `src/settings/` | `Settings` | Project metadata generated from `Settings/Project.cpp.in`. |
| `src/tests/` | `TestRunner` | Shared doctest entry point used by each test executable. |
| `src/assets/` | packaged assets | Manrope debug-UI font and application icons. |

## Core subsystems

Most implementation lives under `src/core/Core/`:

| Path | Responsibility |
|---|---|
| `Application.*`, `Window.*`, `Scene.hpp` | SDL lifetime, recovered main-loop ordering, window/input events, and scene presentation. |
| `Scenario/` | Three-slot scenario ownership, startup mode dispatch, world-context lifecycle, and the sole script scheduler. |
| `Script/` | Immutable script definitions, mutable SCX and AREA VM state, opcode metadata, waits, and presentation requests. |
| `Omikron/` | Bounds-checked readers for original formats including 3DO, 3DT, 3DA, 3DP, SCX, IAM, BMP, and QD ADP. |
| `Character/` | Runtime character identity, AREA presence/transforms, body resources, and CPU-side posed geometry. |
| `Sprite/` | Runtime-style sprite instances, stable handles, frame resolution, draw queues, and billboard rendering. |
| `Interface/` | Generic I2D interface registry, runtime instances, focus/residency, fonts, timelines, and rendering. |
| `Audio/` | SDL3_mixer device ownership, voices, resource cache, music, and the Runtime-style software spatializer. |
| `Video/` | FFmpeg-backed startup-video decoding and presentation. |
| `Startup/` | Ordered startup phases, trace events, and media policy. |
| `Input/` | Semantic actions, device bindings, held state, and per-frame input resolution. |
| `Debug/` | ImGui inspectors, metrics, in-app logging, startup traces, and profiling. |
| `WorldScene.*`, `WorldRenderer.*`, `WorldCamera.*` | Stable presentation of all attached resident worlds, with current-world camera/debug state, effects, interfaces, and GPU resources. |
| `SkyRenderer.*` | The session-global AREA sky 3DO, loaded from `MESHES/MISC` and drawn behind the world. |
| `WorldFog.*`, `WorldShaders.hpp` | Runtime fog/view distance, day-cycle colour, the shared per-pixel fog shader stage and the world shader variant builder. |
| `WorldShaderSet.*` | World programs shared by all attached renderers, including lazily built enhancement variants. |
| `WorldLighting.*` | Runtime context ambient, day-cycle light, baked-colour clamp and explicit decor lights for characters. |
| `ModelViewerScene.*` | Development-only free-flight renderer for inspecting standalone and embedded model resources. |
| `RuntimeMath.*`, `RuntimePresentation.*` | Runtime-native math and the single Runtime-to-OpenGL presentation boundary. |
| `Resources.hpp`, `Platform/*/Resources.cpp` | Packaged-resource and game-data path resolution. |

## Runtime ownership

OpenNomad separates recovered state from presentation responsibilities:

- `ScenarioManager` owns one gameplay-mode SCX slot and two world-context slots. Each loaded slot owns parsed data,
  backing bytes, its mutable `ScenarioRuntime`, and—for a world context—decoded decor resources.
- `ScenarioEngine` is the sole scheduler. It advances resident AREA/event execution, the gameplay-mode runtime, and all
  resident world runtimes in the recovered order.
- `ScenarioStartupController` owns the two resident AREA slots (`RuntimeAreaSlot`) and wires compact contexts to the
  session. The self-contained parts live in collaborators it owns, each behind a narrow host interface:
  `AreaTransitionService` (the seamless and rich 0x2F transactions with rollback, 0x30 release and 0x47/0x48 SCENE
  attach/detach, including their deferral while SCENE or zone scripts run), `AreaResidencyService` (the logical
  current AREA and event-9 physical handoff), `ZoneContactService` (active zones, zone-owned compact contexts,
  heartbeat and capacity), `TriggerProxyService` (the current character's spatial trigger proxy), and
  `CompactContextRegistry` (stable compact-context handles).
  Specs can inject loaders into world runtimes before the first AREA preload with
  `ScenarioManager::set_world_runtime_configurator`.
- `WorldScene` is installed after startup and remains the normal presentation scene. It observes all attached contexts,
  while camera/debug ownership follows the current context; it does not own or execute a runtime.
- Each `WorldRenderer` owns GPU resources for one attached world-context generation. Detach destroys that renderer;
  reattachment of the same generation rebuilds it, and slot reuse cannot retain stale presentation resources.
- `InterfaceManager` owns resident I2D interface instances and focus. `InterfacePresenter`, composed by `WorldScene`,
  only forwards update and render calls; an interface is not a `Scene`.
- `AudioSystem` is updated by `Application`; neither `WorldScene` nor the scenario runtime owns the device.

This is an OpenNomad architecture mapping. It is not evidence that Runtime.exe used C++ classes with these names. See
the [reverse-engineering overview](ReverseEngineering.md) for recovered behaviour.

## Tests

Core specifications live in `src/core/Tests/`, one `.spec.cpp` per focused area. Their executables and CTest entries are
declared in `src/core/Tests/CMakeLists.txt`. Integration tests that open original game data are opt-in. See
[Testing](Testing.md).

## Platform and packaging files

Platform resource lookup is implemented in `src/core/Platform/{Linux,Mac,Windows}/Resources.cpp` and selected with
`target_sources` in `src/core/CMakeLists.txt`.

Application manifests live in `src/app/Manifests/`; build/install logic lives in `src/app/cmake/`; CPack-wide metadata
and installer resources live in `packaging/`. See [Platform-dependent code](PlatformCode.md) and
[Packaging](Packaging.md).
