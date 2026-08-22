# Testing

OpenNomad uses [doctest](https://github.com/doctest/doctest). Most tests live in `src/core/Tests/`, beside the `Core`
library they exercise. `src/tests/TestRunner.cpp` supplies the shared doctest main function.

Every specification is built as a separate executable and registered with CTest. This keeps failures focused and makes
it inexpensive to build or run one area at a time.

## Run the default suite

Configure and build the Debug tree first:

```shell
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

The default suite uses generated fixtures and does not need copyrighted game data.

## Run one test

Build the narrow target, then select its exact CTest name:

```shell
cmake --build build/debug --target Model3DOTest
ctest --test-dir build/debug -R '^Model3DOTest$' --output-on-failure
```

List registered tests with:

```shell
ctest --test-dir build/debug --show-only
```

## Debug-build checks

A non-Release build runs clang-tidy during compilation when the tool is installed. Its diagnostics are errors, including
include-cleaner, unchecked container access, and the other checks configured in `.clang-tidy`.

On non-Windows platforms, non-Release targets are also built with AddressSanitizer. Some environments cannot run
LeakSanitizer under a debugger or ptrace-based harness even after every assertion passes. If that infrastructure issue
is the only failure, this command can confirm the test result without leak detection:

```shell
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/debug -R '^Model3DOTest$' --output-on-failure
```

Disabling leak detection is a diagnostic workaround, not a clean LeakSanitizer result, and should be reported as such.

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
cmake --preset debug -DOPENNOMAD_GAME_DATA_TESTS=ON
cmake --build build/debug
```

Set `OPENNOMAD_GAME_DATA_ROOT` to the original game directory and run the integration tests:

```shell
OPENNOMAD_GAME_DATA_ROOT="/path/to/Omikron" \
  ctest --test-dir build/debug -R 'IntegrationTest$' --output-on-failure
```

The registered integration executables currently cover SCX structure, 3DO/3DT resources, sprite resources, and QD ADP
audio. Individual cases emit a warning and return without checking data that is unavailable, so always confirm the
environment variable and expected files when interpreting a passing integration run.
