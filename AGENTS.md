# AGENTS.md - guidance for AI coding agents

OpenNomad is a C++23 reimplementation of the *Omikron: The Nomad Soul* engine. It uses CMake, vcpkg manifest
mode, Ninja on Linux, SDL3, OpenGL, Dear ImGui, and doctest.

## Build and test

The checked-in presets are the supported entry points. On Linux, set `VCPKG_ROOT` before configuring:

```sh
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

Use `release` and `build/release` for a Release build. The Release build preset targets `App`; Debug builds all
registered targets. The macOS presets are `xcode-debug` and `xcode-release`; they may need an explicit
`-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"`.

Debug is the normal development configuration. When available, `clang-tidy` runs during non-Release compilation and
its diagnostics are errors. Non-Windows non-Release builds use AddressSanitizer. The authoritative checks are in
`.clang-tidy`; do not duplicate its check list here.

Build and run one test without rebuilding everything:

```sh
cmake --build build/debug --target Model3DOTest
ctest --test-dir build/debug -R '^Model3DOTest$' --output-on-failure
```

Tests needing original, uncommitted game data are disabled by default. Enable them at configure time and set the
data root when running them:

```sh
cmake --preset debug -DOPENNOMAD_GAME_DATA_TESTS=ON
OPENNOMAD_GAME_DATA_ROOT=/path/to/Omikron ctest --test-dir build/debug -R 'IntegrationTest$' --output-on-failure
```

Useful project options include `DEACTIVATE_LOGGING`, `DEBUG`, and `ENABLE_DEBUG_UI`; options are cached per build
tree. See [BuildAndExecution.md](docs/BuildAndExecution.md) and [Testing.md](docs/Testing.md) for packaging,
runtime controls, and integration-test details.

## Structure and architecture

- `src/app` builds the `App` executable. `App/Main.cpp` owns process startup and delegates engine work to `Core`.
- `src/core` builds the `Core` static library. It contains the application/window loop, rendering, audio, input,
  runtime/game state, scenes, interface, startup/video, script/scenario systems, sprites, debug UI, and Omikron
  game-data readers.
- `src/settings` builds the `Settings` static library. It owns process-lifetime settings state and generated project
  metadata; interface code should consume settings rather than own their persistent values.
- `src/tests/TestRunner.cpp` supplies the shared doctest main. `src/core/Tests` contains focused specs, each built as
  a separate executable and registered with CTest in its `CMakeLists.txt`.
- `src/assets` contains packaged fonts and icons. `cmake/` contains shared build settings and packaging support is
  under `packaging/` and `src/app/cmake/`.

All project namespaces are rooted at `App` (for example `App::Settings`, `App::Omikron`, and `App::Debug`). Keep
ownership aligned with the existing target boundaries: `App` is the executable layer, `Core` is engine/runtime code,
and `Settings` is independent settings state.

## Coding rules

- Use C++23 with extensions disabled and include every directly used header. Debug builds enforce include-cleaner.
- Use `std::expected<T, std::string>` for recoverable failures rather than exceptions. Preserve the existing error
  propagation style in the surrounding subsystem.
- Follow the configured naming rules: `m_` for private members, lower-case methods/functions/members, CamelCase types
  and namespaces, and `UPPER_CASE` global constants. Mark non-mutating query functions and factories `[[nodiscard]]`
  where the surrounding API does so. Use `///` for public API documentation.
- Prefer the ownership and factory patterns already used by the subsystem. Do not introduce global state or a new
  abstraction merely to avoid a small amount of local code.
- Add focused doctest coverage for new behavior. Use generated fixtures for normal tests; integration tests are the
  place for original game data. Assert `std::expected` results explicitly with `.has_value()` or `.value()`.
- Keep clang-tidy suppressions narrow and justified. Use `NOLINTNEXTLINE(check-name, ...)` for a single intentional
  violation; use a file-scoped `NOLINTBEGIN`/`NOLINTEND` only when a whole test or graphics file requires it.

## Game-data and platform constraints

- Resolve game-data paths with `Resources::resolve_case_insensitive()` before opening files. Original data uses
  inconsistent casing, and lookups must work on case-sensitive filesystems.
- Keep platform-specific resource behavior in `src/core/Platform/<platform>/` and select it through the existing
  CMake platform branches. Do not bypass `Resources` with platform-specific path assumptions.
- The vcpkg SDL3 overlay and custom `x64-linux-dynamic-glcore` triplet are part of the supported Linux setup. Keep
  them when changing dependencies; the overlay enables Wayland `libdecor` support. See [Dependencies.md](docs/Dependencies.md)
  for system prerequisites.
- Use `App::Log` for logging. Its format strings are checked through `fmt`; keep format arguments consistent with
  the format string and use the logging levels exposed by the wrapper.

## References

Prefer the repository documentation over repeating operational detail here: [README.md](README.md),
[docs/WhereIsWhat.md](docs/WhereIsWhat.md), [docs/BuildAndExecution.md](docs/BuildAndExecution.md),
[docs/Testing.md](docs/Testing.md), [docs/Rendering.md](docs/Rendering.md), and
[reverse-engineering/startup-sequence.md](docs/reverse-engineering/startup-sequence.md).
