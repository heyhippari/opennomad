#include "Core/Scenario/CharacterReferenceRuntime.hpp"

#include <algorithm>
#include <expected>
#include <string>
#include <utility>

namespace App {

void CharacterReferenceRuntime::set_body_locator(BodyLocator locator) {
  m_body_locator = std::move(locator);
}

void CharacterReferenceRuntime::set_canonical_locator(CanonicalLocator locator) {
  m_canonical_locator = std::move(locator);
}

void CharacterReferenceRuntime::reset() {
  m_entries.clear();
}

void CharacterReferenceRuntime::install_area(const std::size_t resident_slot,
    const std::int32_t area_id,
    const Omikron::IamAreaRecord& area) {
  if (std::ranges::any_of(
          m_entries, [resident_slot, area_id](const RuntimeCharacterReferenceEntry& entry) {
            return entry.source == CharacterReferenceSource::k_area &&
                   entry.resident_slot == resident_slot && entry.area_id == area_id;
          })) {
    return;
  }
  remove_area(resident_slot, area_id);
  const std::vector<Omikron::IamAreaCharacterRecord> placements{area.character_placements()};
  for (std::size_t index{0}; index < placements.size(); ++index) {
    const std::int16_t id{placements.at(index).character_id};
    m_entries.push_back(RuntimeCharacterReferenceEntry{.source = CharacterReferenceSource::k_area,
        .resident_slot = resident_slot,
        .area_id = area_id,
        .scene_id = -1,
        .placement_index = index,
        .serialized_character_id = id,
        .reference_character_id = id,
        .binding_state = CharacterPlacementBindingState::k_unmaterialized,
        .body_identity = std::nullopt});
  }
}

void CharacterReferenceRuntime::install_scene(const std::size_t resident_slot,
    const std::int32_t area_id,
    const std::int32_t scene_id,
    const Omikron::IamSceneRecord& scene) {
  if (std::ranges::any_of(
          m_entries, [resident_slot, scene_id](const RuntimeCharacterReferenceEntry& entry) {
            return entry.source == CharacterReferenceSource::k_scene &&
                   entry.resident_slot == resident_slot && entry.scene_id == scene_id;
          })) {
    return;
  }
  remove_scene(resident_slot, scene_id);
  const std::vector<Omikron::IamSceneCharacterRecord> placements{scene.character_placements()};
  for (std::size_t index{0}; index < placements.size(); ++index) {
    const std::int16_t id{placements.at(index).character_id};
    m_entries.push_back(RuntimeCharacterReferenceEntry{.source = CharacterReferenceSource::k_scene,
        .resident_slot = resident_slot,
        .area_id = area_id,
        .scene_id = scene_id,
        .placement_index = index,
        .serialized_character_id = id,
        .reference_character_id = id,
        .binding_state = CharacterPlacementBindingState::k_unmaterialized,
        .body_identity = std::nullopt});
  }
}

void CharacterReferenceRuntime::remove_area(
    const std::size_t resident_slot, const std::int32_t area_id) {
  std::erase_if(m_entries, [resident_slot, area_id](const RuntimeCharacterReferenceEntry& entry) {
    return entry.source == CharacterReferenceSource::k_area &&
           entry.resident_slot == resident_slot && entry.area_id == area_id;
  });
  std::erase_if(m_entries, [resident_slot, area_id](const RuntimeCharacterReferenceEntry& entry) {
    return entry.source == CharacterReferenceSource::k_scene &&
           entry.resident_slot == resident_slot && entry.area_id == area_id;
  });
}

std::expected<void, std::string> CharacterReferenceRuntime::bind_placement_body(
    const CharacterReferenceSource source,
    const std::size_t resident_slot,
    const std::int32_t area_id,
    const std::int32_t scene_id,
    const std::size_t placement_index,
    const Character::BodyIdentity body_identity) {
  const auto found{std::ranges::find_if(m_entries,
      [source, resident_slot, area_id, scene_id, placement_index](
          const RuntimeCharacterReferenceEntry& entry) {
        return entry.source == source && entry.resident_slot == resident_slot &&
               entry.area_id == area_id && entry.scene_id == scene_id &&
               entry.placement_index == placement_index;
      })};
  if (found == m_entries.end()) {
    return std::expected<void, std::string>{std::unexpect, "character placement is not installed"};
  }
  found->body_identity = body_identity;
  found->binding_state = CharacterPlacementBindingState::k_bound;
  return {};
}

std::expected<void, std::string> CharacterReferenceRuntime::mark_placement_explicitly_unbound(
    const CharacterReferenceSource source,
    const std::size_t resident_slot,
    const std::int32_t area_id,
    const std::int32_t scene_id,
    const std::size_t placement_index) {
  const auto found{std::ranges::find_if(m_entries,
      [source, resident_slot, area_id, scene_id, placement_index](
          const RuntimeCharacterReferenceEntry& entry) {
        return entry.source == source && entry.resident_slot == resident_slot &&
               entry.area_id == area_id && entry.scene_id == scene_id &&
               entry.placement_index == placement_index;
      })};
  if (found == m_entries.end()) {
    return std::expected<void, std::string>{std::unexpect, "character placement is not installed"};
  }
  found->body_identity.reset();
  found->binding_state = CharacterPlacementBindingState::k_explicitly_unbound;
  return {};
}

std::expected<void, std::string> CharacterReferenceRuntime::rebind_placement(
    const std::size_t entry_index,
    const std::int16_t reference_character_id,
    const Character::BodyIdentity body_identity) {
  if (entry_index >= m_entries.size()) {
    return std::expected<void, std::string>{std::unexpect, "character placement is not installed"};
  }
  RuntimeCharacterReferenceEntry& entry{m_entries.at(entry_index)};
  entry.reference_character_id = reference_character_id;
  entry.body_identity = body_identity;
  entry.binding_state = CharacterPlacementBindingState::k_bound;
  return {};
}

const RuntimeCharacterReferenceEntry* CharacterReferenceRuntime::placement(
    const std::size_t entry_index) const {
  return entry_index < m_entries.size() ? &m_entries.at(entry_index) : nullptr;
}

void CharacterReferenceRuntime::remove_scene(
    const std::size_t resident_slot, const std::int32_t scene_id) {
  std::erase_if(m_entries, [resident_slot, scene_id](const RuntimeCharacterReferenceEntry& entry) {
    return entry.source == CharacterReferenceSource::k_scene &&
           entry.resident_slot == resident_slot && entry.scene_id == scene_id;
  });
}

std::optional<std::size_t> CharacterReferenceRuntime::find_mutable_placement(
    const std::size_t resident_slot,
    const std::int32_t area_id,
    const std::int16_t reference_id) const {
  std::optional<std::size_t> no_body_match;
  for (std::size_t index{0}; index < m_entries.size(); ++index) {
    const RuntimeCharacterReferenceEntry& entry{m_entries.at(index)};
    if (entry.source == CharacterReferenceSource::k_area && entry.resident_slot == resident_slot &&
        entry.area_id == area_id && entry.reference_character_id == reference_id) {
      if (entry.binding_state == CharacterPlacementBindingState::k_bound) {
        return index;
      }
      if (!no_body_match.has_value()) {
        no_body_match = index;
      }
    }
  }
  return no_body_match;
}

std::optional<std::size_t> CharacterReferenceRuntime::find_mutable_scene_placement(
    const std::size_t resident_slot,
    const std::int32_t area_id,
    const std::int32_t scene_id,
    const std::int16_t reference_id) const {
  std::optional<std::size_t> no_body_match;
  for (std::size_t index{0}; index < m_entries.size(); ++index) {
    const RuntimeCharacterReferenceEntry& entry{m_entries.at(index)};
    if (entry.source == CharacterReferenceSource::k_scene && entry.resident_slot == resident_slot &&
        entry.area_id == area_id && entry.scene_id == scene_id &&
        entry.reference_character_id == reference_id) {
      if (entry.binding_state == CharacterPlacementBindingState::k_bound) {
        return index;
      }
      if (!no_body_match.has_value()) {
        no_body_match = index;
      }
    }
  }
  return no_body_match;
}

CharacterReferenceResolution CharacterReferenceRuntime::resolve(const std::size_t resident_slot,
    const std::int32_t owner_area_id,
    const std::int16_t reference_id) const {
  const auto resolve_entry = [this, reference_id](const std::size_t index) {
    const RuntimeCharacterReferenceEntry& entry{m_entries.at(index)};
    if (entry.binding_state != CharacterPlacementBindingState::k_bound ||
        !entry.body_identity.has_value() || !m_body_locator) {
      return CharacterReferenceResolution{
          .status = CharacterReferenceResolutionStatus::k_placement_without_body,
          .resolved = std::nullopt,
          .resident_slot = entry.resident_slot,
          .placement_index = entry.placement_index,
          .binding_state = entry.binding_state};
    }
    std::optional<ResolvedCharacterReference> located{m_body_locator(entry.body_identity.value())};
    if (!located.has_value()) {
      return CharacterReferenceResolution{
          .status = CharacterReferenceResolutionStatus::k_placement_without_body,
          .resolved = std::nullopt,
          .resident_slot = entry.resident_slot,
          .placement_index = entry.placement_index,
          .binding_state = entry.binding_state};
    }
    located->requested_reference_id = reference_id;
    located->source = entry.source == CharacterReferenceSource::k_area
                          ? CharacterReferenceResolutionSource::k_area_placement
                          : CharacterReferenceResolutionSource::k_scene_placement;
    located->resident_slot = entry.resident_slot;
    located->placement_index = entry.placement_index;
    return CharacterReferenceResolution{.status = CharacterReferenceResolutionStatus::k_resolved,
        .resolved = std::move(located),
        .resident_slot = entry.resident_slot,
        .placement_index = entry.placement_index,
        .binding_state = entry.binding_state};
  };

  if (const auto area_index{find_mutable_placement(resident_slot, owner_area_id, reference_id)};
      area_index.has_value()) {
    return resolve_entry(area_index.value());
  }
  for (std::size_t index{0}; index < m_entries.size(); ++index) {
    const RuntimeCharacterReferenceEntry& entry{m_entries.at(index)};
    if (entry.source == CharacterReferenceSource::k_scene && entry.resident_slot == resident_slot &&
        entry.area_id == owner_area_id && entry.reference_character_id == reference_id) {
      return resolve_entry(index);
    }
  }
  if (!m_canonical_locator) {
    return {};
  }
  std::optional<ResolvedCharacterReference> located{m_canonical_locator(reference_id)};
  if (located.has_value()) {
    located->requested_reference_id = reference_id;
    located->source = CharacterReferenceResolutionSource::k_global_body;
    located->resident_slot.reset();
    located->placement_index.reset();
  }
  return CharacterReferenceResolution{
      .status = located.has_value() ? CharacterReferenceResolutionStatus::k_resolved
                                    : CharacterReferenceResolutionStatus::k_not_found,
      .resolved = std::move(located),
      .resident_slot = std::nullopt,
      .placement_index = std::nullopt,
      .binding_state = std::nullopt};
}

}  // namespace App
