# Toolchain

OpenNomad uses CMake presets as the supported build interface. C++23 is required
with compiler extensions disabled. vcpkg runs in manifest mode through
`VCPKG_ROOT`, and Ninja is the generator for the native presets.

## Host presets

Linux development uses `linux-debug`, `linux-release`, and `linux-sanitize`.
The matching macOS presets are `macos-debug` and `macos-release`; Windows uses
`windows-debug` and `windows-release`. Configure, build, and test with the same
preset name, or use its workflow preset for the complete sequence.

```shell
export VCPKG_ROOT="$HOME/.local/share/vcpkg"
cmake --workflow --preset linux-debug
cmake --workflow --preset linux-release
cmake --workflow --preset linux-sanitize
```

The checked-in presets enable warnings as errors for first-party code and keep
the platform triplets and dependency overlays consistent with CI.

## Canonical quality tools

The `quality` preset requires exactly LLVM `22.1.8` for both clang-format and
clang-tidy. Configuration fails when that version is unavailable; it never
silently accepts a different major or patch version. Set `OPENNOMAD_LLVM_ROOT`
to the LLVM installation root or place the matching tools on `PATH`.

```shell
export OPENNOMAD_LLVM_ROOT="/path/to/llvm-22.1.8"
cmake --preset quality
cmake --build --preset quality --target check-format
cmake --build --preset quality --target tidy
```

`.clang-format` and `.clang-tidy` are authoritative. The quality target uses
the generated compilation database, checks direct includes through
include-cleaner, and treats project diagnostics as errors. Use narrow,
documented suppressions only for intentional exceptions.

## Sanitizers

`linux-sanitize` enables AddressSanitizer and UndefinedBehaviorSanitizer with
frame pointers preserved. Sanitizer failures are quality failures. Disabling
LeakSanitizer may be used only to diagnose debugger or ptrace infrastructure
limitations and must not be reported as a clean sanitizer result.