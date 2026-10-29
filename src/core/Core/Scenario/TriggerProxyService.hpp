#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Scenario/ScenarioManager.hpp"

namespace App {

/// Session-owned spatial trigger proxy for the durable current character.
/// Runtime updates this independently from scripted presentation transforms.
struct CurrentCharacterTriggerProxy {
  ControlledCharacterRef owner;
  bool registered{false};
  bool contact_ready{false};
  Runtime::Vec3 position{};
  float radius{0.0F};
  std::optional<std::size_t> actor_object_index;
  std::string actor_object_name;
  float model_bounds_radius{0.0F};
  float heading_degrees{0.0F};
  /// The actor spatial-service generation most recently consumed by this proxy.
  /// This is not a movement counter; it advances only when the current actor
  /// genuinely receives the ordinary service path that copies logical XYZ into
  /// the registered spatial proxy.
  std::uint64_t generation{0};
  std::uint64_t last_consumed_actor_spatial_generation{0};
  std::size_t overlapping_zone_count{0};
  bool synchronization_suspended{false};
  std::string suspension_reason;
};

/// Owns the single spatial trigger proxy of the current character: registration when a body
/// becomes current, the per-step ordinary spatial sample, and suspension bookkeeping.
class TriggerProxyService {
 public:
  void reset();

  [[nodiscard]] const std::optional<CurrentCharacterTriggerProxy>& proxy() const {
    return m_proxy;
  }

  /// Registers (replacing any previous proxy) the proxy of a newly selected current body.
  void register_proxy(const ControlledCharacterRef& owner, const Character::RuntimeCharacter& character);

  /// Publishes one ordinary actor spatial sample. Returns true when the proxy consumed a fresh
  /// actor-service generation (a fresh sample for zone qualification).
  [[nodiscard]] bool publish_ordinary_sample(
      const ControlledCharacterRef& owner, const Character::RuntimeCharacter& character);

  /// Non-sampling bookkeeping: drops or suspends the proxy when its owner/body is gone.
  void housekeeping(const ScenarioManager& manager);

  /// Moves the proxy to the owner reference after an event-9 body-storage handoff.
  void rebind_owner(const ControlledCharacterRef& owner);

  void set_overlapping_zone_count(std::size_t count);

 private:
  std::optional<CurrentCharacterTriggerProxy> m_proxy;
};

}  // namespace App
