#include "Log.hpp"

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <vector>

#include "Core/Debug/LogSink.hpp"

namespace App {

Log::Log() {
  std::vector<spdlog::sink_ptr> log_sinks;

  const spdlog::level::level_enum level{spdlog::level::debug};

  log_sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  log_sinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("app.log", true));

  // Ring-buffer sink for the in-app debug log viewer.
  m_debug_sink = std::make_shared<DebugSink>();
  log_sinks.emplace_back(m_debug_sink);

  log_sinks.at(0)->set_pattern("%^[%T] %n(%l): %v%$");
  log_sinks.at(1)->set_pattern("[%T] [%l] %n(%l): %v");

  // The debug sink uses a flat pattern (no colour escape codes).
  m_debug_sink->set_pattern("[%T] [%l] %v");

  m_logger = std::make_shared<spdlog::logger>("APP", begin(log_sinks), end(log_sinks));
  spdlog::register_logger(m_logger);
  spdlog::set_default_logger(m_logger);
  m_logger->set_level(level);
  m_logger->flush_on(level);
}

}  // namespace App

// Explicit instantiation required by the extern template declaration in LogSink.hpp.
template class App::Debug::RingBufferSink<512>;
