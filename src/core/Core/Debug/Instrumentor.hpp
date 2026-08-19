#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"

namespace App::Debug {

using FloatingPointMicroseconds = std::chrono::duration<double, std::micro>;

struct ProfileResult {
  std::string name;
  FloatingPointMicroseconds start;
  std::chrono::microseconds elapsed_time;
  std::thread::id thread_id;
};

struct InstrumentationSession {
  const std::string name;
  explicit InstrumentationSession(std::string session_name) : name(std::move(session_name)) {}
};

class Instrumentor {
 public:
  Instrumentor(const Instrumentor&) = delete;
  Instrumentor(Instrumentor&&) = delete;
  Instrumentor& operator=(Instrumentor other) = delete;
  Instrumentor& operator=(Instrumentor&& other) = delete;

  void begin_session(const std::string& name, const std::string& filepath = "results.json") {
    const std::scoped_lock lock(m_mutex);

    if (m_current_session != nullptr) {
      // If there is already a current session, then close it before beginning new one.
      // Subsequent profiling output meant for the original session will end up in the
      // newly opened session instead.  That's better than having badly formatted
      // profiling output.
      App::Log::error(LogCategory::Debug,
          "Instrumentor::begin_session('{0}') when session '{1}' already open.",
          name,
          m_current_session->name);
      internal_end_session();
    }
    m_output_stream.open(filepath);

    if (m_output_stream.is_open()) {
      m_current_session = std::make_unique<InstrumentationSession>(name);
      write_header();
    } else {
      App::Log::error(LogCategory::Debug, "Instrumentor could not open results file '{0}'.", filepath);
    }
  }

  void end_session() {
    const std::scoped_lock lock(m_mutex);
    internal_end_session();
  }

  void write_profile(const ProfileResult& result) {
    std::string name{result.name};
    std::ranges::replace(name, '"', '\'');

    // fmt beats a stringstream here — write_profile runs once per profile
    // sample, so it is on the hot path.
    const std::string json{fmt::format(
        R"(,{{"cat":"function","dur":{},"name":"{}","ph":"X","pid":0,"tid":"{}","ts":{:.3f}}})",
        result.elapsed_time.count(),
        name,
        fmt::streamed(result.thread_id),
        result.start.count())};

    const std::scoped_lock lock(m_mutex);
    if (m_current_session != nullptr) {
      m_output_stream << json;
      m_output_stream.flush();
    }

    // Also store in the ring buffer for real-time in-app display.
    const std::size_t idx = m_recent_write_index.fetch_add(1, std::memory_order_relaxed) % kMaxRecentProfiles;
    m_recent_profiles[idx] = result;
  }

  static Instrumentor& get() {
    static Instrumentor instance;
    return instance;
  }

  /// Return a snapshot of recent profile results (for in-app profiler display).
  /// Entries may be from different frames; aggregate by name for a summary.
  [[nodiscard]] std::vector<ProfileResult> get_recent_profiles() const {
    const std::size_t count = std::min(
        m_recent_write_index.load(std::memory_order_acquire), kMaxRecentProfiles);
    const std::span<const ProfileResult> recent{m_recent_profiles.data(), count};
    return std::ranges::to<std::vector<ProfileResult>>(recent);
  }

  /// Clear the recent-profiles ring buffer (call once per frame after aggregating).
  void clear_recent_profiles() {
    m_recent_write_index.store(0, std::memory_order_release);
  }

 private:
  static constexpr std::size_t kMaxRecentProfiles{512};
  Instrumentor() : m_current_session(nullptr) {}

  ~Instrumentor() {
    end_session();
  }

  void write_header() {
    m_output_stream << R"({"otherData": {},"traceEvents":[{})";
    m_output_stream.flush();
  }

  void write_footer() {
    m_output_stream << "]}";
    m_output_stream.flush();
  }

  // Note: you must already own lock on m_Mutex before
  // calling InternalEndSession()
  void internal_end_session() {
    if (m_current_session != nullptr) {
      write_footer();
      m_output_stream.close();
    }
  }

  std::mutex m_mutex;
  std::unique_ptr<InstrumentationSession> m_current_session;
  std::ofstream m_output_stream;

  // Ring buffer for real-time in-app profiling display.
  std::array<ProfileResult, kMaxRecentProfiles> m_recent_profiles{};
  std::atomic<std::size_t> m_recent_write_index{0};
};

class InstrumentationTimer {
 public:
  explicit InstrumentationTimer(std::string name)
      : m_name(std::move(name)),
        m_start_time_point(std::chrono::steady_clock::now()) {}

  InstrumentationTimer(const InstrumentationTimer&) = delete;
  InstrumentationTimer(InstrumentationTimer&&) = delete;
  InstrumentationTimer& operator=(InstrumentationTimer other) = delete;
  InstrumentationTimer& operator=(InstrumentationTimer&& other) = delete;

  ~InstrumentationTimer() {
    if (!m_stopped) {
      stop();
    }
  }

  void stop() {
    const auto end_time_point{std::chrono::steady_clock::now()};
    const auto high_res_start{FloatingPointMicroseconds{m_start_time_point.time_since_epoch()}};
    const auto elapsed_time{
        std::chrono::time_point_cast<std::chrono::microseconds>(end_time_point).time_since_epoch() -
        std::chrono::time_point_cast<std::chrono::microseconds>(m_start_time_point)
            .time_since_epoch()};

    Instrumentor::get().write_profile(
        {m_name, high_res_start, elapsed_time, std::this_thread::get_id()});

    m_stopped = true;
  }

 private:
  const std::string m_name;
  bool m_stopped{false};
  const std::chrono::time_point<std::chrono::steady_clock> m_start_time_point;
};

/// Name of the calling function, resolved via std::source_location. Used by
/// APP_PROFILE_FUNCTION(); the default argument is evaluated at the call site,
/// so it reports the profiled function rather than this helper.
inline const char* current_function_name(
    const std::source_location location = std::source_location::current()) {
  return location.function_name();
}

}  // namespace App::Debug

#if APP_PROFILE
#define JOIN_AGAIN(x, y) x##y
#define JOIN(x, y) JOIN_AGAIN(x, y)
#define APP_PROFILE_BEGIN_SESSION(name) ::App::Debug::Instrumentor::get().begin_session(name)
#define APP_PROFILE_BEGIN_SESSION_WITH_FILE(name, file_path) \
  ::App::Debug::Instrumentor::get().begin_session(name, file_path)
#define APP_PROFILE_END_SESSION() ::App::Debug::Instrumentor::get().end_session()
#define APP_PROFILE_SCOPE(name)                                    \
  const ::App::Debug::InstrumentationTimer JOIN(timer, __LINE__) { \
    name                                                           \
  }
#define APP_PROFILE_FUNCTION() APP_PROFILE_SCOPE(::App::Debug::current_function_name())
#else
#define APP_PROFILE_BEGIN_SESSION(name)
#define APP_PROFILE_BEGIN_SESSION_WITH_FILE(name, file_path)
#define APP_PROFILE_END_SESSION()
#define APP_PROFILE_SCOPE(name)
#define APP_PROFILE_FUNCTION()
#endif
