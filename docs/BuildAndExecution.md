# Build and execution

OpenNomad uses CMake, C++23, and vcpkg manifest mode. The checked-in presets are the preferred entry point, but their
availability is platform-conditional.

## Presets

Use CMake's listing commands to see the presets available on the current host:

```shell
cmake --list-presets
cmake --build --list-presets
ctest --list-presets
cpack --list-presets
cmake --workflow --list-presets
```

The repository currently defines:

| Kind | Preset | Host | Purpose |
|---|---|---|---|
| Configure/build | `debug` | Linux | Ninja Debug build in `build/debug`. |
| Configure/build | `release` | Linux | Ninja Release build in `build/release`; the build preset targets `App`. |
| Configure/build | `linux-sanitize` | Linux | Debug build in `build/linux-sanitize` with AddressSanitizer and UndefinedBehaviorSanitizer. |
| Configure/build | `quality` | Linux | Debug build with the pinned clang-tidy and clang-format tools. |
| Configure/build | `xcode-debug` | macOS | Xcode Debug build in `build/xcode-debug`. |
| Configure/build | `xcode-release` | macOS | Xcode Release build in `build/xcode-release`; the build preset targets `App`. |
| Test | `all` | Linux | Run CTest against the `debug` configure preset. |
| Package | `release` | Linux | Package the Release build. |
| Package | `xcode-release` | macOS | Package the Xcode Release build. |
| Workflow | `dist`, `xcode-dist` | Linux, macOS | Configure, build, then package. |

On Linux, the configure presets read the vcpkg toolchain from
`$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`. `VCPKG_ROOT` must be set before the first configure of a build tree.

The Xcode presets also read the vcpkg toolchain from `$VCPKG_ROOT`:

```shell
cmake --preset xcode-debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build --preset xcode-debug
```

CI owns platform-specific Release presets for Linux, macOS, and Windows. They use the same toolchain settings as the
developer presets and are intentionally named `ci-<platform>-release`.

## Build modes

### Debug

Debug is the normal development configuration. It:

- defines `DEBUG` and `APP_PROFILE`;
- enables the in-app debug UI by default;
- has no implicit static analysis or sanitizer instrumentation.

Use the explicit validation configurations when needed:

```shell
cmake --preset linux-sanitize
cmake --build --preset linux-sanitize
ctest --preset linux-sanitize

cmake --preset quality
cmake --build --preset quality
cmake --build --preset quality --target check-format
```

`quality` requires clang-format and clang-tidy version `22.1.8`. This is checked at configure time; set
`OPENNOMAD_CLANG_TOOLS_MAJOR` only for an intentional local toolchain-policy change. `format` modifies C++ files;
`check-format` is the non-mutating validation target. Clang-tidy diagnostics are build failures.

### Release

Release omits the debug UI, trace/debug logging, profiling macros, clang-tidy, and AddressSanitizer unless a developer
explicitly opts back into debug definitions. The Release build preset only builds the `App` target; build a specific
test target manually if a release-mode test is needed.

`RelWithDebInfo` is accepted by the CMake project but has no checked-in preset.

## Project options

| Option | Default | Effect |
|---|---:|---|
| `DEACTIVATE_LOGGING` | `OFF` | Compiles out all `App::Log` calls. |
| `DEBUG` | `OFF` | Enables debug definitions and profiling outside a Debug build. |
| `ENABLE_DEBUG_UI` | `ON` | Enables the ImGui development UI when debug definitions are active. |
| `OPENNOMAD_GAME_DATA_TESTS` | `OFF` | Registers tests that inspect original game data. |
| `OPENNOMAD_ENABLE_ASAN` | `OFF` | Enables AddressSanitizer on project targets. Prefer `linux-sanitize`. |
| `OPENNOMAD_ENABLE_UBSAN` | `OFF` | Enables UndefinedBehaviorSanitizer on project targets. Prefer `linux-sanitize`. |
| `OPENNOMAD_ENABLE_CLANG_TIDY` | `OFF` | Runs the pinned clang-tidy on project targets. Prefer `quality`. |
| `OPENNOMAD_ENABLE_FORMAT_TARGETS` | `OFF` | Creates `format` and `check-format`. Prefer `quality`. |

Pass options during configuration, for example:

```shell
cmake --preset debug -DENABLE_DEBUG_UI=OFF
cmake --preset debug -DOPENNOMAD_GAME_DATA_TESTS=ON
cmake --preset release -DDEBUG=ON
```

Options are cached per build tree. Re-run CMake after changing one.

## Build selected targets

Build everything registered in a Debug tree:

```shell
cmake --build build/debug
```

Build only the application or one test executable:

```shell
cmake --build build/debug --target App
cmake --build build/debug --target Model3DOTest
```

## Executable locations

| Platform/build | Executable |
|---|---|
| Linux Ninja | `build/<config>/src/app/App` |
| Windows Ninja | `build/<config>/src/app/App.exe` |
| macOS Xcode Debug | `build/xcode-debug/src/app/Debug/App.app/Contents/MacOS/App` |
| macOS Xcode Release | `build/xcode-release/src/app/Release/App.app/Contents/MacOS/App` |

Set `OPENNOMAD_GAME_DATA_ROOT` to the original game directory before running. Game-data lookup is case-insensitive on
all platforms. See [Quick start](QuickStart.md#3-point-opennomad-at-the-game-data) for an example.

## Runtime controls

- `Escape` skips startup videos and acts as interface cancel where implemented.
- Arrow keys and `Enter` navigate the current interface.
- `Alt+Enter` toggles between Windowed and the preferred fullscreen mode.
- `F12` releases the mouse for the debug UI; clicking outside ImGui captures it again.
- `F3` toggles the performance overlay in debug builds.

The WASD/mouse-look actions are wired for development scenes, including `ModelViewerScene`; they are not a claim that
normal gameplay movement is implemented.
