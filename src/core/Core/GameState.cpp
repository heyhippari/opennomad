#include "Core/GameState.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/IamCharacterDefinition.hpp"
#include "Core/Omikron/IamStart.hpp"

namespace App {

namespace {

constexpr std::size_t K_BITS_PER_BYTE{8};
constexpr std::size_t K_PACKED_VALUES_PER_BYTE{4};
constexpr std::int16_t K_EMPTY_OBJECT_ID{-1};
constexpr std::int32_t K_SMALL_CHARACTER_VALUE_MAX{200};
constexpr std::int32_t K_UNSIGNED_CHARACTER_VALUE_MAX{65'535};

template <typename Value>
Value read_at(const std::span<const std::byte> data, const std::size_t offset) {
  Value value{};
  std::memcpy(&value, data.subspan(offset, sizeof(Value)).data(), sizeof(value));
  return value;
}

std::vector<std::uint8_t> copy_bytes(const std::span<const std::byte> data) {
  std::vector<std::uint8_t> result;
  result.reserve(data.size());
  for (const std::byte value : data) {
    result.push_back(std::to_integer<std::uint8_t>(value));
  }
  return result;
}

std::expected<std::int32_t, std::string> unsupported_character_value_kind(const std::int16_t kind) {
  return std::expected<std::int32_t, std::string>{
      std::unexpect, fmt::format("character value kind {} is not implemented", kind)};
}

}  // namespace

CharacterValueState::CharacterValueState(
    const Omikron::IamCharacterValueInitialState& initial_values)
    : m_values(initial_values) {}

std::expected<std::int32_t, std::string> CharacterValueState::get(const std::int16_t kind) const {
  switch (kind) {
    case 1:
      return m_values.energy;
    case 2:
      return m_values.mana;
    case 3:
      return m_values.speed;
    case 4:
      return m_values.seteks;
    case 5:
      return m_values.rings;
    case 16:
      return m_values.attack;
    case 17:
      return m_values.body_resistance;
    case 18:
      return m_values.dodge;
    case 19:
      return m_values.fight_experience;
    case 20:
      return m_values.unknown_characteristic_a8;
    default:
      return unsupported_character_value_kind(kind);
  }
}

std::expected<void, std::string> CharacterValueState::set(
    const std::int16_t kind, const std::int32_t value) {
  const auto clamped_small{static_cast<std::int16_t>(std::min(value, K_SMALL_CHARACTER_VALUE_MAX))};
  switch (kind) {
    case 1:
      m_values.energy = clamped_small;
      break;
    case 2:
      m_values.mana = clamped_small;
      break;
    case 3:
      m_values.speed = clamped_small;
      break;
    case 4:
      m_values.seteks = static_cast<std::uint16_t>(std::min(value, K_UNSIGNED_CHARACTER_VALUE_MAX));
      break;
    case 5:
      m_values.rings = static_cast<std::int16_t>(value);
      break;
    case 16:
      m_values.attack = clamped_small;
      break;
    case 17:
      m_values.body_resistance = clamped_small;
      break;
    case 18:
      m_values.dodge = clamped_small;
      break;
    case 19:
      m_values.fight_experience = clamped_small;
      break;
    case 20:
      m_values.unknown_characteristic_a8 = clamped_small;
      break;
    default:
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("character value kind {} is not implemented", kind)};
  }
  return {};
}

std::expected<GameState, std::string> GameState::from_start(const Omikron::IamStart& start) {
  auto serialized_globals{start.global_variables()};
  auto serialized_area_map{start.area_mappings()};
  auto serialized_packed{start.packed_state_bytes()};
  auto serialized_character_flags{start.character_flags()};
  auto serialized_address_flags{start.address_flags()};
  auto serialized_zone_flags{start.zone_flags()};
  auto serialized_current_character{start.current_character()};
  if (!serialized_globals || !serialized_area_map || !serialized_packed ||
      !serialized_character_flags || !serialized_address_flags || !serialized_zone_flags ||
      !serialized_current_character) {
    return std::expected<GameState, std::string>{
        std::unexpect, "cannot initialize GameState from invalid IAM/START region"};
  }

  GameState state;
  state.m_format_revision = start.format_revision();
  state.m_build_date = start.build_date();
  std::ranges::copy(start.opaque_header_state(), state.m_opaque_header_state.begin());
  state.m_saved_position = start.saved_position();
  state.m_saved_orientation = start.saved_orientation();
  state.m_current_area = start.current_area_id();
  state.m_linked_area = start.linked_area_id();
  std::ranges::copy(start.opaque_area_state(), state.m_opaque_area_state.begin());

  state.m_global_variables.reserve(serialized_globals->size() / sizeof(std::int32_t));
  for (std::size_t offset{0}; offset < serialized_globals->size(); offset += sizeof(std::int32_t)) {
    state.m_global_variables.push_back(read_at<std::int32_t>(*serialized_globals, offset));
  }
  state.m_area_mappings.reserve(serialized_area_map->size() / sizeof(std::int16_t));
  for (std::size_t offset{0}; offset < serialized_area_map->size();
      offset += sizeof(std::int16_t)) {
    state.m_area_mappings.push_back(read_at<std::int16_t>(*serialized_area_map, offset));
  }
  state.m_packed_state = copy_bytes(*serialized_packed);
  state.m_character_flags = copy_bytes(*serialized_character_flags);
  state.m_address_flags = copy_bytes(*serialized_address_flags);
  state.m_zone_flags = copy_bytes(*serialized_zone_flags);
  state.m_current_character = std::move(*serialized_current_character);

  for (std::size_t kind{0}; kind < state.m_object_collections.size(); ++kind) {
    auto serialized_collection{
        start.persistent_object_collection(static_cast<std::uint16_t>(kind))};
    if (!serialized_collection) {
      return std::expected<GameState, std::string>{std::unexpect,
          fmt::format("cannot initialize persistent object collection {}: {}",
              kind,
              serialized_collection.error())};
    }
    PersistentObjectCollection& collection{state.m_object_collections.at(kind)};
    collection.capacity = serialized_collection->size() / sizeof(std::int16_t);
    collection.object_ids.reserve(collection.capacity);
    for (std::size_t offset{0}; offset < serialized_collection->size();
        offset += sizeof(std::int16_t)) {
      collection.object_ids.push_back(read_at<std::int16_t>(*serialized_collection, offset));
    }
  }
  return state;
}

std::expected<std::int32_t, std::string> GameState::global_variable(const std::uint16_t id) const {
  if (id >= m_global_variables.size()) {
    return std::expected<std::int32_t, std::string>{std::unexpect,
        fmt::format("global variable ID {} is outside the {}-variable START region",
            id,
            m_global_variables.size())};
  }
  return m_global_variables.at(id);
}

std::expected<void, std::string> GameState::set_global_variable(
    const std::uint16_t id, const std::int32_t value) {
  if (id >= m_global_variables.size()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("global variable ID {} is outside the {}-variable START region",
            id,
            m_global_variables.size())};
  }
  m_global_variables.at(id) = value;
  return {};
}

