#pragma once

#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

#include <array>
#include <memory>
#include <utility>
#include <version>

#include "Core/Debug/LogSink.hpp"
#include "Core/LogCategory.hpp"

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
  ~Log();

  /// The ring-buffer sink used for the in-app log viewer.
  using DebugSink = Debug::RingBufferSink<512>;

  /// Access the debug sink for reading log lines in the UI.
  static std::shared_ptr<DebugSink> get_debug_sink() {
    return get().m_debug_sink;
  }

  // --- Per-sink runtime verbosity control ----------------------------------

  static void set_console_level(spdlog::level::level_enum level);
  static void set_file_level(spdlog::level::level_enum level);
  static void set_debug_sink_level(spdlog::level::level_enum level);

  [[nodiscard]] static spdlog::level::level_enum console_level();
  [[nodiscard]] static spdlog::level::level_enum file_level();
  [[nodiscard]] static spdlog::level::level_enum debug_sink_level();

  // --- Logging functions ----------------------------------------------------

  /// Trace level. Compiled out in release builds and when logging is disabled.
  template <typename... Args>
  static void trace(const LogCategory category,
                    [[maybe_unused]] fmt::format_string<Args...> format,
                    [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled && k_debug_logging_enabled) {
      category_logger(category).trace(format, std::forward<Args>(args)...);
    }
  }

  /// Debug level. Compiled out in release builds and when logging is disabled.
  template <typename... Args>
  static void debug(const LogCategory category,
                    [[maybe_unused]] fmt::format_string<Args...> format,
                    [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled && k_debug_logging_enabled) {
      category_logger(category).debug(format, std::forward<Args>(args)...);
    }
  }

  /// Info level.
  template <typename... Args>
  static void info(const LogCategory category,
                   [[maybe_unused]] fmt::format_string<Args...> format,
                   [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled) {
      category_logger(category).info(format, std::forward<Args>(args)...);
    }
  }

  /// Warn level.
  template <typename... Args>
  static void warn(const LogCategory category,
                   [[maybe_unused]] fmt::format_string<Args...> format,
                   [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled) {
      category_logger(category).warn(format, std::forward<Args>(args)...);
    }
  }

  /// Error level.
  template <typename... Args>
  static void error(const LogCategory category,
                    [[maybe_unused]] fmt::format_string<Args...> format,
                    [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled) {
      category_logger(category).error(format, std::forward<Args>(args)...);
    }
  }

  /// Fatal level (mapped to spdlog's highest level, critical). Also records
  /// the current stack trace when available.
  template <typename... Args>
  static void fatal(const LogCategory category,
                    [[maybe_unused]] fmt::format_string<Args...> format,
                    [[maybe_unused]] Args&&... args) {
    if constexpr (k_logging_enabled) {
      category_logger(category).critical(format, std::forward<Args>(args)...);
#ifdef __cpp_lib_stacktrace
      category_logger(category).critical("Stack trace:\n{}", std::stacktrace::current());
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

  /// The spdlog logger for one category (a lightweight logger sharing the
  /// common sinks, named after the category so `%n` carries it).
  static spdlog::logger& category_logger(const LogCategory category) {
    return *get().m_category_loggers.at(static_cast<std::size_t>(category));
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

  std::shared_ptr<spdlog::sinks::sink> m_console_sink;
  std::shared_ptr<spdlog::sinks::sink> m_file_sink;
  std::shared_ptr<DebugSink> m_debug_sink;
  std::array<std::shared_ptr<spdlog::logger>, k_log_category_count> m_category_loggers{};
};

}  // namespace App
