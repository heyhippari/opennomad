#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioStartupController.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"

namespace App::Audio {
class AudioSystem;
}

namespace App {

/// The recovered scenario-engine modes (Runtime's ScenarioEngine(mode,
/// argument) dispatcher).
enum class ScenarioMode : std::uint8_t {
  k_initial = 0,
  k_tick = 1,
  k_new_session = 2,
  k_teardown = 3,
};

/// Explicit scenario-mode dispatcher. Modes run in the recovered order:
/// mode 0 (idle init) during core initialization, mode 3 (preliminary
/// teardown) after the splash, mode 2 (new-session initialization), then
/// mode 1 (per-frame scenario tick) which executes the area script and
/// opens interface 29.
class ScenarioEngine {
 public:
  ScenarioEngine(ScenarioManager& manager, Startup::StartupTraceRecorder& trace);

  ScenarioEngine(const ScenarioEngine&) = delete;
  ScenarioEngine(ScenarioEngine&&) = delete;
  ScenarioEngine& operator=(const ScenarioEngine&) = delete;
  ScenarioEngine& operator=(ScenarioEngine&&) = delete;

  /// Dispatches one scenario mode. `argument` is preserved for parity with
  /// Runtime even though no current mode consumes it.
  [[nodiscard]] std::expected<void, std::string> enter_mode(
      ScenarioMode mode, std::int32_t argument);

  /// Selects aventure.SCX into the permanent mode slot (pre-splash phase).
  [[nodiscard]] std::expected<void, std::string> select_permanent_mode_script();

  /// Wires the application audio system through to the startup controller's
  /// music sink (opcode 0x67).
  void set_audio_system(Audio::AudioSystem* audio);

  /// Per-frame scenario scheduler update: continues the active AREA script
  /// across frames without re-entering a scenario mode or re-recording the
  /// mode begin/complete trace events. No-op until a new session is
  /// initialized.
  [[nodiscard]] std::expected<void, std::string> update();

  /// Delivers a deferred interface completion to the dispatcher (updating the
  /// main-menu lifecycle) and resumes the waiting AREA script.
  void notify_interface_completion(const InterfaceCompletion& completion);

  [[nodiscard]] InterfaceDispatcher& dispatcher() {
    return m_startup.dispatcher();
  }
  [[nodiscard]] const InterfaceDispatcher& dispatcher() const {
    return m_startup.dispatcher();
  }

  [[nodiscard]] ScenarioManager& manager() {
    return m_manager;
  }
  [[nodiscard]] const ScenarioManager& manager() const {
    return m_manager;
  }

  [[nodiscard]] std::int16_t initial_area_id() const {
    return m_startup.initial_area_id();
  }
  [[nodiscard]] std::int16_t linked_area_id() const {
    return m_startup.linked_area_id();
  }
  [[nodiscard]] std::optional<std::int32_t> area_mapping(std::int32_t area_id) const {
    return m_startup.area_mapping(area_id);
  }
  [[nodiscard]] const std::unordered_map<std::int32_t, std::int32_t>& area_mapping_entries() const {
    return m_startup.area_mapping_entries();
  }
  [[nodiscard]] const Omikron::IamAreaRecord* area_record() const {
    return m_startup.area_record();
  }
  [[nodiscard]] const Script::AreaScriptRuntime* area_script() const {
    return m_startup.area_script();
  }
  [[nodiscard]] bool ticked() const {
    return m_startup.ticked();
  }
  [[nodiscard]] const std::string& grid_scx_path() const {
    return m_startup.grid_scx_path();
  }
  [[nodiscard]] const std::string& grid_3do_path() const {
    return m_startup.grid_3do_path();
  }
  [[nodiscard]] const std::string& grid_3do_state() const {
    return m_startup.grid_3do_state();
  }
  [[nodiscard]] const Omikron::Model3DOData* grid_3do_model() const {
    return m_startup.grid_3do_model();
  }
  [[nodiscard]] const std::string& last_error() const {
    return m_startup.last_error();
  }

 private:
  /// Selects the permanent mode script and records `event_name`.
  [[nodiscard]] std::expected<void, std::string> select_permanent_mode_script_impl(
      std::string event_name);

  ScenarioManager& m_manager;
  ScenarioStartupController m_startup;
  Startup::StartupTraceRecorder& m_trace;
};

}  // namespace App
