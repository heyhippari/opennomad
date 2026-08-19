# Dependencies

Dependencies are managed with [vcpkg](https://vcpkg.io) in manifest mode. The manifest
[vcpkg.json](../vcpkg.json) at the repository root lists every dependency, its version
(via `overrides` + `builtin-baseline`), and the features to build. The CMake presets
point at the vcpkg toolchain, so the first configure of a build tree automatically
installs everything into `build/<config>/vcpkg_installed`.

## Prerequisites

1. Install the `vcpkg` package (Arch: `sudo pacman -S vcpkg`).
2. Clone the registry once (the package only ships the binary):

```bash
git clone https://github.com/microsoft/vcpkg.git ~/.local/share/vcpkg
```

`/etc/profile.d/vcpkg.sh` already exports `VCPKG_ROOT=~/.local/share/vcpkg` for login
shells. The CMake presets read `$env{VCPKG_ROOT}`, so restart your shell/editor after
the first install so the variable is set.

## Already included

The following set of dependencies are already included:

- [Doctest](https://github.com/doctest/doctest) - Testing framework
- [fmtlib](https://fmt.dev/latest/index.html) - Formatting library
- [glad](https://glad.dav1d.de) - OpenGL loader (4.1 core profile, see below)
- [Dear ImGui](https://github.com/ocornut/imgui) - Immediate mode GUI library (docking branch, SDL3 + OpenGL3 backends)
- [SDL3](https://www.libsdl.org) - Media layer library for rendering and input abstraction
- [SDL3_image](https://github.com/libsdl-org/SDL_image) - Image loading for SDL3
- [spdlog](https://github.com/gabime/spdlog) - Logging library

## Triplets

Linux builds use the custom overlay triplet
[triplets/x64-linux-dynamic-glcore.cmake](../triplets/x64-linux-dynamic-glcore.cmake).
It is the community `x64-linux-dynamic` triplet (shared libraries, so SDL3 can be
bundled by the packaging scripts) plus `GLAD_PROFILE=core`, because the stock glad
port generates the compatibility profile while `Window.cpp` requests a 4.1 core
context. The presets pass it via `VCPKG_TARGET_TRIPLET` and `VCPKG_OVERLAY_TRIPLETS`.

## Port overlays

The upstream vcpkg `sdl3` port hardcodes `SDL_WAYLAND_LIBDECOR=OFF`, which leaves SDL3
relying on server-side decorations (`xdg-decoration`) on Wayland. Compositors without
SSD support — GNOME and Weston — then show windowed windows with no decorations at all.
[`overlay-ports/sdl3`](../overlay-ports/sdl3) mirrors the upstream port and adds an
opt-in `libdecor` feature (`SDL_WAYLAND_LIBDECOR=ON`); it is enabled in the root
[`vcpkg.json`](../vcpkg.json) and registered via
[`vcpkg-configuration.json`](../vcpkg-configuration.json). Like SDL's wayland/xkbcommon
handling, libdecor itself comes from the system (Arch: `libdecor`, Debian/Ubuntu:
`libdecor-0-dev`), found via pkg-config at configure time.

## Add or update a dependency

1. Add the port (and any features) to the `dependencies` list in `vcpkg.json`, and pin
   it in `overrides`.
2. Reconfigure the build tree (`cmake --preset debug`). vcpkg installs only what
   changed.
3. Add the `find_package(<port> CONFIG REQUIRED)` call to the root `CMakeLists.txt`.
4. Link the imported target, e.g. `target_link_libraries(Core PUBLIC imgui::imgui)`.
   Run `vcpkg search <port>` or check the port's `usage` file under
   `$VCPKG_ROOT/ports/<port>/` for the exact target names.

To move the whole dependency set to newer versions, update the `builtin-baseline` in
`vcpkg.json` to a newer vcpkg registry commit and adjust the `overrides` accordingly.

***

Next up: [Packaging](Packaging.md)
