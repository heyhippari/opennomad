# Quick start

This is the shortest supported path for a Linux development build. See [Build and execution](BuildAndExecution.md) for
other platforms and build modes.

## 1. Prepare the checkout

OpenNomad's font and image assets are stored with Git LFS:

```shell
git lfs install
git lfs pull
```

Install CMake, Ninja, a C++23 compiler, Git, Git LFS, pkg-config, and your distribution's libdecor development package.
See [Dependencies](Dependencies.md) for why libdecor is required and for the vcpkg setup.

Set `VCPKG_ROOT` to a vcpkg checkout. For example:

```shell
export VCPKG_ROOT="$HOME/.local/share/vcpkg"
```

The path must contain `scripts/buildsystems/vcpkg.cmake`.

## 2. Configure and build

```shell
cmake --preset debug
cmake --build build/debug
```

The first configure builds the manifest dependencies and can take a while. They are cached under
`build/debug/vcpkg_installed` for later builds.

Debug builds enable clang-tidy when it is installed, AddressSanitizer on non-Windows platforms, profiling, and the
in-app debug UI. For a faster build without those development checks:

```shell
cmake --preset release
cmake --build build/release
```

## 3. Point OpenNomad at the game data

OpenNomad does not include copyrighted game files. Set `OPENNOMAD_GAME_DATA_ROOT` to the directory containing the
original `Runtime.exe` and folders such as `SCPTDATA`, `IAM`, and `IMAGES`:

```shell
export OPENNOMAD_GAME_DATA_ROOT="/path/to/Omikron"
```

The alternative is to copy the original game tree next to the built executable. The environment variable is more
convenient for development and does not modify the original installation.

## 4. Run

```shell
./build/debug/src/app/App
```

To set the game-data root for one invocation only:

```shell
OPENNOMAD_GAME_DATA_ROOT="/path/to/Omikron" ./build/debug/src/app/App
```

The application currently starts in borderless fullscreen. Useful controls are:

| Input | Action |
|---|---|
| `Escape` | Skip a startup video; cancel in an interface where supported. |
| Arrow keys / `Enter` | Navigate and confirm interface entries. |
| `F11` or `Alt+Enter` | Toggle borderless fullscreen. |
| `F12` | Release the captured mouse for the debug UI; click the scene to capture it again. |
| `F3` | Toggle the performance overlay in a debug build. |

## 5. Run the tests

```shell
ctest --test-dir build/debug --output-on-failure
```

The default suite uses generated fixtures and does not require original game files. See [Testing](Testing.md) for
focused targets, integration tests, and the AddressSanitizer/LeakSanitizer caveat.

## Common setup failure

If CMake reports a toolchain path such as `/scripts/buildsystems/vcpkg.cmake`, `VCPKG_ROOT` was empty when that build
tree was configured. Set the variable, then configure a fresh build tree or explicitly correct
`CMAKE_TOOLCHAIN_FILE`. The checked-in VS Code tasks already supply the expected vcpkg location.
