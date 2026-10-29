#include "Core/Scenario/AreaResidencyService.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/GameState.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Scenario/RuntimeAreaSlot.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"

namespace App {

AreaResidencyService::AreaResidencyService(AreaResidencyHost& host) : m_host{host} {}

void AreaResidencyService::reset() {
  m_active_slot = 0;
}

std::optional<std::size_t> AreaResidencyService::resident_slot(const std::int32_t area_id) const {
  const std::span<const RuntimeAreaSlot> slots{m_host.residency_slots()};
  for (std::size_t index{0}; index < slots.size(); ++index) {
    if (slots[index].primary.has_value() && slots[index].primary_area_id == area_id) {
      return index;
    }
  }
  return std::nullopt;
}

std::expected<std::size_t, std::string> AreaResidencyService::current_slot() const {
  const ScenarioManager* const manager{m_host.residency_manager()};
  const std::span<const RuntimeAreaSlot> slots{m_host.residency_slots()};
  if (manager == nullptr) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "logical current AREA manager is unavailable"};
  }
  if (m_active_slot >= slots.size()) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "logical current AREA slot is out of range"};
  }
  const RuntimeAreaSlot& slot{slots[m_active_slot]};
  if (!slot.primary.has_value() || slot.primary_area_id < 0) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "logical current AREA slot is not resident"};
  }
  const WorldSceneContext* const current{manager->current_world_context()};
  if (current == nullptr || current->scene_id != slot.world_scene_id) {
    return std::expected<std::size_t, std::string>{std::unexpect,
        fmt::format("logical current AREA disagreement: slot {} is AREA {}/world {}, manager is "
                    "world {}",
            m_active_slot,
            slot.primary_area_id,
            slot.world_scene_id,
            current == nullptr ? -1 : static_cast<std::int64_t>(current->scene_id))};
  }
  return m_active_slot;
}

std::expected<bool, std::string> AreaResidencyService::commit_physical_handoff(
    const std::uint32_t accepted_support_world) {
  auto current_slot_result{current_slot()};
  if (!current_slot_result) {
    return std::expected<bool, std::string>{std::unexpect, std::move(current_slot_result).error()};
  }
  ScenarioManager& manager{*m_host.residency_manager()};
  const std::span<const RuntimeAreaSlot> slots{m_host.residency_slots()};
  const RuntimeAreaSlot& logical_slot{slots[current_slot_result.value()]};
  if (accepted_support_world == logical_slot.world_scene_id) {
    return false;
  }

  const auto destination{
      std::ranges::find(slots, accepted_support_world, &RuntimeAreaSlot::world_scene_id)};
  const WorldSceneContext* const destination_context{
      manager.find_world_context(accepted_support_world)};
  if (destination == slots.end() || !destination->primary.has_value() ||
      destination->primary_area_id < 0 || destination_context == nullptr ||
      destination_context->residency != WorldSceneResidencyState::ResidentAttached) {
    return false;
  }
  const std::optional<ControlledCharacterRef> controlled{manager.controlled_character()};
  if (!controlled.has_value()) {
    return std::expected<bool, std::string>{
        std::unexpect, "physical AREA handoff has no selected controlled body"};
  }

  const std::uint32_t body_owner_world{controlled->world_scene_id};
  const bool body_transfer_required{body_owner_world != accepted_support_world};
  manager.reverse_attached_world_order();
  if (auto selected{manager.set_current_world_context(accepted_support_world)}; !selected) {
    manager.reverse_attached_world_order();
    return std::expected<bool, std::string>{std::unexpect, selected.error()};
  }
  if (body_transfer_required) {
    if (auto transferred{
            manager.transfer_controlled_character(body_owner_world, accepted_support_world)};
        !transferred) {
      auto restored{manager.set_current_world_context(logical_slot.world_scene_id)};
      manager.reverse_attached_world_order();
      if (!restored) {
        return std::expected<bool, std::string>{std::unexpect,
            fmt::format("{}; failed to restore logical current world: {}",
                transferred.error(),
                restored.error())};
      }
      return std::expected<bool, std::string>{std::unexpect, transferred.error()};
    }
  }

  // Commit point: the chain order, current world, and body storage have moved.
  const std::size_t destination_slot{
      static_cast<std::size_t>(std::distance(slots.begin(), destination))};
  m_active_slot = destination_slot;
  if (GameState* const game_state{manager.game_state()}; game_state != nullptr) {
    game_state->set_current_area(static_cast<std::int16_t>(destination->primary_area_id));
  }
  const std::optional<ControlledCharacterRef> updated{manager.controlled_character()};
  if (!updated.has_value()) {
    return std::expected<bool, std::string>{
        std::unexpect, "controlled body selection vanished during AREA handoff"};
  }
  ScenarioRuntime* const runtime{manager.world_runtime(updated->world_scene_id)};
  Character::RuntimeCharacter* const character{
      runtime == nullptr ? nullptr : runtime->character_runtime().find_body(updated->body_identity)};
  if (character == nullptr) {
    return std::expected<bool, std::string>{
        std::unexpect, "controlled body vanished during AREA handoff"};
  }
  character->area_id = destination->primary_area_id;
  character->scene_id.reset();
  if (auto followed{m_host.residency_handoff_committed(*updated)}; !followed) {
    return std::expected<bool, std::string>{std::unexpect, followed.error()};
  }

  App::Log::debug(LogCategory::Scenario,
      "AREA physical handoff — logicalCurrent={}/world{} bodyOwner=world{} "
      "supportOwner={}/world{} targetSlot={} bodyTransfer={}",
      logical_slot.primary_area_id,
      logical_slot.world_scene_id,
      body_owner_world,
      destination->primary_area_id,
      accepted_support_world,
      destination_slot,
      body_transfer_required ? "yes" : "no");
  return true;
}

}  // namespace App
