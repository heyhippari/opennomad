#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace App::Startup {

/// One sequence-numbered startup trace event.
struct StartupTraceEvent {
  std::uint32_t sequence{0};
  std::string name;
  std::string detail;
};

/// Flat, ordered, sequence-numbered startup trace. Single-threaded. Sequence
/// numbers are assigned in record() order, so ordering tests can assert
/// subsequences by comparing sequence values.
class StartupTraceRecorder {
 public:
  /// Appends one event (sequence = next free number). This only stores the
  /// trace event; it does not print into the ordinary application log — the
  /// dedicated Startup Trace inspector is the primary way to read it.
  void record(std::string name, std::string detail = {});

  [[nodiscard]] const std::vector<StartupTraceEvent>& events() const {
    return m_events;
  }
  [[nodiscard]] std::size_t size() const {
    return m_events.size();
  }

  /// Sequence number of the first event whose name equals `name`, or
  /// std::nullopt when absent. The search starts at `from` (inclusive).
  [[nodiscard]] std::optional<std::uint32_t> first_sequence_of(
      std::string_view name, std::size_t from = 0) const;

 private:
  std::vector<StartupTraceEvent> m_events;
};

}  // namespace App::Startup
