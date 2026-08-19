# AGENTS.md — guidance for AI coding agents

OpenNomad: an open-source reimplementation of the **Omikron: The Nomad Soul** game engine.
C++23 app and engine: SDL3 + OpenGL 4.1 + Dear ImGui, built with CMake + vcpkg (manifest mode) + Ninja.

## Build, run, test

- Configure/build: `cmake --preset debug && cmake --build build/debug` (or `release`).
- **`VCPKG_ROOT` must be set** (e.g. `$HOME/.local/share/vcpkg`) — the preset toolchain path depends on it.
  The predefined VS Code workspace tasks set it; the CMake Tools extension's own configure may not,
  which fails with a bogus `/scripts/buildsystems/vcpkg.cmake` cached path.
- First configure builds all transitive vcpkg dependencies (one-time cost, cached in `build/<config>/vcpkg_installed`).
- Tests: `ctest --test-dir build/debug` (or `build/release`).
- Debug (non-Release) builds run clang-tidy and ASan. The ASan app exits with a LeakSanitizer report —
  leaks are in SDL3/ALSA/DBus/PipeWire, pre-existing and harmless. Don't chase them.

## Layout

- `src/core` — static lib `Core`: GL/rendering classes, `Application`/`Scene` framework,
  `Core/Omikron/` game-data parsers and `Core/Sprite/` (Runtime-style sprite instances,
  frame resolution, billboard queue and sprite renderer).
- `src/app` — `App` executable (`App/Main.cpp`).
- `src/settings` — `Settings` lib (`Project.cpp.in` is configured into `Project.cpp`).
- `src/core/Tests` — doctest specs, one per class.
- Details: [docs/WhereIsWhat.md](docs/WhereIsWhat.md).

## Conventions

- All code lives in namespace `App`; `App::Settings`, `App::Debug`, `App::Omikron` are nested, not top-level.
- Error handling: **`std::expected<T, std::string>`** for recoverable errors — no exceptions.
  Parsers use error-state readers (`Omikron::BinaryReader::has_error()`).
- C++23 with `CMAKE_CXX_EXTENSIONS OFF`. `#pragma once`; direct includes only
  (`misc-include-cleaner` is enforced — every symbol used in a `.cpp` needs its include there).
- Naming: `m_` members, `k_` constants; `[[nodiscard]]` on getters/statics; Doxygen `///` on public API.
- Factories: `create()` returns by value (movable types) or `expected<unique_ptr<T>, string>`;
  private ctors + `new T(...)` inside factories need `NOLINT(cppcoreguidelines-owning-memory)`.
- libstdc++ makes dynamic→fixed `std::span` conversion **explicit** — construct fixed spans at call sites
  (`std::span<const GLfloat, 4>{array}`).

## Clang-tidy is strict (warnings-as-errors in non-Release builds; keep both builds clean)

- Checks that bite most: `misc-include-cleaner`, `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access`
  (use `.at()`; `std::span`/`std::mdspan` have no `.at()` → NOLINT),
  `readability-math-missing-parentheses`, `performance-no-int-to-ptr`,
  `modernize-use-designated-initializers`, `modernize-use-ranges`.
- NOLINT style: per-line `// NOLINTNEXTLINE(check-name, ...)` with a brief reason;
  file-scoped `NOLINTBEGIN(...)`/`NOLINTEND(...)` in GL-heavy and test files.

## Omikron game-data loading

- **Every** file loader must route through `Resources::resolve_case_insensitive(path)`
  (`src/core/Core/Resources.hpp`) before opening a file — game data on disk has inconsistent
  casing and lookups must be case-insensitive on case-sensitive filesystems.
- Loader template: `ModelScene.cpp`'s `read_file` (resolve → `SDL_LoadFile` → `expected<vector<byte>, string>`).
- Parser pattern: `Core/Omikron/Model3DO.{hpp,cpp}` and `Texture3DT.{hpp,cpp}`.
  New parsers (ANIMS, MAP2D) follow the same style: header-only `BinaryReader`, static `load()`,
  spec in `src/core/Tests` building binary fixtures with `OmikronTestBuffer.hpp`.

## Tests

- doctest. Specs live next to the code: `src/core/Tests/<Area>.spec.cpp`.
- Each spec is its own executable registered in `src/core/Tests/CMakeLists.txt`
  via `add_executable(XTest ...)` + `add_test(NAME XTest COMMAND XTest)`.
- Assert on `std::expected` results via `.has_value()` / `.value()`.

## Build-system landmines (do not "fix")

- `cmake/StandardProjectSettings.cmake` sets `CMAKE_CXX_SCAN_FOR_MODULES OFF` — required with CMake 4.4,
  otherwise clang-tidy rejects the injected module-scan flags. Removing it breaks the build.
- `overlay-ports/sdl3` mirrors the vcpkg sdl3 port (registered via `vcpkg-configuration.json`)
  with an opt-in `libdecor` feature (`SDL_WAYLAND_LIBDECOR=ON`, enabled in the root `vcpkg.json`).
  Upstream hardcodes libdecor OFF, which leaves windowed mode undecorated on Wayland compositors
  without server-side decorations (GNOME/Weston). SDL finds the **system** libdecor via pkg-config
  (like it already does for wayland/xkbcommon), so `libdecor` (Arch) / `libdecor-0-dev`
  (Debian/Ubuntu) must be installed. Don't "simplify" the overlay away.
- `.clang-tidy` YAML regexes must use single-quoted scalars (`'...\.h'`); double quotes make
  `\.` an invalid escape and clang-tidy silently disables all checks.
- The vcpkg imgui port installs backend headers flat: include `<imgui_impl_sdl3.h>` /
  `<imgui_impl_opengl3.h>` directly (no `backends/` prefix).
- Font/icon assets are Git LFS tracked; a missing `git lfs pull` surfaces as a runtime font crash.
- Logging via `App::Log` (`Core/Log.hpp`) uses `fmt::format_string`, consteval-checked —
  invalid format strings are compile errors. spdlog has no `fatal` level (mapped to `critical`).

## Further reading

Link rather than duplicate: [docs/README.md](docs/README.md) (usage guide), [docs/QuickStart.md](docs/QuickStart.md),
[docs/BuildAndExecution.md](docs/BuildAndExecution.md), [docs/Testing.md](docs/Testing.md),
[docs/Rendering.md](docs/Rendering.md), [docs/Logging.md](docs/Logging.md),
[docs/PlatformCode.md](docs/PlatformCode.md), [docs/Dependencies.md](docs/Dependencies.md).
