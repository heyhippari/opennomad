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
#include <vector>

#include "Core/Omikron/IamCharacterDefinition.hpp"
#include "Core/Omikron/IamStart.hpp"

namespace App {

namespace {

constexpr std::size_t K_BITS_PER_BYTE{8};
constexpr std::int16_t K_EMPTY_OBJECT_ID{-1};
constexpr std::int32_t K_SMALL_CHARACTER_VALUE_MAX{200};
constexpr std::int32_t K_UNSIGNED_CHARACTER_VALUE_MAX{65'535};

std::int16_t read_i16(const std::span<const std::byte> data, const std::size_t offset) {
  std::int16_t value{0};
  std::memcpy(&value, data.subspan(offset, sizeof(value)).data(), sizeof(value));
  return value;
}

std::int32_t read_i32(const std::span<const std::byte> data, const std::size_t offset) {
  std::int32_t value{0};
  std::memcpy(&value, data.subspan(offset, sizeof(value)).data(), sizeof(value));
  return value;
}

std::expected<std::int32_t, std::string> unsupported_character_value_kind(
    const std::int16_t kind) {
  return std::expected<std::int32_t, std::string>{std::unexpect,
      fmt::format("character value kind {} is not implemented", kind)};
}

}  // namespace

CharacterValueState::CharacterValueState(
    const Omikron::IamCharacterValueInitialState& initial_values)
    : m_values(initial_values) {}

std::expected<std::int32_t, std::string> CharacterValueState::get(const std::int16_t kind) const {
  switch (kind) {
    case 1:
      return m_values.field_aa;
    case 2:
      return m_values.field_9c;
    case 3:
      return m_values.field_9e;
    case 4:
      return m_values.field_ac;
    case 5:
      return m_values.field_ae;
    case 16:
      return m_values.field_a0;
    case 17:
      return m_values.field_a2;
    case 18:
      return m_values.field_a4;
    case 19:
      return m_values.field_a6;
    case 20:
      return m_values.field_a8;
    default:
      return unsupported_character_value_kind(kind);
  }
}

std::expected<void, std::string> CharacterValueState::set(
    const std::int16_t kind, const std::int32_t value) {
  const auto clamped_small{static_cast<std::int16_t>(std::min(value, K_SMALL_CHARACTER_VALUE_MAX))};
  switch (kind) {
    case 1:
      m_values.field_aa = clamped_small;
      break;
    case 2:
      m_values.field_9c = clamped_small;
      break;
    case 3:
      m_values.field_9e = clamped_small;
      break;
    case 4:
      m_values.field_ac =
          static_cast<std::uint16_t>(std::min(value, K_UNSIGNED_CHARACTER_VALUE_MAX));
      break;
    case 5:
      m_values.field_ae = static_cast<std::int16_t>(value);
      break;
    case 16:
      m_values.field_a0 = clamped_small;
      break;
    case 17:
      m_values.field_a2 = clamped_small;
      break;
    case 18:
      m_values.field_a4 = clamped_small;
      break;
    case 19:
      m_values.field_a6 = clamped_small;
      break;
    case 20:
      m_values.field_a8 = clamped_small;
      break;
    default:
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("character value kind {} is not implemented", kind)};
  }
  return {};
}

std::expected<GameState, std::string> GameState::from_start(const Omikron::IamStart& start) {
  auto serialized_global_variables{start.global_variables()};
  if (!serialized_global_variables) {
    return std::expected<GameState, std::string>{std::unexpect,
        fmt::format("cannot initialize global variables: {}", serialized_global_variables.error())};
  }

  auto serialized_address_flags{start.address_flags()};
  if (!serialized_address_flags) {
    return std::expected<GameState, std::string>{std::unexpect,
        fmt::format("cannot initialize persistent ADDRESS flags: {}", serialized_address_flags.error())};
  }

  GameState state;
  const std::size_t global_count{serialized_global_variables->size() / sizeof(std::int32_t)};
  state.m_global_variables.reserve(global_count);
  for (std::size_t index{0}; index < global_count; ++index) {
    state.m_global_variables.push_back(
        read_i32(serialized_global_variables.value(), index * sizeof(std::int32_t)));
  }

  state.m_address_flags.reserve(serialized_address_flags->size());
  for (const std::byte value : serialized_address_flags.value()) {
    state.m_address_flags.push_back(std::to_integer<std::uint8_t>(value));
  }

  for (std::size_t kind{0}; kind < state.m_object_collections.size(); ++kind) {
    auto serialized_collection{start.persistent_object_collection(static_cast<std::uint16_t>(kind))};
    if (!serialized_collection) {
      return std::expected<GameState, std::string>{std::unexpect,
          fmt::format("cannot initialize persistent object collection {}: {}",
              kind,
              serialized_collection.error())};
    }

    PersistentObjectCollection& collection{state.m_object_collections.at(kind)};
    collection.capacity = serialized_collection->size() / sizeof(std::int16_t);
    collection.object_ids.reserve(collection.capacity);
    for (std::size_t index{0}; index < collection.capacity; ++index) {
      collection.object_ids.push_back(
          read_i16(serialized_collection.value(), index * sizeof(std::int16_t)));
    }
  }

  return state;
}

bool GameState::address_flag(const std::uint16_t address_id) const {
  const std::size_t byte_index{static_cast<std::size_t>(address_id) / K_BITS_PER_BYTE};
  if (byte_index >= m_address_flags.size()) {
    return false;
  }
  const std::size_t bit_index{static_cast<std::size_t>(address_id) % K_BITS_PER_BYTE};
  const std::uint8_t mask{static_cast<std::uint8_t>(1U << bit_index)};
  return (m_address_flags.at(byte_index) & mask) != 0U;
}

std::expected<void, std::string> GameState::set_address_flag(
    const std::uint16_t address_id, const bool enabled) {
  const std::size_t byte_index{static_cast<std::size_t>(address_id) / K_BITS_PER_BYTE};
  if (byte_index >= m_address_flags.size()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("ADDRESSES ID {} is outside the {}-bit persistent region",
            address_id,
            m_address_flags.size() * K_BITS_PER_BYTE)};
  }
  const std::size_t bit_index{static_cast<std::size_t>(address_id) % K_BITS_PER_BYTE};
  const std::uint8_t mask{static_cast<std::uint8_t>(1U << bit_index)};
  std::uint8_t& byte{m_address_flags.at(byte_index)};
  if (enabled) {
    byte = static_cast<std::uint8_t>(byte | mask);
  } else {
    byte = static_cast<std::uint8_t>(byte & static_cast<std::uint8_t>(~mask));
  }
  return {};
}

