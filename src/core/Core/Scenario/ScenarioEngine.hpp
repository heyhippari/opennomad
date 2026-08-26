#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

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

  /// Per-frame scenario scheduler update: advances the active AREA script
  /// and then ticks the gameplay-mode runtime and every LoadedActive world
  /// runtime with the real application delta in seconds. No-op until a new
  /// session is initialized.
  [[nodiscard]] std::expected<void, std::string> update(float delta_seconds);

  /// Delivers a deferred interface completion to the dispatcher (updating the
  /// main-menu lifecycle) and resumes the waiting AREA script.
  void notify_interface_completion(const InterfaceCompletion& completion);

  [[nodiscard]] InterfaceDispatcher& dispatcher() {
    return m_startup.dispatcher();
  }
  [[nodiscard]] const InterfaceDispatcher& dispatcher() const {
    return m_startup.dispatcher();
  }

  /// Opens the preliminary interface 29 phase shown while the splash runs.
  void open_preliminary_29() {
    m_startup.open_preliminary_29();
  }
  /// Closes the preliminary interface 29 phase (idempotent).
  void close_preliminary_29() {
    m_startup.close_preliminary_29();
  }
  /// True while the preliminary splash interface 29 phase is open.
  [[nodiscard]] bool preliminary_29_active() const {
    return m_startup.preliminary_29_active();
  }
  /// True once the AREA script has opened interface 29 (the main menu).
  [[nodiscard]] bool main_menu_active() const {
    return m_startup.main_menu_active();
  }
  /// The handle of the active main-menu instance, or nullopt when none.
  [[nodiscard]] std::optional<InterfaceHandle> active_handle() const {
    return m_startup.active_handle();
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
  [[nodiscard]] std::span<const std::int16_t> area_mapping_entries() const {
    return m_startup.area_mapping_entries();
  }
  [[nodiscard]] const Omikron::IamAreaRecord* area_record() const {
    return m_startup.area_record();
  }
  [[nodiscard]] std::size_t active_area_slot() const {
    return m_startup.active_area_slot();
  }
  [[nodiscard]] std::int32_t active_area_id() const {
    return m_startup.active_area_id();
  }
  [[nodiscard]] const RuntimeAreaSlot* runtime_area_slot(const std::size_t index) const {
    return m_startup.runtime_area_slot(index);
  }
  [[nodiscard]] bool area_transition_pending() const {
    return m_startup.area_transition_pending();
  }
  [[nodiscard]] const Script::AreaScriptRuntime* area_script() const {
    return m_startup.area_script();
  }
  [[nodiscard]] const Script::AreaScriptRuntime* area_script(
      const std::size_t resident_slot) const {
    return m_startup.area_script(resident_slot);
  }
  [[nodiscard]] bool ticked() const {
    return m_startup.ticked();
  }
  /// True while an AREA-started dialog suppresses normal AREA VM servicing.
  [[nodiscard]] bool dialog_takeover_active() const {
    return m_startup.dialog_takeover_active();
  }
  [[nodiscard]] std::optional<std::int16_t> dialog_takeover_id() const {
    return m_startup.dialog_takeover_id();
  }
  [[nodiscard]] const std::string& initial_world_scenario_path() const {
    return m_startup.initial_world_scenario_path();
  }
  [[nodiscard]] const std::string& initial_world_decor_path() const {
    return m_startup.initial_world_decor_path();
  }
  [[nodiscard]] const std::string& initial_world_decor_state() const {
    return m_startup.initial_world_decor_state();
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
