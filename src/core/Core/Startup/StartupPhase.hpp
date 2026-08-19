#pragma once

#include <cstdint>
#include <string_view>

namespace App::Startup {

/// One startup phase, in the Runtime.exe order. The coordinator requires
/// these to complete strictly in the order they are listed.
enum class StartupPhase : std::uint8_t {
  k_process_bootstrap,
  k_create_windows,
  k_initialize_core_systems,
  k_play_publisher_video,
  k_play_developer_video,
  k_play_intro_video,
  k_select_permanent_mode_script,
  k_prepare_splash,
  k_run_splash,
  k_reset_preliminary_scenario,
  k_initialize_new_session,
  k_run_initial_area_script,
  k_open_main_menu,
  k_complete,
};

/// Completion state of one phase.
enum class StartupPhaseStatus : std::uint8_t {
  k_pending,
  k_running,
  k_complete,
  k_skipped_by_configuration,
  k_skipped_unavailable,
  k_skipped_by_user,
  k_failed,
};

/// Diagnostic name of a phase (matches the coarse trace-event base).
[[nodiscard]] constexpr std::string_view startup_phase_name(const StartupPhase phase) {
  switch (phase) {
    case StartupPhase::k_process_bootstrap:            return "ProcessBootstrap";
    case StartupPhase::k_create_windows:               return "CreateWindows";
    case StartupPhase::k_initialize_core_systems:      return "CoreInitialization";
    case StartupPhase::k_play_publisher_video:         return "PlayPublisherVideo";
    case StartupPhase::k_play_developer_video:         return "PlayDeveloperVideo";
    case StartupPhase::k_play_intro_video:             return "PlayIntroVideo";
    case StartupPhase::k_select_permanent_mode_script: return "SelectPermanentModeScript";
    case StartupPhase::k_prepare_splash:               return "PrepareSplash";
    case StartupPhase::k_run_splash:                   return "RunSplash";
    case StartupPhase::k_reset_preliminary_scenario:   return "ResetPreliminaryScenario";
    case StartupPhase::k_initialize_new_session:       return "InitializeNewSession";
    case StartupPhase::k_run_initial_area_script:      return "RunInitialAreaScript";
    case StartupPhase::k_open_main_menu:               return "OpenMainMenu";
    case StartupPhase::k_complete:                     return "Complete";
  }
  return "Unknown";
}

/// Diagnostic name of a phase-completion status.
[[nodiscard]] constexpr std::string_view startup_status_name(const StartupPhaseStatus status) {
  switch (status) {
    case StartupPhaseStatus::k_pending:                   return "Pending";
    case StartupPhaseStatus::k_running:                   return "Running";
    case StartupPhaseStatus::k_complete:                  return "Complete";
    case StartupPhaseStatus::k_skipped_by_configuration:  return "SkippedByConfiguration";
    case StartupPhaseStatus::k_skipped_unavailable:       return "SkippedUnavailable";
    case StartupPhaseStatus::k_skipped_by_user:           return "SkippedByUser";
    case StartupPhaseStatus::k_failed:                    return "Failed";
  }
  return "Unknown";
}

}  // namespace App::Startup
