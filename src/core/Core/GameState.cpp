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

#include "Core/Omikron/IamStart.hpp"

namespace App {

namespace {

constexpr std::size_t K_BITS_PER_BYTE{8};
constexpr std::int16_t K_EMPTY_OBJECT_ID{-1};

std::int16_t read_i16(const std::span<const std::byte> data, const std::size_t offset) {
  std::int16_t value{0};
  std::memcpy(&value, data.subspan(offset, sizeof(value)).data(), sizeof(value));
  return value;
}

}  // namespace

std::expected<GameState, std::string> GameState::from_start(const Omikron::IamStart& start) {
  auto serialized_address_flags{start.address_flags()};
  if (!serialized_address_flags) {
    return std::expected<GameState, std::string>{std::unexpect,
        fmt::format("cannot initialize persistent ADDRESS flags: {}", serialized_address_flags.error())};
  }

  GameState state;
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
