# Platform-dependent code

OpenNomad keeps its platform boundary small. SDL3 handles windows, input, audio, and most operating-system integration;
the only platform-selected `Core` implementation today is resource-path lookup.

## Resource lookup

`src/core/Core/Resources.hpp` declares the common API:

- `get_user_config_path()` uses `SDL_GetPrefPath` for writable per-user configuration.
- `resource_path()` resolves packaged OpenNomad assets.
- `font_path()` resolves a font within those assets.
- `game_data_path()` resolves original Omikron data either from `OPENNOMAD_GAME_DATA_ROOT` or relative to `App`.
- `resolve_case_insensitive()` walks a path component by component when its exact spelling is absent.

Platform implementations live in:

- `src/core/Platform/Linux/Resources.cpp`
- `src/core/Platform/Mac/Resources.cpp`
- `src/core/Platform/Windows/Resources.cpp`

Linux and Windows development builds place `src/assets/` in a `share/` directory adjacent to the executable directory.
macOS resolves assets from the application bundle. Game data keeps the original game's layout on every platform.

Case-insensitive resolution is shared code in `Resources.hpp`, not a platform-specific shortcut. Every game-file loader
must use it before opening a file because the original data contains inconsistent casing.

## CMake selection

`src/core/CMakeLists.txt` selects exactly one implementation with `target_sources`:

```cmake
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_sources(Core PRIVATE Platform/Windows/Resources.cpp)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_sources(Core PRIVATE Platform/Mac/Resources.cpp)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(Core PRIVATE Platform/Linux/Resources.cpp)
endif ()
```

If a new platform needs different behaviour, add an implementation of the existing API and a new CMake branch. Keep
the public interface platform-neutral and continue to prefer SDL or standard C++ facilities when they already express
the required behaviour.

## Related platform files

Application manifests and packaging rules are separate from engine platform code:

- `src/app/Manifests/` contains the macOS, Windows, and Linux application metadata.
- `src/app/cmake/packaging/` contains platform install/bundle rules.
- `cmake/UniversalAppleBuild.cmake` configures Apple architecture defaults.
- `triplets/x64-linux-dynamic-glcore.cmake` configures Linux vcpkg linkage and glad's core profile.

See [Packaging](Packaging.md) for distribution artifacts and [Dependencies](Dependencies.md) for the Linux SDL3
overlay.