std::span<const std::int32_t> GameState::global_variables() const {
  return m_global_variables;
}

std::expected<std::int16_t, std::string> GameState::area_mapping(const std::int32_t area_id) const {
  if (area_id < 0 || static_cast<std::size_t>(area_id) >= m_area_mappings.size()) {
    return std::expected<std::int16_t, std::string>{std::unexpect,
        fmt::format("area ID {} is outside the {}-entry persistent area map",
            area_id,
            m_area_mappings.size())};
  }
  return m_area_mappings.at(static_cast<std::size_t>(area_id));
}

std::expected<void, std::string> GameState::set_area_mapping(
    const std::int32_t area_id, const std::int16_t value) {
  if (area_id < 0 || static_cast<std::size_t>(area_id) >= m_area_mappings.size()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("area ID {} is outside the {}-entry persistent area map",
            area_id,
            m_area_mappings.size())};
  }
  m_area_mappings.at(static_cast<std::size_t>(area_id)) = value;
  return {};
}

std::span<const std::int16_t> GameState::area_mappings() const {
  return m_area_mappings;
}

std::expected<std::uint8_t, std::string> GameState::packed_state(const std::size_t index) const {
  const std::size_t capacity{m_packed_state.size() * K_PACKED_VALUES_PER_BYTE};
  if (index >= capacity) {
    return std::expected<std::uint8_t, std::string>{std::unexpect,
        fmt::format("packed-state index {} is outside the {}-entry region", index, capacity)};
  }
  const std::size_t shift{(index % K_PACKED_VALUES_PER_BYTE) * 2U};
  return static_cast<std::uint8_t>(
      (static_cast<std::uint32_t>(m_packed_state.at(index / K_PACKED_VALUES_PER_BYTE)) >> shift) &
      0x03U);
}

