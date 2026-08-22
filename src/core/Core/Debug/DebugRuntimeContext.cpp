#include "Core/Debug/DebugRuntimeContext.hpp"

#include <cstddef>
#include <optional>
#include <span>

#include "Core/Scenario/ScenarioManager.hpp"

namespace App::Debug {
namespace {

ResolvedDebugRuntimeTarget resolve_world_context(const DebugRuntimeTarget target,
    const ScenarioManager& manager,
    const WorldSceneContext& context,
    const std::size_t slot) {
  const bool loaded{context.residency != WorldSceneResidencyState::Free};
  ScenarioRuntime* const runtime{loaded ? context.runtime.get() : nullptr};
  return ResolvedDebugRuntimeTarget{.identity = DebugRuntimeIdentity{.requested = target,
                                        .owner = &manager,
                                        .role = ScenarioRole::WorldScene,
                                        .slot = slot,
                                        .scene_id = context.scene_id,
                                        .generation = context.generation,
                                        .runtime = runtime},
      .runtime = runtime,
      .residency = context.residency,
      .gameplay_mode = std::nullopt,
      .scenario_path = context.scenario_path,
      .resolved_scenario_path = context.resolved_scenario_path};
}

}  // namespace

ResolvedDebugRuntimeTarget DebugRuntimeContext::resolve(
    const DebugRuntimeTarget target, const ScenarioManager* const manager) {
  ResolvedDebugRuntimeTarget result;
  result.identity.requested = target;
  result.identity.owner = manager;
  if (manager == nullptr) {
    return result;
  }

  switch (target) {
    case DebugRuntimeTarget::k_active_world: {
      const WorldSceneContext* const active{manager->active_world_context()};
      if (active == nullptr) {
        return result;
      }
      const std::span<const WorldSceneContext, WorldSceneContext::k_capacity> contexts{
          manager->world_contexts()};
      const std::size_t slot{static_cast<std::size_t>(active - contexts.data())};
      return resolve_world_context(target, *manager, *active, slot);
    }

    case DebugRuntimeTarget::k_gameplay_mode: {
      const ScenarioIdentity slot_identity{manager->gameplay_identity()};
      ScenarioRuntime* const runtime{manager->gameplay_runtime()};
      result.identity.role = ScenarioRole::GameplayMode;
      result.identity.slot = slot_identity.slot;
      result.identity.generation = slot_identity.generation;
      result.identity.runtime = runtime;
      result.runtime = runtime;
      result.gameplay_mode = manager->current_gameplay_mode();
      result.scenario_path = manager->gameplay_scenario_path();
      result.resolved_scenario_path = manager->gameplay_resolved_scenario_path();
      return result;
    }

    case DebugRuntimeTarget::k_world_slot_0:
    case DebugRuntimeTarget::k_world_slot_1: {
      const std::size_t slot{target == DebugRuntimeTarget::k_world_slot_0 ? 0U : 1U};
      const auto contexts{manager->world_contexts()};
      // std::span has no bounds-checked at(); slot is constrained to 0 or 1 above.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      return resolve_world_context(target, *manager, contexts[slot], slot);
    }
  }

  return result;
}

bool DebugRuntimeContext::refresh(const ScenarioManager* const manager) {
  const ResolvedDebugRuntimeTarget next{resolve(m_selected_target, manager)};
  const bool changed{!m_initialized || next.identity != m_resolved.identity};
  m_initialized = true;
  m_resolved = next;
  if (changed) {
    ++m_selection_epoch;
  }
  return changed;
}

}  // namespace App::Debug
