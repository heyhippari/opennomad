#include "Log.hpp"

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <vector>

#include "Core/Debug/LogSink.hpp"
#include "Core/LogCategory.hpp"

namespace App {

namespace {

/// Console pattern: millisecond timestamps, severity and category.
constexpr std::string_view k_console_pattern{"%^[%H:%M:%S.%e] [%-5l] [%-10n] %v%$"};
/// File pattern additionally carries the date, useful for bug reports.
constexpr std::string_view k_file_pattern{"[%Y-%m-%d %H:%M:%S.%e] [%-5l] [%-10n] %v"};
/// Ring-buffer pattern is flat (no colour escape codes).
constexpr std::string_view k_ring_pattern{"[%H:%M:%S.%e] [%-5l] [%-10n] %v"};

/// Default per-sink verbosity for the current build.
constexpr spdlog::level::level_enum console_level_for_build() {
  return spdlog::level::info;
}

constexpr spdlog::level::level_enum file_level_for_build() {
#if defined(DEBUG)
  return spdlog::level::debug;
#else
  return spdlog::level::info;
#endif
}

}  // namespace

Log::Log() {
  m_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  m_file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("app.log", true);
  m_debug_sink = std::make_shared<DebugSink>();

  m_console_sink->set_pattern(std::string{k_console_pattern});
  m_file_sink->set_pattern(std::string{k_file_pattern});
  m_debug_sink->set_pattern(std::string{k_ring_pattern});

  m_console_sink->set_level(console_level_for_build());
  m_file_sink->set_level(file_level_for_build());
  // The ring buffer captures everything the loggers emit (trace included) so
  // the in-app viewer can expose trace at runtime without a rebuild; its
  // default display filter starts at debug (see DebugUI).
  m_debug_sink->set_level(spdlog::level::trace);

  const std::vector<spdlog::sink_ptr> sinks{m_console_sink, m_file_sink, m_debug_sink};
  for (std::size_t index{0}; index < k_log_category_count; ++index) {
    const LogCategory category{k_all_log_categories.at(index)};
    const std::string name{log_category_name(category)};
    auto logger{std::make_shared<spdlog::logger>(name, begin(sinks), end(sinks))};
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::warn);
    spdlog::register_logger(logger);
    m_category_loggers.at(index) = std::move(logger);
  }
}

Log::~Log() {
  for (const auto& logger : m_category_loggers) {
    if (logger != nullptr) {
      logger->flush();
    }
  }
}

void Log::set_console_level(const spdlog::level::level_enum level) {
  get().m_console_sink->set_level(level);
}

void Log::set_file_level(const spdlog::level::level_enum level) {
  get().m_file_sink->set_level(level);
}

void Log::set_debug_sink_level(const spdlog::level::level_enum level) {
  get().m_debug_sink->set_level(level);
}

spdlog::level::level_enum Log::console_level() {
  return get().m_console_sink->level();
}

spdlog::level::level_enum Log::file_level() {
  return get().m_file_sink->level();
}

spdlog::level::level_enum Log::debug_sink_level() {
  return get().m_debug_sink->level();
}

}  // namespace App

// Explicit instantiation required by the extern template declaration in LogSink.hpp.
template class App::Debug::RingBufferSink<512>;
