#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "Core/Scenario/ScenarioManager.hpp"

namespace App {
class ScenarioRuntime;
}

namespace App::Debug {

/// User-selectable scenario runtime observed by scenario-scoped inspectors.
enum class DebugRuntimeTarget : std::uint8_t {
  k_active_world,
  k_gameplay_mode,
  k_world_slot_0,
  k_world_slot_1,
};

/// Stable resolved identity used to invalidate runtime-local inspector state.
///
/// The owner pointer distinguishes two ScenarioManager lifetimes. World
/// identities use their real slot and generation; gameplay uses the manager's
/// authoritative gameplay-slot generation.
struct DebugRuntimeIdentity {
  DebugRuntimeTarget requested{DebugRuntimeTarget::k_active_world};
  const ScenarioManager* owner{nullptr};
  std::optional<ScenarioRole> role;
  std::optional<std::size_t> slot;
  std::uint32_t scene_id{0};
  std::uint32_t generation{0};
  ScenarioRuntime* runtime{nullptr};

  friend bool operator==(const DebugRuntimeIdentity&, const DebugRuntimeIdentity&) = default;
};

/// Non-owning debugger view of one resolved ScenarioManager target.
struct ResolvedDebugRuntimeTarget {
  DebugRuntimeIdentity identity;
  ScenarioRuntime* runtime{nullptr};
  std::optional<WorldSceneResidencyState> residency;
  std::optional<GameplayMode> gameplay_mode;
  std::string_view scenario_path;
  std::string_view resolved_scenario_path;

  [[nodiscard]] bool available() const {
    return runtime != nullptr;
  }
};

/// Owns the global debugger runtime selection and detects resolved changes.
class DebugRuntimeContext {
 public:
  [[nodiscard]] DebugRuntimeTarget selected_target() const {
    return m_selected_target;
  }

  void set_selected_target(DebugRuntimeTarget target) {
    m_selected_target = target;
  }

  /// Resolves the current selection and advances the epoch if its identity
  /// changed. Returns true when runtime-local inspector state must be cleared.
  bool refresh(const ScenarioManager* manager);

  [[nodiscard]] const ResolvedDebugRuntimeTarget& resolved() const {
    return m_resolved;
  }

  [[nodiscard]] std::uint64_t selection_epoch() const {
    return m_selection_epoch;
  }

  [[nodiscard]] static ResolvedDebugRuntimeTarget resolve(
      DebugRuntimeTarget target, const ScenarioManager* manager);

 private:
  DebugRuntimeTarget m_selected_target{DebugRuntimeTarget::k_active_world};
  ResolvedDebugRuntimeTarget m_resolved{};
  std::uint64_t m_selection_epoch{0};
  bool m_initialized{false};
};

[[nodiscard]] constexpr std::string_view debug_runtime_target_name(
    const DebugRuntimeTarget target) {
  switch (target) {
    case DebugRuntimeTarget::k_active_world:
      return "Active World";
    case DebugRuntimeTarget::k_gameplay_mode:
      return "Gameplay Mode";
    case DebugRuntimeTarget::k_world_slot_0:
      return "World Slot 0";
    case DebugRuntimeTarget::k_world_slot_1:
      return "World Slot 1";
  }
  return "Unknown";
}

}  // namespace App::Debug
