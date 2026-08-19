# Logging

The library [spdlog](https://github.com/gabime/spdlog) is used for logging. The logger is set up
in `src/core/Core/Log.{cpp,hpp}` that will define a default logger writing to stdout and into a `app.log` file. The
logging functions are defined in `src/core/Core/Log.hpp`.

## Available functions

The available functions are defined in order of severity as static members of `App::Log`.

- `App::Log::trace(...)`
- `App::Log::debug(...)`
- `App::Log::info(...)`
- `App::Log::warn(...)`
- `App::Log::error(...)`
- `App::Log::fatal(...)` (maps to spdlog `critical` and appends the current stack trace)

The levels `trace` and `debug` are only enabled in debug mode or when `DEBUG` is defined through CMake;
their call sites are compiled out otherwise. Format strings are checked at compile time via
`fmt::format_string`.

```shell
cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DDEBUG -B build/release
```

Logging can also fully be **deactivated** via `DEACTIVATE_LOGGING`.

```shell
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug -DDEACTIVATE_LOGGING -B build/debug
```

## Usage

Include the logger and call one of the functions. All logging functions use fmt under the hood for string formatting.

```c++
#include "Core/Log.hpp"

namespace App {

Window::Window(const Settings& settings) {
  Log::debug("Window created: {}", settings.title);
}

}
```

***

Next up: [Dependencies](Dependencies.md)