std::expected<void, std::string> GameState::set_packed_state(
    const std::size_t index, const std::uint8_t value) {
  if (value > 3U) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("packed-state value {} is outside [0, 3]", value)};
  }
  const std::size_t capacity{m_packed_state.size() * K_PACKED_VALUES_PER_BYTE};
  if (index >= capacity) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("packed-state index {} is outside the {}-entry region", index, capacity)};
  }
  const std::size_t byte_index{index / K_PACKED_VALUES_PER_BYTE};
  const std::size_t shift{(index % K_PACKED_VALUES_PER_BYTE) * 2U};
  const std::uint8_t mask{static_cast<std::uint8_t>(0x03U << shift)};
  std::uint8_t& byte{m_packed_state.at(byte_index)};
  byte = static_cast<std::uint8_t>(
      (byte & static_cast<std::uint8_t>(~mask)) | static_cast<std::uint8_t>(value << shift));
  return {};
}

std::span<const std::uint8_t> GameState::packed_state_raw() const {
  return m_packed_state;
}

std::expected<bool, std::string> GameState::read_bit(
    const std::span<const std::uint8_t> bytes, const std::size_t id, const std::string& label) {
  const std::size_t byte_index{id / K_BITS_PER_BYTE};
  if (byte_index >= bytes.size()) {
    return std::expected<bool, std::string>{std::unexpect,
        fmt::format("{} ID {} is outside the {}-bit persistent region",
            label,
            id,
            bytes.size() * K_BITS_PER_BYTE)};
  }
  const std::uint8_t mask{static_cast<std::uint8_t>(1U << (id % K_BITS_PER_BYTE))};
  // std::span has no bounds-checked accessor; byte_index was checked above.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return (bytes[byte_index] & mask) != 0U;
}

std::expected<void, std::string> GameState::write_bit(const std::span<std::uint8_t> bytes,
    const std::size_t id,
    const bool enabled,
    const std::string& label) {
  const std::size_t byte_index{id / K_BITS_PER_BYTE};
  if (byte_index >= bytes.size()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("{} ID {} is outside the {}-bit persistent region",
            label,
            id,
            bytes.size() * K_BITS_PER_BYTE)};
  }
  const std::uint8_t mask{static_cast<std::uint8_t>(1U << (id % K_BITS_PER_BYTE))};
  // std::span has no bounds-checked accessor; byte_index was checked above.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  std::uint8_t& byte{bytes[byte_index]};
  byte = enabled ? static_cast<std::uint8_t>(byte | mask)
                 : static_cast<std::uint8_t>(byte & static_cast<std::uint8_t>(~mask));
  return {};
}

std::expected<bool, std::string> GameState::character_flag(const std::uint16_t id) const {
  return read_bit(m_character_flags, id, "CHARACTERS");
}

std::expected<void, std::string> GameState::set_character_flag(
    const std::uint16_t id, const bool enabled) {
  return write_bit(m_character_flags, id, enabled, "CHARACTERS");
}

std::span<const std::uint8_t> GameState::character_flags_raw() const {
  return m_character_flags;
}

bool GameState::address_flag(const std::uint16_t address_id) const {
  return read_bit(m_address_flags, address_id, "ADDRESSES").value_or(false);
}

std::expected<void, std::string> GameState::set_address_flag(
    const std::uint16_t address_id, const bool enabled) {
  return write_bit(m_address_flags, address_id, enabled, "ADDRESSES");
}

std::span<const std::uint8_t> GameState::address_flags_raw() const {
  return m_address_flags;
}

std::expected<bool, std::string> GameState::zone_flag(const std::uint16_t id) const {
  return read_bit(m_zone_flags, static_cast<std::size_t>(id & 0x7FFFU), "ZONES");
}

std::expected<void, std::string> GameState::set_zone_flag(
    const std::uint16_t id, const bool enabled) {
  return write_bit(m_zone_flags, static_cast<std::size_t>(id & 0x7FFFU), enabled, "ZONES");
}

std::span<const std::uint8_t> GameState::zone_flags_raw() const {
  return m_zone_flags;
}