std::span<const std::uint8_t> GameState::address_flags_raw() const {
  return m_address_flags;
}

std::expected<std::int32_t, std::string> GameState::global_variable(
    const std::uint16_t id) const {
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

void GameState::ensure_character_profile(
    const std::int16_t character_id,
    const Omikron::IamCharacterValueInitialState& initial_values) {
  m_character_profiles.try_emplace(character_id, initial_values);
}

bool GameState::has_character_profile(const std::int16_t character_id) const {
  return m_character_profiles.contains(character_id);
}

std::expected<std::int32_t, std::string> GameState::character_value(
    const std::int16_t character_id, const std::int16_t kind) const {
  const auto profile{m_character_profiles.find(character_id)};
  if (profile == m_character_profiles.end()) {
    return std::expected<std::int32_t, std::string>{std::unexpect,
        fmt::format("character {} has no session value profile", character_id)};
  }
  return profile->second.get(kind);
}

std::expected<void, std::string> GameState::set_character_value(
    const std::int16_t character_id, const std::int16_t kind, const std::int32_t value) {
  const auto profile{m_character_profiles.find(character_id)};
  if (profile == m_character_profiles.end()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("character {} has no session value profile", character_id)};
  }
  return profile->second.set(kind, value);
}

std::expected<std::span<const std::int16_t>, std::string> GameState::persistent_object_collection(
    const std::uint16_t kind) const {
  if (kind >= m_object_collections.size()) {
    return std::expected<std::span<const std::int16_t>, std::string>{std::unexpect,
        fmt::format("persistent object collection kind {} is unsupported", kind)};
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
    return std::expected<bool, std::string>{std::unexpect,
        fmt::format("persistent object collection kind {} is unsupported", kind)};
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
  if (std::ranges::find_if(first_empty,
          collection.object_ids.end(),
          [](const std::int16_t id) { return id != K_EMPTY_OBJECT_ID; }) !=
      collection.object_ids.end()) {
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
