#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"

namespace App {

enum class CharacterReferenceSource : std::uint8_t { k_area, k_scene };
enum class CharacterReferenceResolutionSource : std::uint8_t {
  k_area_placement,
  k_scene_placement,
  k_global_body,
};

enum class CharacterReferenceResolutionStatus : std::uint8_t {
  k_resolved,
  k_placement_without_body,
  k_not_found,
};

/// Distinguishes lazy materialization from Runtime's deliberate table-0
/// no-actor state. Only k_bound entries carry a physical body identity.
enum class CharacterPlacementBindingState : std::uint8_t {
  k_unmaterialized,
  k_bound,
  k_explicitly_unbound,
};

struct RuntimeCharacterReferenceEntry {
  CharacterReferenceSource source{CharacterReferenceSource::k_area};
  std::size_t resident_slot{0};
  std::int32_t area_id{-1};
  std::int32_t scene_id{-1};
  std::size_t placement_index{0};
  std::int16_t serialized_character_id{0};
  std::int16_t reference_character_id{0};
  CharacterPlacementBindingState binding_state{CharacterPlacementBindingState::k_unmaterialized};
  std::optional<Character::BodyIdentity> body_identity;
};

struct ResolvedCharacterReference {
  std::int16_t requested_reference_id{0};
  Character::BodyIdentity body_identity{0};
  std::uint32_t body_world_scene_id{0};
  CharacterReferenceResolutionSource source{CharacterReferenceResolutionSource::k_global_body};
  std::optional<std::size_t> resident_slot;
  std::optional<std::size_t> placement_index;
  std::optional<CharacterPlacementBindingState> binding_state;
};

struct CharacterReferenceResolution {
  CharacterReferenceResolutionStatus status{CharacterReferenceResolutionStatus::k_not_found};
  std::optional<ResolvedCharacterReference> resolved;
  std::optional<std::size_t> resident_slot;
  std::optional<std::size_t> placement_index;
  std::optional<CharacterPlacementBindingState> binding_state;
};

/// Mutable overlay for IAM table-0 character placement semantics.
/// Parsed IAM records remain immutable; possession mutates only these entries.
class CharacterReferenceRuntime {
 public:
  using BodyLocator =
      std::function<std::optional<ResolvedCharacterReference>(Character::BodyIdentity)>;
  using CanonicalLocator = std::function<std::optional<ResolvedCharacterReference>(std::int16_t)>;

  void set_body_locator(BodyLocator locator);
  void set_canonical_locator(CanonicalLocator locator);
  void reset();
  void install_area(
      std::size_t resident_slot, std::int32_t area_id, const Omikron::IamAreaRecord& area);
  void install_scene(std::size_t resident_slot,
      std::int32_t area_id,
      std::int32_t scene_id,
      const Omikron::IamSceneRecord& scene);
  void remove_area(std::size_t resident_slot, std::int32_t area_id);
  void remove_scene(std::size_t resident_slot, std::int32_t scene_id);
  [[nodiscard]] std::expected<void, std::string> bind_placement_body(
      CharacterReferenceSource source,
      std::size_t resident_slot,
      std::int32_t area_id,
      std::int32_t scene_id,
      std::size_t placement_index,
      Character::BodyIdentity body_identity);
  [[nodiscard]] std::expected<void, std::string> mark_placement_explicitly_unbound(
      CharacterReferenceSource source,
      std::size_t resident_slot,
      std::int32_t area_id,
      std::int32_t scene_id,
      std::size_t placement_index);
  [[nodiscard]] std::expected<void, std::string> rebind_placement(std::size_t entry_index,
      std::int16_t reference_character_id,
      Character::BodyIdentity body_identity);

  [[nodiscard]] std::optional<std::size_t> find_mutable_placement(
      std::size_t resident_slot, std::int32_t area_id, std::int16_t reference_id) const;
  [[nodiscard]] std::optional<std::size_t> find_mutable_scene_placement(std::size_t resident_slot,
      std::int32_t area_id,
      std::int32_t scene_id,
      std::int16_t reference_id) const;
  [[nodiscard]] const std::vector<RuntimeCharacterReferenceEntry>& entries() const {
    return m_entries;
  }
  [[nodiscard]] const RuntimeCharacterReferenceEntry* placement(std::size_t entry_index) const;

  [[nodiscard]] CharacterReferenceResolution resolve(
      std::size_t resident_slot, std::int32_t owner_area_id, std::int16_t reference_id) const;

 private:
  std::vector<RuntimeCharacterReferenceEntry> m_entries;
  BodyLocator m_body_locator;
  CanonicalLocator m_canonical_locator;
};

}  // namespace App