void GameState::establish_current_character(const Omikron::IamCharacterDefinition& definition) {
  if (m_current_character.has_value()) {
    m_character_profiles.insert_or_assign(
        m_current_character->character_id, CharacterValueState{m_current_character->values});
  }
  CurrentCharacterState promoted{definition};
  if (const auto profile{m_character_profiles.find(definition.character_id)};
      profile != m_character_profiles.end()) {
    promoted.values = profile->second.values();
  } else {
    m_character_profiles.try_emplace(definition.character_id, definition.values);
  }
  m_current_character = std::move(promoted);
}

void GameState::ensure_character_profile(
    const std::int16_t character_id, const Omikron::IamCharacterValueInitialState& initial_values) {
  if (m_current_character.has_value() && m_current_character->character_id == character_id) {
    return;
  }
  m_character_profiles.try_emplace(character_id, initial_values);
}

bool GameState::has_character_profile(const std::int16_t character_id) const {
  return (m_current_character.has_value() && m_current_character->character_id == character_id) ||
         m_character_profiles.contains(character_id);
}

std::expected<std::int32_t, std::string> GameState::character_value(
    const std::int16_t character_id, const std::int16_t kind) const {
  if (m_current_character.has_value() && m_current_character->character_id == character_id) {
    return CharacterValueState{m_current_character->values}.get(kind);
  }
  const auto profile{m_character_profiles.find(character_id)};
  if (profile == m_character_profiles.end()) {
    return std::expected<std::int32_t, std::string>{
        std::unexpect, fmt::format("character {} has no session value profile", character_id)};
  }
  return profile->second.get(kind);
}

std::expected<void, std::string> GameState::set_character_value(
    const std::int16_t character_id, const std::int16_t kind, const std::int32_t value) {
  if (m_current_character.has_value() && m_current_character->character_id == character_id) {
    CharacterValueState values{m_current_character->values};
    if (auto changed{values.set(kind, value)}; !changed) {
      return changed;
    }
    m_current_character->values = values.values();
    return {};
  }
  const auto profile{m_character_profiles.find(character_id)};
  if (profile == m_character_profiles.end()) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("character {} has no session value profile", character_id)};
  }
  return profile->second.set(kind, value);
}

std::expected<std::span<const std::int16_t>, std::string> GameState::persistent_object_collection(
    const std::uint16_t kind) const {
  if (kind >= m_object_collections.size()) {
    return std::expected<std::span<const std::int16_t>, std::string>{
        std::unexpect, fmt::format("persistent object collection kind {} is unsupported", kind)};
  }
  const PersistentObjectCollection& collection{m_object_collections.at(kind)};
  if (collection.object_ids.size() != collection.capacity) {
    return std::expected<std::span<const std::int16_t>, std::string>{std::unexpect,
        fmt::format("persistent object collection {} has {} slots but capacity {}",
            kind,
            collection.object_ids.size(),
            collection.capacity)};
  }
  return collection.object_ids;
}

std::expected<bool, std::string> GameState::add_object_to_collection(
    const std::uint16_t kind, const std::int16_t object_id) {
  if (kind >= m_object_collections.size()) {
    return std::expected<bool, std::string>{
        std::unexpect, fmt::format("persistent object collection kind {} is unsupported", kind)};
  }
  PersistentObjectCollection& collection{m_object_collections.at(kind)};
  if (collection.object_ids.size() != collection.capacity) {
    return std::expected<bool, std::string>{std::unexpect,
        fmt::format("persistent object collection {} has {} slots but capacity {}",
            kind,
            collection.object_ids.size(),
            collection.capacity)};
  }
  const auto first_empty{std::ranges::find(collection.object_ids, K_EMPTY_OBJECT_ID)};
  const std::size_t count{
      static_cast<std::size_t>(std::distance(collection.object_ids.begin(), first_empty))};
  if (std::ranges::find_if(first_empty, collection.object_ids.end(), [](const std::int16_t id) {
        return id != K_EMPTY_OBJECT_ID;
      }) != collection.object_ids.end()) {
    return std::expected<bool, std::string>{std::unexpect,
        fmt::format("persistent object collection {} has a non-contiguous empty slot", kind)};
  }
  if (first_empty == collection.object_ids.end()) {
    return false;
  }
  if (kind == 2U &&
      std::ranges::find(collection.object_ids.begin(), first_empty, object_id) != first_empty) {
    return false;
  }
  for (std::size_t index{count}; index > 0U; --index) {
    collection.object_ids.at(index) = collection.object_ids.at(index - 1U);
  }
  collection.object_ids.at(0) = object_id;
  return true;
}

}  // namespace App
