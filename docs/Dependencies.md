# Dependencies

OpenNomad uses [vcpkg](https://vcpkg.io/) in manifest mode. The root [vcpkg.json](../vcpkg.json) declares the ports and
features, while its `builtin-baseline` and selected `overrides` make dependency resolution reproducible. Each build tree
gets its own installed dependency tree under `build/<config>/vcpkg_installed`.

## Development prerequisites

Install these outside vcpkg:

- Git and Git LFS;
- CMake 3.22 or newer;
- Ninja for the Linux and Windows command-line builds, or Xcode for the macOS presets;
- a compiler with C++23 standard-library support;
- pkg-config and the platform build tools used by vcpkg ports; and
- the system libdecor development package on Linux (`libdecor` on Arch, `libdecor-0-dev` on Debian/Ubuntu).

The first configure may require additional X11, Wayland, ALSA, or other development packages depending on the Linux
distribution. vcpkg's configure error will identify a missing system package.

## vcpkg setup

Use a vcpkg checkout whose registry contains the manifest's `builtin-baseline`. For example:

```shell
git clone https://github.com/microsoft/vcpkg.git "$HOME/.local/share/vcpkg"
export VCPKG_ROOT="$HOME/.local/share/vcpkg"
```

Verify the toolchain exists before configuring:

```shell
test -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

The Linux presets reference this path directly. If an editor starts without `VCPKG_ROOT`, CMake can cache an invalid
path such as `/scripts/buildsystems/vcpkg.cmake`; set the variable before starting the editor and use a fresh build tree
or correct the cached toolchain path.

## Manifest dependencies

| Dependency | Purpose |
|---|---|
| doctest | Unit-test framework. |
| fmt | Compile-time-checked formatting. |
| glad | OpenGL 4.1 core loader. |
| GLM | Vector and matrix math at presentation boundaries. |
| Dear ImGui | Debug and inspection UI, using docking, FreeType, SDL3, and OpenGL backends. |
| SDL3 | Windowing, input, platform services, and the OpenGL context. |
| SDL3_image | General image loading support. |
| SDL3_mixer | Audio device, decoding, and mixing support. |
| FFmpeg | Startup-video demuxing, decoding, resampling, and scaling. |
| spdlog | Categorized console, file, and in-app logging. |

## Linux triplet

Linux uses [triplets/x64-linux-dynamic-glcore.cmake](../triplets/x64-linux-dynamic-glcore.cmake). It is based on the
dynamic x64 Linux triplet so runtime libraries can be copied or packaged next to `App`. It also sets
`GLAD_PROFILE=core`; the stock glad port otherwise generates a compatibility profile while OpenNomad requests an
OpenGL 4.1 core context.

`CMakePresets.json` passes the triplet through `VCPKG_TARGET_TRIPLET` and registers the repository's overlay triplets.

## SDL3 overlay

The upstream SDL3 port disables libdecor. That leaves Wayland window decorations entirely to the compositor, so
compositors without server-side decorations can produce undecorated windows.

[overlay-ports/sdl3](../overlay-ports/sdl3) mirrors the upstream port and adds a `libdecor` feature that enables
`SDL_WAYLAND_LIBDECOR`. The feature is selected in [vcpkg.json](../vcpkg.json), and
[vcpkg-configuration.json](../vcpkg-configuration.json) registers the overlay. libdecor itself is intentionally supplied
by the host through pkg-config; do not remove the overlay merely because vcpkg does not build libdecor.

## Add or update a dependency

1. Add the port and required features to `dependencies` in `vcpkg.json`.
2. Use the existing baseline unless the change intentionally updates the registry snapshot. Add an override only when
   the project needs a specific version beyond that baseline.
3. Reconfigure the affected build tree so vcpkg installs the change.
4. Add the appropriate `find_package(... CONFIG REQUIRED)` call and link the imported target in CMake.
5. Build the narrowest affected target, then run its tests.

When updating the baseline, update and test the dependency set as a single change. Overlay ports must also be reviewed
against the new upstream port revision so local changes are not silently lost.
