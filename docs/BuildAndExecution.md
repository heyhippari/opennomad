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
| Configure/build | `xcode-debug` | macOS | Xcode Debug build in `build/xcode-debug`. |
| Configure/build | `xcode-release` | macOS | Xcode Release build in `build/xcode-release`; the build preset targets `App`. |
| Test | `all` | Linux | Run CTest against the `debug` configure preset. |
| Package | `release` | Linux | Package the Release build. |
| Package | `xcode-release` | macOS | Package the Xcode Release build. |
| Workflow | `dist`, `xcode-dist` | Linux, macOS | Configure, build, then package. |

On Linux, the configure presets read the vcpkg toolchain from
`$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`. `VCPKG_ROOT` must be set before the first configure of a build tree.

The Xcode presets do not currently embed the vcpkg toolchain path. Pass it when configuring unless your local CMake
setup already injects it:

```shell
cmake --preset xcode-debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build --preset xcode-debug
```

There is no checked-in Windows preset yet. A Ninja build can be configured manually from a shell where the compiler
and vcpkg are available:

```shell
cmake -S . -B build/debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build/debug
```

## Build modes

### Debug

Debug is the normal development configuration. It:

- defines `DEBUG` and `APP_PROFILE`;
- enables the in-app debug UI by default;
- runs clang-tidy during compilation when `clang-tidy` is installed; and
- enables AddressSanitizer on non-Windows platforms.

Clang-tidy diagnostics are treated as build failures. This includes include-cleaner and the project's other strict
checks, not only compiler warnings.

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
- `F11` or `Alt+Enter` toggles borderless fullscreen.
- `F12` releases the mouse for the debug UI; clicking outside ImGui captures it again.
- `F3` toggles the performance overlay in debug builds.

The WASD/mouse-look actions are wired for development scenes, including `ModelViewerScene`; they are not a claim that
normal gameplay movement is implemented.
