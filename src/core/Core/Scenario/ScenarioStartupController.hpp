#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Omikron/IamArchive.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamStart.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"

namespace App::Startup {
class StartupTraceRecorder;
}

namespace App::Audio {
class AudioSystem;
}

namespace App {

class ScenarioManager;

/// One runtime area slot, mirroring Runtime's `RuntimeAreaSlot` layout with
/// safe ownership: the parsed primary area record is owned here, and the
/// primary/secondary area IDs are ordinary values rather than serialized
/// pointers.
struct RuntimeAreaSlot {
  std::optional<Omikron::IamAreaRecord> primary;
  std::int32_t primary_area_id{-1};
  std::int32_t secondary_area_id{-1};
};

/// Staged startup that follows the recovered Runtime.exe path:
/// IAM/START -> IAM/AREA record -> GRID.3DO/GRID.SCX dependencies ->
/// area script context -> interface 29 -> native main menu.
///
/// The controller owns all loaded byte buffers; parsers and the script VM
/// borrow from those buffers, never from raw relocated pointers. Loading a
/// script never executes it: event/state 1 is queued, the context is
/// explicitly activated, and tick() runs the first interpreter tick.
class ScenarioStartupController {
 public:
  ScenarioStartupController() = default;
  ~ScenarioStartupController() = default;

  ScenarioStartupController(const ScenarioStartupController&) = delete;
  ScenarioStartupController(ScenarioStartupController&&) = delete;
  ScenarioStartupController& operator=(const ScenarioStartupController&) = delete;
  ScenarioStartupController& operator=(ScenarioStartupController&&) = delete;

  /// Wires the optional startup trace recorder. Fine-grained IAM/area-script
  /// events are recorded here; coarse mode-level events live in ScenarioEngine.
  void set_trace_recorder(Startup::StartupTraceRecorder* trace);

  /// Wires the application audio system so the AREA music opcode (0x67) can
  /// request numbered tracks. May be null (music is non-fatal).
  void set_audio_system(Audio::AudioSystem* audio);

  /// Selects aventure.SCX into the permanent gameplay-mode slot. Does not
  /// load any IAM data.
  [[nodiscard]] std::expected<void, std::string> select_permanent_mode_script(
      ScenarioManager& manager);

  /// Clears all transient session state (area IDs, mapping, record, script).
  void reset_session();

  /// Mode 2: new-session initialization. Loads IAM/START, selects and loads
  /// the initial area, loads its dependencies, creates the area context, and
  /// queues/activates event 1. Precondition: reset_session().
  [[nodiscard]] std::expected<void, std::string> initialize_new_session(ScenarioManager& manager);

  /// Compatibility entry point: select the permanent mode script, reset the
  /// scenario state, then initialize the new session.
  [[nodiscard]] std::expected<void, std::string> initialize(ScenarioManager& manager);

  /// Executes one area-script interpreter tick (mode 1).
  [[nodiscard]] std::expected<void, std::string> tick();

  /// True once the new session has been initialized (area script exists).
  [[nodiscard]] bool initialized() const {
    return m_initialized;
  }

  /// Delivers an interface completion to the waiting area script. Resumes the
  /// script when the completion matches the stored interface handle; logs and
  /// ignores stale/mismatched completions.
  [[nodiscard]] std::expected<void, std::string> complete_interface(
      const InterfaceCompletion& completion);

  /// The UI dispatch owning the native main-menu transition.
  [[nodiscard]] InterfaceDispatcher& dispatcher() {
    return m_dispatcher;
  }
  [[nodiscard]] const InterfaceDispatcher& dispatcher() const {
    return m_dispatcher;
  }

  [[nodiscard]] std::int16_t initial_area_id() const {
    return m_initial_area_id;
  }
  [[nodiscard]] std::int16_t linked_area_id() const {
    return m_linked_area_id;
  }
  /// Reproduced `areaMapping[areaId]` value, or nullopt when unset.
  [[nodiscard]] std::optional<std::int32_t> area_mapping(std::int32_t area_id) const;
  /// All recovered area-mapping entries (diagnostics).
  [[nodiscard]] const std::unordered_map<std::int32_t, std::int32_t>& area_mapping_entries() const;
  [[nodiscard]] const Omikron::IamAreaRecord* area_record() const;
  [[nodiscard]] const Script::AreaScriptRuntime* area_script() const;
  [[nodiscard]] bool ticked() const {
    return m_ticked;
  }
  [[nodiscard]] const std::string& grid_scx_path() const {
    return m_grid_scx_path;
  }
  [[nodiscard]] const std::string& grid_3do_path() const {
    return m_grid_3do_path;
  }
  /// Honest dependency state string for GRID.3DO (requested/loaded/...).
  [[nodiscard]] const std::string& grid_3do_state() const {
    return m_grid_3do_state;
  }
  /// Parsed GRID.3DO decor geometry, or nullptr when it was not loaded.
  [[nodiscard]] const Omikron::Model3DOData* grid_3do_model() const {
    return m_grid_3do_model.has_value() ? &*m_grid_3do_model : nullptr;
  }
  [[nodiscard]] const std::string& last_error() const {
    return m_last_error;
  }

 private:
  /// Reads a whole file through the case-insensitive game-data resolver.
  [[nodiscard]] static std::expected<std::vector<std::byte>, std::string> read_file(
      const std::string& relative_path);

  /// Records one fine-grained startup trace event (no-op without a recorder).
  void record(std::string name, std::string detail = {});

  std::vector<std::byte> m_start_bytes;
  std::vector<std::byte> m_area_archive_bytes;

  std::optional<Omikron::IamStart> m_start;
  std::optional<Omikron::IamIndexedArchive> m_area_archive;
  /// Two runtime area slots; slot 0 holds the initial area, slot 1 is empty
  /// unless the linked/secondary area is populated.
  std::array<RuntimeAreaSlot, 2> m_area_slots{};
  std::optional<Script::AreaScriptRuntime> m_area_script;
  /// Parsed GRID.3DO decor geometry (loaded best-effort; not required to
  /// render the menu).
  std::optional<Omikron::Model3DOData> m_grid_3do_model;

  /// Reproduces Runtime's `areaMapping[areaId] = linkedAreaId` assignment.
  std::unordered_map<std::int32_t, std::int32_t> m_area_mapping;

  std::int16_t m_initial_area_id{0};
  std::int16_t m_linked_area_id{0};
  std::string m_grid_scx_path;
  std::string m_grid_3do_path;
  std::string m_grid_3do_state;
  std::string m_last_error;
  bool m_initialized{false};
  bool m_ticked{false};
  /// True once event 1 has been recorded as started (avoids per-frame spam).
  bool m_event_started{false};
  /// True once the transition into interface waiting has been recorded.
  bool m_waiting_recorded{false};
  /// Optional startup trace recorder (fine-grained events).
  Startup::StartupTraceRecorder* m_trace{nullptr};
  /// Application audio system (may be null; music is non-fatal).
  Audio::AudioSystem* m_audio{nullptr};
  /// UI dispatch; owns the native main-menu transition state.
  InterfaceDispatcher m_dispatcher;
};

}  // namespace App
