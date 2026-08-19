#pragma once

#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <utility>
#include <version>

#include "Core/Debug/LogSink.hpp"

#ifdef __cpp_lib_stacktrace
#include <stacktrace>
#endif

namespace App {

class Log {
 public:
  Log(const Log&) = delete;
  Log(const Log&&) = delete;
  Log& operator=(const Log&) = delete;
  Log& operator=(const Log&&) = delete;
  ~Log() = default;

  static std::shared_ptr<spdlog::logger>& logger() {
    return get().m_logger;
  }

  /// The ring-buffer sink used for the in-app log viewer.
  using DebugSink = Debug::RingBufferSink<512>;

  /// Access the debug sink for reading log lines in the UI.
  static std::shared_ptr<DebugSink> get_debug_sink() {
    return get().m_debug_sink;
  }

  // --- Logging functions ----------------------------------------------------

  /// Trace level. Compiled out in release builds and when logging is disabled.
  template <typename... Args>
  static void trace([[maybe_unused]] fmt::format_string<Args...> format,
                    [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled && k_debug_logging_enabled) {
      logger()->trace(format, std::forward<Args>(args)...);
    }
  }

  /// Debug level. Compiled out in release builds and when logging is disabled.
  template <typename... Args>
  static void debug([[maybe_unused]] fmt::format_string<Args...> format,
                    [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled && k_debug_logging_enabled) {
      logger()->debug(format, std::forward<Args>(args)...);
    }
  }

  /// Info level.
  template <typename... Args>
  static void info([[maybe_unused]] fmt::format_string<Args...> format,
                   [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled) {
      logger()->info(format, std::forward<Args>(args)...);
    }
  }

  /// Warn level.
  template <typename... Args>
  static void warn([[maybe_unused]] fmt::format_string<Args...> format,
                   [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled) {
      logger()->warn(format, std::forward<Args>(args)...);
    }
  }

  /// Error level.
  template <typename... Args>
  static void error([[maybe_unused]] fmt::format_string<Args...> format,
                    [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled) {
      logger()->error(format, std::forward<Args>(args)...);
    }
  }

  /// Fatal level (mapped to spdlog's highest level, critical). Also records
  /// the current stack trace when available.
  template <typename... Args>
  static void fatal([[maybe_unused]] fmt::format_string<Args...> format,
                    [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled) {
      logger()->critical(format, std::forward<Args>(args)...);
#ifdef __cpp_lib_stacktrace
      logger()->critical("Stack trace:\n{}", std::stacktrace::current());
#endif
    }
  }

 private:
  // The constructor shall not be deleted but used to bootstrap the logger. Ignoring
  // the lint warning is ignoring doing `Log() = delete`.
  // NOLINTNEXTLINE
  Log();

  static Log& get() {
    static Log instance{};
    return instance;
  }

  // Compile-time switches mirroring the old APP_* macro gating.
#if defined(APP_DEACTIVATE_LOGGING)
  static constexpr bool k_logging_enabled{false};
#else
  static constexpr bool k_logging_enabled{true};
#endif

#if defined(DEBUG)
  static constexpr bool k_debug_logging_enabled{true};
#else
  static constexpr bool k_debug_logging_enabled{false};
#endif

  std::shared_ptr<spdlog::logger> m_logger;
  std::shared_ptr<DebugSink> m_debug_sink;
};

}  // namespace App
