#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Omikron/IamCharacterDefinition.hpp"

namespace App::Omikron {
class IamStart;
}

namespace App {

/// Fixed-capacity persistent IAM object-ID collection. `object_ids` retains
/// all serialized slots, including `-1` empty entries, for save fidelity.
struct PersistentObjectCollection {
  std::size_t capacity{0};
  std::vector<std::int16_t> object_ids;
};

/// Session-owned mutable copy of the recovered numeric fields in one authored
/// character definition. The numeric kind remains the public Runtime ABI.
class CharacterValueState {
 public:
  explicit CharacterValueState(const Omikron::IamCharacterValueInitialState& initial_values);

  [[nodiscard]] std::expected<std::int32_t, std::string> get(std::int16_t kind) const;
  [[nodiscard]] std::expected<void, std::string> set(std::int16_t kind, std::int32_t value);

 private:
  Omikron::IamCharacterValueInitialState m_values;
};

/// Mutable, session-owned state initialized from immutable IAM/START data.
/// It deliberately has no dependency on transient AREA/SCENE contexts.
class GameState {
 public:
  /// Creates a mutable session copy of recovered persistent START regions.
  [[nodiscard]] static std::expected<GameState, std::string> from_start(
      const Omikron::IamStart& start);

  /// Reads one persistent ADDRESS bit. Out-of-range IDs are false; mutation
  /// is the checked operation used by compact VM handlers.
  [[nodiscard]] bool address_flag(std::uint16_t address_id) const;

  /// Sets or clears one persistent ADDRESS bit.
  [[nodiscard]] std::expected<void, std::string> set_address_flag(
      std::uint16_t address_id, bool enabled);

  /// Immutable raw ADDRESS bytes for inspection or later serialization.
  [[nodiscard]] std::span<const std::uint8_t> address_flags_raw() const;

  /// Reads one signed START global variable by authored numeric ID.
  [[nodiscard]] std::expected<std::int32_t, std::string> global_variable(
      std::uint16_t id) const;

  /// Writes one existing START global variable without growing the store.
  [[nodiscard]] std::expected<void, std::string> set_global_variable(
      std::uint16_t id, std::int32_t value);

  /// All session global variables in authored START order.
  [[nodiscard]] std::span<const std::int32_t> global_variables() const;

  /// Lazily creates one mutable profile from its immutable IAM definition.
  void ensure_character_profile(std::int16_t character_id,
      const Omikron::IamCharacterValueInitialState& initial_values);

  [[nodiscard]] bool has_character_profile(std::int16_t character_id) const;

  [[nodiscard]] std::expected<std::int32_t, std::string> character_value(
      std::int16_t character_id, std::int16_t kind) const;

  [[nodiscard]] std::expected<void, std::string> set_character_value(
      std::int16_t character_id, std::int16_t kind, std::int32_t value);

  /// Full fixed-capacity object-ID slots for a supported persistent kind.
  [[nodiscard]] std::expected<std::span<const std::int16_t>, std::string>
  persistent_object_collection(std::uint16_t kind) const;

  /// Inserts an object ID at the front. Kinds 0/1 allow duplicates; kind 2
  /// suppresses an existing ID. Returns false for a full or suppressed add.
  [[nodiscard]] std::expected<bool, std::string> add_object_to_collection(
      std::uint16_t kind, std::int16_t object_id);

 private:
  std::vector<std::int32_t> m_global_variables;
  std::vector<std::uint8_t> m_address_flags;
  std::array<PersistentObjectCollection, 3> m_object_collections;
  std::unordered_map<std::int16_t, CharacterValueState> m_character_profiles;
};

}  // namespace App
