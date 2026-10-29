#include "Core/Scenario/TriggerProxyService.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"

namespace App {

void TriggerProxyService::reset() {
  m_proxy.reset();
}

void TriggerProxyService::rebind_owner(const ControlledCharacterRef& owner) {
  if (m_proxy.has_value()) {
    m_proxy->owner = owner;
  }
}

void TriggerProxyService::set_overlapping_zone_count(const std::size_t count) {
  if (m_proxy.has_value()) {
    m_proxy->overlapping_zone_count = count;
  }
}

void TriggerProxyService::register_proxy(
    const ControlledCharacterRef& owner, const Character::RuntimeCharacter& character) {
  const std::optional<std::size_t> actor_object_index{
      character.model_resource == nullptr ? std::nullopt
                                          : character.model_resource->actor_object_index};
  const bool actor_object_valid{
      character.model_resource != nullptr && actor_object_index.has_value() &&
      actor_object_index.value() < character.model_resource->model.meshes.size()};
  m_proxy = CurrentCharacterTriggerProxy{.owner = owner,
      .registered = true,
      .contact_ready = false,
      .position = character.transform.translation,
      .radius = character.actor_spatial_radius().value_or(0.0F),
      .actor_object_index = actor_object_index,
      .actor_object_name =
          actor_object_valid
              ? character.model_resource->model.meshes.at(actor_object_index.value()).name
              : std::string{},
      .model_bounds_radius =
          character.model_resource == nullptr ? 0.0F : character.model_resource->bounds_radius,
      .heading_degrees = character.principal_orientation_degrees.y,
      .generation = character.ordinary_actor_service_generation,
      .last_consumed_actor_spatial_generation = character.ordinary_actor_service_generation,
      .overlapping_zone_count = 0,
      .synchronization_suspended = false,
      .suspension_reason = {}};
  App::Log::info(LogCategory::Scenario,
      "TriggerProxyRegistered — character={} world={} generation={} xyz=({:.3f},{:.3f},{:.3f}) "
      "actorObject=[{}:'{}'] actorObjectRadius={:.3f} modelBoundsRadius={:.3f}",
      owner.character_id,
      owner.world_scene_id,
      m_proxy->generation,
      character.transform.translation.x,
      character.transform.translation.y,
      character.transform.translation.z,
      actor_object_index.has_value() ? fmt::format("{}", actor_object_index.value()) : "invalid",
      m_proxy->actor_object_name,
      m_proxy->radius,
      m_proxy->model_bounds_radius);
}

bool TriggerProxyService::publish_ordinary_sample(
    const ControlledCharacterRef& owner, const Character::RuntimeCharacter& character) {
  if (!m_proxy.has_value() ||
      !m_proxy->registered) {
    return false;
  }
  CurrentCharacterTriggerProxy& proxy{m_proxy.value()};
  if (owner != proxy.owner) {
    proxy.registered = false;
    proxy.synchronization_suspended = true;
    proxy.suspension_reason = "controlled-character ownership changed";
    return false;
  }
  if (!character.active || !character.area_present) {
    proxy.synchronization_suspended = true;
    proxy.suspension_reason = "current character is not active and AREA-present";
    return false;
  }
  if (character.body_identity != proxy.owner.body_identity) {
    proxy.registered = false;
    proxy.synchronization_suspended = true;
    proxy.suspension_reason = "current character body identity changed";
    return false;
  }
  if (proxy.last_consumed_actor_spatial_generation >= character.ordinary_actor_service_generation) {
    if (!proxy.contact_ready) {
      proxy.synchronization_suspended = true;
      proxy.suspension_reason = "ordinary actor service not yet consumed";
      return false;
    }
    proxy.synchronization_suspended = false;
    proxy.suspension_reason.clear();
    return false;
  }
  proxy.position = character.transform.translation;
  proxy.radius = character.actor_spatial_radius().value_or(0.0F);
  proxy.actor_object_index = character.model_resource == nullptr
                                 ? std::nullopt
                                 : character.model_resource->actor_object_index;
  proxy.actor_object_name =
      character.model_resource != nullptr && proxy.actor_object_index.has_value() &&
              proxy.actor_object_index.value() < character.model_resource->model.meshes.size()
          ? character.model_resource->model.meshes.at(proxy.actor_object_index.value()).name
          : std::string{};
  proxy.model_bounds_radius =
      character.model_resource == nullptr ? 0.0F : character.model_resource->bounds_radius;
  proxy.heading_degrees = character.principal_orientation_degrees.y;
  proxy.last_consumed_actor_spatial_generation = character.ordinary_actor_service_generation;
  proxy.generation = proxy.last_consumed_actor_spatial_generation;
  proxy.contact_ready = true;
  proxy.synchronization_suspended = false;
  proxy.suspension_reason.clear();
  App::Log::debug(LogCategory::Scenario,
      "TriggerProxySynchronized — character={} world={} generation={} xyz=({:.3f},{:.3f},{:.3f}) "
      "actorObject=[{}:'{}'] actorObjectRadius={:.3f} modelBoundsRadius={:.3f}",
      proxy.owner.character_id,
      proxy.owner.world_scene_id,
      proxy.generation,
      proxy.position.x,
      proxy.position.y,
      proxy.position.z,
      proxy.actor_object_index.has_value() ? fmt::format("{}", proxy.actor_object_index.value())
                                           : "invalid",
      proxy.actor_object_name,
      proxy.radius,
      proxy.model_bounds_radius);
  return true;
}

void TriggerProxyService::housekeeping(const ScenarioManager& manager) {
  if (!m_proxy.has_value() ||
      !m_proxy->registered) {
    return;
  }
  CurrentCharacterTriggerProxy& proxy{m_proxy.value()};
  const std::optional<ControlledCharacterRef> current{manager.controlled_character()};
  if (!current.has_value() || current.value() != proxy.owner) {
    proxy.registered = false;
    proxy.synchronization_suspended = true;
    proxy.suspension_reason = "controlled-character ownership changed";
    return;
  }
  const ScenarioRuntime* const scenario{manager.world_runtime(proxy.owner.world_scene_id)};
  const Character::RuntimeCharacter* const character{
      scenario == nullptr ? nullptr
                          : scenario->character_runtime().find_body(proxy.owner.body_identity)};
  if (character == nullptr || !character->active || !character->area_present) {
    proxy.synchronization_suspended = true;
    proxy.suspension_reason = "current character is not active and AREA-present";
    return;
  }
  if (character->body_identity != proxy.owner.body_identity) {
    proxy.registered = false;
    proxy.synchronization_suspended = true;
    proxy.suspension_reason = "current character body identity changed";
    return;
  }
  if (!proxy.contact_ready) {
    proxy.synchronization_suspended = true;
    proxy.suspension_reason = "ordinary actor service not yet consumed";
  }
}

}  // namespace App
