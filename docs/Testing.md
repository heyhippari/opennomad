# Testing

OpenNomad uses [doctest](https://github.com/doctest/doctest). Most tests live in `src/core/Tests/`, beside the `Core`
library they exercise. `src/tests/TestRunner.cpp` supplies the shared doctest main function.

Every specification is built as a separate executable and registered with CTest. This keeps failures focused and makes
it inexpensive to build or run one area at a time.

## Run the default suite

Configure and build the Debug tree first:

```shell
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
```

The default suite uses generated fixtures and does not need copyrighted game data.

## Run one test

Build the narrow target, then select its exact CTest name:

```shell
cmake --build --preset linux-debug --target Model3DOTest
ctest --preset linux-debug -R '^Model3DOTest$' --output-on-failure
```

List registered tests with:

```shell
ctest --preset linux-debug --show-only
```

## Sanitizer and quality checks

The normal Debug configuration has no implicit sanitizers or static analysis. Use `linux-sanitize` to run the suite
with AddressSanitizer and UndefinedBehaviorSanitizer:

```shell
cmake --preset linux-sanitize
cmake --build --preset linux-sanitize
ctest --preset linux-sanitize
```

Some environments cannot run LeakSanitizer under a debugger or ptrace-based harness even after every assertion passes.
If that infrastructure issue is the only failure, this command can confirm the test result without leak detection:

```shell
ASAN_OPTIONS=detect_leaks=0 ctest --preset linux-sanitize -R '^Model3DOTest$'
```

Disabling leak detection is a diagnostic workaround, not a clean LeakSanitizer result, and should be reported as such.

The `quality` preset requires the canonical LLVM `22.1.8` toolchain. Diagnostics are errors, including include-cleaner and the
checks configured in `.clang-tidy`. It also provides `format` and non-mutating `check-format` targets.

See [Toolchain.md](Toolchain.md) for the complete host-preset and quality-tool contract.

## Test provenance

Runtime-facing tests state what kind of truth they encode in the test name or an adjacent concise comment:

- `[RUNTIME]` is established from Runtime.exe behavior or static data.
- `[RETAIL]` is a direct property of original shipped files.
- `[FORMAT]` is a recovered serialization invariant supported by the retail corpus or RE documentation.
- `[OPENNOMAD]` is an intentional safety, platform, scheduler, or presentation policy of the reimplementation.
- `[PROVISIONAL]` is a useful reconstruction whose exact Runtime semantics remain unresolved.

Current production behavior is not independent evidence for a Runtime-facing assertion. Layout source-of-truth tests
write recovered offsets as literals and separately pin production constants so producer and parser cannot drift together.
Unknown and provisional fields retain neutral names until stronger evidence exists.

## Add a test

Add `src/core/Tests/<Area>.spec.cpp`, then register a matching executable and CTest entry in
`src/core/Tests/CMakeLists.txt`:

```cmake
add_executable(AreaTest Area.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME AreaTest COMMAND AreaTest)
target_link_libraries(AreaTest PRIVATE doctest::doctest Core)
```

A minimal specification follows the existing files:

```c++
#include <doctest/doctest.h>

#include <filesystem>

#include "Core/Resources.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
//             cert-err33-c) -- doctest macros intentionally use these patterns.

TEST_SUITE("Core::Resources") {
  TEST_CASE("leaves an unresolved path unchanged") {
    const std::filesystem::path missing{"definitely-not-an-opennomad-file"};
    CHECK(App::Resources::resolve_case_insensitive(missing) == missing);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
//           cert-err33-c)
```

Keep fixtures in the test source when practical. Binary-parser tests use `OmikronTestBuffer.hpp` to construct explicit
little-endian byte fixtures. Assert on `std::expected` with `.has_value()` or `.value()` so failures remain readable.

All includes must be direct: Debug builds enforce include-cleaner in tests as well as production code. Use a targeted
`NOLINTNEXTLINE` with a reason only when a test intentionally exercises a pattern rejected by clang-tidy.

## Original-game-data tests

Tests that inspect the original files are registered only when `OPENNOMAD_GAME_DATA_TESTS` is enabled:

```shell
cmake --preset linux-debug -DOPENNOMAD_GAME_DATA_TESTS=ON
cmake --build --preset linux-debug
```

Set `OPENNOMAD_GAME_DATA_ROOT` to the original game directory and run the integration tests:

```shell
OPENNOMAD_GAME_DATA_ROOT="/path/to/Omikron" \
  ctest --preset linux-debug -R 'IntegrationTest$' --output-on-failure
```

The registered integration executables cover START, AREA, SCENE, GLOBAL, CTL, DIALOG, SCX, 3DO/3DT, sprite, font, SFX,
and QD ADP data. Enabling these tests without the expected retail files is a test failure, not a skip or false positive.
Ordinary builds leave them unregistered and never depend on copyrighted data.
