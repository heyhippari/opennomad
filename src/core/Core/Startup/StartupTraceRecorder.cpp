#include "Core/Startup/StartupTraceRecorder.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Log.hpp"

namespace App::Startup {

void StartupTraceRecorder::record(std::string name, std::string detail) {
  const std::uint32_t sequence{static_cast<std::uint32_t>(m_events.size())};
  if (detail.empty()) {
    App::Log::info("{:03d} {}", sequence, name);
  } else {
    App::Log::info("{:03d} {} {}", sequence, name, detail);
  }
  m_events.push_back(StartupTraceEvent{
      .sequence = sequence, .name = std::move(name), .detail = std::move(detail)});
}

std::optional<std::uint32_t> StartupTraceRecorder::first_sequence_of(
    const std::string_view name, const std::size_t from) const {
  for (std::size_t index{from}; index < m_events.size(); ++index) {
    if (m_events.at(index).name == name) {
      return m_events.at(index).sequence;
    }
  }
  return std::nullopt;
}

}  // namespace App::Startup
