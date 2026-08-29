# AGENTS.md - guidance for AI coding agents

OpenNomad is a C++23 reimplementation of the *Omikron: The Nomad Soul* engine. It uses CMake, vcpkg manifest
mode, Ninja for native development and CI builds, SDL3, OpenGL, Dear ImGui, and doctest.

## Authoritative local quality gate

Before finishing a substantive C++ change, run the repository's checked-in quality workflow for the current host
platform. The canonical rules are in `.clang-tidy` and the toolchain contract is documented in
[docs/Toolchain.md](docs/Toolchain.md).

Linux workflow:

```sh
cmake --workflow --preset linux-debug
cmake --workflow --preset linux-release
cmake --workflow --preset linux-sanitize
cmake --build --preset quality --target check-format
cmake --build --preset quality --target tidy
```

The quality build requires the canonical LLVM 22.1.8 toolchain. If the tool is not present, the configure step must
fail with a clear error instead of silently accepting a different formatter or lint tool.

## Build and test

The checked-in presets are the supported entry points. On Linux, configure from a shell that exports
`VCPKG_ROOT` before the first configure of a build tree:

```sh
export VCPKG_ROOT="$HOME/.local/share/vcpkg"
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
```

Use the matching release and sanitizer presets for the other local workflows:

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure

cmake --preset linux-sanitize
cmake --build --preset linux-sanitize
ctest --preset linux-sanitize --output-on-failure
```

The macOS and Windows equivalents are `macos-debug`, `macos-release`, `windows-debug`, and `windows-release`.
They use the same semantic build settings as the Linux presets and must remain consistent with the repository's
quality policy.

Useful project options include `DEACTIVATE_LOGGING`, `DEBUG`, `ENABLE_DEBUG_UI`,
`OPENNOMAD_ENABLE_ASAN`, `OPENNOMAD_ENABLE_UBSAN`, and `OPENNOMAD_WARNINGS_AS_ERRORS`; options are cached per build
tree. See [docs/BuildAndExecution.md](docs/BuildAndExecution.md), [docs/Testing.md](docs/Testing.md), and
[docs/Toolchain.md](docs/Toolchain.md) for the supported commands.

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

- Use C++23 with extensions disabled and include every directly used header. The checked-in quality build enforces
  include-cleaner and warnings-as-errors for first-party code.
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
  them when changing dependencies; the overlay enables Wayland `libdecor` support. See [docs/Dependencies.md](docs/Dependencies.md)
  for system prerequisites.
- Use `App::Log` for logging. Its format strings are checked through `fmt`; keep format arguments consistent with
  the format string and use the logging levels exposed by the wrapper.

## References

Prefer the repository documentation over repeating operational detail here: [README.md](README.md),
[docs/WhereIsWhat.md](docs/WhereIsWhat.md), [docs/BuildAndExecution.md](docs/BuildAndExecution.md),
[docs/Testing.md](docs/Testing.md), [docs/Rendering.md](docs/Rendering.md), [docs/Toolchain.md](docs/Toolchain.md),
and [docs/reverse-engineering/startup-sequence.md](docs/reverse-engineering/startup-sequence.md).
