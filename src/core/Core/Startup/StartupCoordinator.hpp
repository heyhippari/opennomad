#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"

namespace App::Startup {

/// Enforces that startup phases complete strictly in the documented order.
/// Pure logic (no SDL/GL): the application performs each phase's work and
/// reports it through begin()/complete(); out-of-order reports are rejected.
/// Every phase produces structured, sequence-numbered trace events.
class StartupCoordinator {
 public:
  /// Number of phases in the mandatory sequence (excluding the terminal
  /// Startup.Complete marker produced by finish()).
  static constexpr std::size_t k_phase_count{13};
  using PhaseList = std::array<StartupPhase, k_phase_count>;

  /// The phases in their mandatory order.
  [[nodiscard]] static constexpr PhaseList ordered_phases() {
    return {
        StartupPhase::k_process_bootstrap,
        StartupPhase::k_create_windows,
        StartupPhase::k_initialize_core_systems,
        StartupPhase::k_play_publisher_video,
        StartupPhase::k_play_developer_video,
        StartupPhase::k_play_intro_video,
        StartupPhase::k_select_permanent_mode_script,
        StartupPhase::k_prepare_splash,
        StartupPhase::k_run_splash,
        StartupPhase::k_reset_preliminary_scenario,
        StartupPhase::k_initialize_new_session,
        StartupPhase::k_run_initial_area_script,
        StartupPhase::k_open_main_menu,
    };
  }

  explicit StartupCoordinator(StartupTraceRecorder& recorder);

  StartupCoordinator(const StartupCoordinator&) = delete;
  StartupCoordinator& operator=(const StartupCoordinator&) = delete;

  /// Marks `phase` as started. Rejects a phase that is not the expected next
  /// phase. Records "<Phase>.Begin".
  std::expected<void, std::string> begin(StartupPhase phase);

  /// Reports completion of `phase` with `status`. Rejects a mismatched or
  /// out-of-order phase. Records "<Phase>.<Status>". A k_failed status stops
  /// the sequence; any other status advances to the next phase.
  std::expected<void, std::string> complete(
      StartupPhase phase, StartupPhaseStatus status, std::string detail = {});

  /// Records the terminal Startup.Complete marker. Valid only after every
  /// phase has completed.
  std::expected<void, std::string> finish();

  /// The phase currently running, or the last completed phase.
  [[nodiscard]] StartupPhase current_phase() const {
    return m_current_phase;
  }
  /// True once the final phase completed or a phase failed.
  [[nodiscard]] bool finished() const {
    return m_finished;
  }

 private:
  /// Index of `phase` in the ordered list, or std::nullopt.
  [[nodiscard]] static std::optional<std::size_t> phase_index(StartupPhase phase);

  StartupTraceRecorder& m_recorder;
  std::size_t m_cursor{0};
  StartupPhase m_current_phase{StartupPhase::k_process_bootstrap};
  bool m_running{false};
  bool m_finished{false};
};

}  // namespace App::Startup
