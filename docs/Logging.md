# Logging

`App::Log` wraps spdlog with compile-time-checked fmt format strings, a small set of subsystem categories, and three
shared sinks. Its implementation lives in `src/core/Core/Log.{hpp,cpp}`; category definitions live in
`src/core/Core/LogCategory.hpp`.

## Sinks

Every category logger writes to:

- a colour console sink, `info` and above by default;
- `app.log` in the process working directory, `debug` and above in Debug builds and `info` and above otherwise; and
- a 512-entry ring buffer used by the Debug build's ImGui log viewer.

The log viewer can filter by severity, category, and text. Release builds do not compile the debug UI.

## Levels and categories

The available functions, from least to most severe, are:

- `Log::trace`
- `Log::debug`
- `Log::info`
- `Log::warn`
- `Log::error`
- `Log::fatal`, mapped to spdlog `critical` and followed by a stack trace when the standard library provides one

Every call takes a `LogCategory` before the format string. Current categories cover Core, Startup, Video, Input,
Resource, Scenario, SCX, Script, Audio, Music, Interface, I2D, Renderer, and Debug. Add a category only for a subsystem a
developer would reasonably filter independently; do not create per-class categories.

Trace and debug calls are compiled out unless `DEBUG` is defined. A normal Debug configuration defines it; a Release
build can opt in with `-DDEBUG=ON`. `DEACTIVATE_LOGGING=ON` compiles out every level.

## Usage

Include both the logger and category declaration directly:

```c++
#include <cstdint>

#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"

namespace App {

void report_world_ready(const std::uint32_t scene_id) {
  Log::info(LogCategory::Scenario, "world {} is ready", scene_id);
}

}  // namespace App
```

Format strings use fmt syntax and are checked at compile time. Prefer structured values in the format string over
prebuilding a message. Do not log original game-file payloads or other large binary data.

## Runtime sink levels

The console, file, and debug-ring thresholds can be changed independently through:

- `Log::set_console_level(...)`
- `Log::set_file_level(...)`
- `Log::set_debug_sink_level(...)`

Their matching getters expose the current thresholds. Changing a sink threshold affects every category logger because
the category loggers share the sinks.
