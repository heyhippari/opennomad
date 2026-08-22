# Profiling

OpenNomad includes a lightweight scoped profiler in `src/core/Core/Debug/Instrumentor.hpp`. It records Chrome Trace
Event JSON and keeps a small in-memory ring for the Debug build's ImGui profiler window.

Profiling macros are enabled when `APP_PROFILE` is defined. A normal Debug build enables it automatically; a Release
configuration can opt in with `-DDEBUG=ON`.

## Application session

`src/app/App/Main.cpp` opens one session around the application lifetime:

```c++
APP_PROFILE_BEGIN_SESSION_WITH_FILE("App", "profile.json");

// Application lifetime.

APP_PROFILE_END_SESSION();
```

The output path is relative to the process working directory. A normal run therefore writes `profile.json` wherever
`App` was launched, not necessarily beside the executable.

## Instrument code

Use `APP_PROFILE_FUNCTION()` near the start of a function:

```c++
std::expected<Application, std::string> Application::create(const std::string& title) {
  APP_PROFILE_FUNCTION();
  // ...
}
```

Use `APP_PROFILE_SCOPE("name")` for a smaller region:

```c++
{
  APP_PROFILE_SCOPE("Decode 3DT material");
  // ...
}
```

The timer writes when the scope exits. In builds without `APP_PROFILE`, all profiler macros expand to nothing.

Do not add a profiler scope to every trivial accessor. Prefer boundaries that can explain startup, loading, scripting,
audio, or frame-time costs without flooding the trace with noise.

## Inspect results

Open `profile.json` in a Trace Event viewer such as [Perfetto](https://ui.perfetto.dev/). The profiler flushes each event
as it is recorded, but the JSON footer is written when the session ends; an abruptly terminated process can leave an
incomplete trace.

In a Debug build, press `F12` to release the mouse, then open **View → Profiler** from the ImGui menu bar. That view reads
the recent in-memory samples and is useful for live inspection without loading the JSON trace.

The built-in profiler is intended for targeted engine instrumentation. Use platform profilers when call stacks,
sampling, GPU timing, allocations, or system-wide scheduling are needed.
