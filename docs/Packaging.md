# Packaging

OpenNomad has CPack infrastructure for developer package artifacts. It has not yet established a supported release or
installer matrix, so a package must be tested on a clean machine before it is published.

General CPack configuration is in `packaging/CMakeLists.txt`. Application installation and platform-specific bundle
rules are under `src/app/cmake/Packaging.cmake` and `src/app/cmake/packaging/`.

## Generators

The configured generators are:

| Host | Generators |
|---|---|
| Linux | `.tar.gz` and Debian `.deb` |
| macOS | `.tar.gz` and DragNDrop `.dmg` |
| Windows | `.zip` and NSIS `.exe` |
| Other | `.tar.gz` |

Packages are built natively; the project does not define a cross-packaging workflow.

## Build a package

On Linux, use the checked-in workflow preset:

```shell
cmake --workflow --preset dist
```

Or run its stages independently:

```shell
cmake --preset release
cmake --build --preset release
cpack --preset release
```

On macOS:

```shell
cmake --preset xcode-release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build --preset xcode-release
cpack --preset xcode-release
```

The package preset reports the generated filenames. CPack's configured output directory is `distribution/` relative to
the package build invocation.

There is no checked-in Windows preset. Configure and build Release manually, then run CPack against that tree:

```shell
cpack --config build/release/CPackConfig.cmake -C Release
```

NSIS must be installed to produce the Windows installer.

## What is installed

- `App` is installed under the platform's runtime/bundle destination.
- `src/assets/` supplies the debug-UI font and application icons.
- Linux installs the vcpkg shared-library directory next to `App`, sets an `$ORIGIN` runtime path, and installs a desktop
  entry plus icon.
- Windows copies SDL3 beside the development executable and package executable.
- macOS copies SDL3 into the application bundle's `Frameworks` directory and configures `Info.plist`.

The Windows and macOS rules currently name only SDL3 explicitly even though OpenNomad has additional dynamic
dependencies. Validate dependency closure on a clean target system before treating either artifact as redistributable.
Original Omikron game data must never be added to a package.

## Maintainer checklist

Before publishing an artifact:

1. Replace unfinished metadata in `packaging/CMakeLists.txt`, including the Debian maintainer value.
2. Review `packaging/{Welcome,Description,Readme,License}.txt` and the platform installer artwork for OpenNomad-specific
   text and branding.
3. Confirm the project version and company metadata in the root `CMakeLists.txt`.
4. Build from a clean Release tree with Git LFS assets present.
5. Inspect the archive to ensure it contains no original game data.
6. Install or extract it on a clean machine and verify library loading, assets, video, audio, and startup.

## Application icons

Source icons are under `src/assets/icons/`. `src/app/cmake/AppAssets.cmake` connects the macOS `.icns` and Windows `.ico`
files to their bundles; Linux installs `BaseAppIcon.png` and references it from `App.desktop.in`.

Regenerate the macOS icon on macOS from the checked-in iconset:

```shell
cd src/assets/icons
iconutil -c icns icon.iconset
```

Regenerate the Windows icon with ImageMagick:

```shell
cd src/assets/icons
magick windows/icon_16x16.png windows/icon_32x32.png \
  windows/icon_64x64.png windows/icon_128x128.png \
  windows/icon_256x256.png windows/icon_512x512.png icon.ico
```

Keep generated icon assets in Git LFS and verify all three manifests/bundle rules after changing their names.
