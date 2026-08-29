#include "Core/Startup/StartupCoordinator.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <utility>

#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"

namespace App::Startup {

namespace {

/// Builds a coordinator failure, logging it so the application can drive the
/// fixed sequence without checking every begin/complete result.
std::expected<void, std::string> failure(std::string message) {
  App::Log::error(LogCategory::Startup, "StartupCoordinator: {}", message);
  return std::expected<void, std::string>{std::unexpect, std::move(message)};
}

}  // namespace

StartupCoordinator::StartupCoordinator(StartupTraceRecorder& recorder) : m_recorder(recorder) {}

std::optional<std::size_t> StartupCoordinator::phase_index(const StartupPhase phase) {
  const PhaseList phases{ordered_phases()};
  for (std::size_t index{0}; index < phases.size(); ++index) {
    if (phases.at(index) == phase) {
      return index;
    }
  }
  return std::nullopt;
}

std::expected<void, std::string> StartupCoordinator::begin(const StartupPhase phase) {
  if (m_finished) {
    return failure("startup already finished");
  }
  if (m_running) {
    return failure("a phase is already running");
  }
  if (m_cursor >= k_phase_count) {
    return failure("all phases complete; call finish()");
  }

  const StartupPhase expected{ordered_phases().at(m_cursor)};
  if (phase != expected) {
    return failure(fmt::format("phase {} is out of order (expected {})",
        startup_phase_name(phase),
        startup_phase_name(expected)));
  }

  m_recorder.record(fmt::format("{}.Begin", startup_phase_name(phase)));
  m_current_phase = phase;
  m_running = true;
  return {};
}

std::expected<void, std::string> StartupCoordinator::complete(
    const StartupPhase phase, const StartupPhaseStatus status, std::string detail) {
  if (m_finished) {
    return failure("startup already finished");
  }
  if (!m_running) {
    return failure("no phase is running");
  }
  if (phase != m_current_phase) {
    return failure(fmt::format("completing {} but {} is running",
        startup_phase_name(phase),
        startup_phase_name(m_current_phase)));
  }

  m_recorder.record(fmt::format("{}.{}", startup_phase_name(phase), startup_status_name(status)),
      std::move(detail));
  m_running = false;

  if (status == StartupPhaseStatus::k_failed) {
    m_finished = true;
  } else {
    ++m_cursor;
  }
  return {};
}

std::expected<void, std::string> StartupCoordinator::finish() {
  if (m_finished) {
    return failure("startup already finished");
  }
  if (m_running) {
    return failure("a phase is still running");
  }
  if (m_cursor != k_phase_count) {
    return failure("not all phases have completed");
  }

  m_recorder.record("Startup.Complete");
  m_current_phase = StartupPhase::k_complete;
  m_finished = true;
  return {};
}

}  // namespace App::Startup
