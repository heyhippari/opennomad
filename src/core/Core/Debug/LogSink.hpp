#pragma once

#include <spdlog/sinks/base_sink.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <ranges>
#include <string>
#include <vector>

namespace App::Debug {

/// A thread-safe spdlog sink that stores the last N log lines in a ring buffer
/// for display in the in-app debug UI.
///
/// @tparam Capacity  Maximum number of log lines to retain.
template <std::size_t Capacity = 512>
class RingBufferSink final : public spdlog::sinks::base_sink<std::mutex> {
 public:
  RingBufferSink() = default;

  /// One buffered log line with its severity, for display in the UI.
  struct LogEntry {
    std::string line;
    spdlog::level::level_enum level;
  };

  /// Return a snapshot of all buffered lines with their levels (oldest first).
  [[nodiscard]] std::vector<LogEntry> get_entries() const {
    const std::scoped_lock lock(m_ring_mutex);
    const std::size_t count{m_count};
    const auto entry_at{[this, count](const std::size_t i) {
      const std::size_t idx{(m_head + Capacity - count + i) % Capacity};
      return LogEntry{m_buffer.at(idx), m_levels.at(idx)};
    }};
    return std::views::iota(std::size_t{0}, count) | std::views::transform(entry_at) |
           std::ranges::to<std::vector<LogEntry>>();
  }

  /// Clear all buffered lines.
  void clear() {
    const std::scoped_lock lock(m_ring_mutex);
    m_head = 0;
    m_count = 0;
  }

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    const std::scoped_lock lock(m_ring_mutex);

    // Format the log message
    spdlog::memory_buf_t formatted;
    this->formatter_->format(msg, formatted);
    m_buffer[m_head] = std::string(formatted.data(), formatted.size());
    m_levels[m_head] = msg.level;
    m_head = (m_head + 1) % Capacity;
    if (m_count < Capacity) {
      m_count += 1;
    }
  }

  void flush_() override {
    // No-op: ring buffer is always "flushed"
  }

 private:
  mutable std::mutex m_ring_mutex;
  std::array<std::string, Capacity> m_buffer{};
  std::array<spdlog::level::level_enum, Capacity> m_levels{};
  std::size_t m_head{0};
  std::size_t m_count{0};
};

// Explicit instantiation for the capacity used by Log::get_debug_sink().
extern template class RingBufferSink<512>;

}  // namespace App::Debug
