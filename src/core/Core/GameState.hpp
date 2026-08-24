#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
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

/// Session-owned mutable numeric profile for one authored character.
class CharacterValueState {
 public:
  explicit CharacterValueState(const Omikron::IamCharacterValueInitialState& initial_values);

  [[nodiscard]] std::expected<std::int32_t, std::string> get(std::int16_t kind) const;
  [[nodiscard]] std::expected<void, std::string> set(std::int16_t kind, std::int32_t value);
  [[nodiscard]] const Omikron::IamCharacterValueInitialState& values() const {
    return m_values;
  }

 private:
  Omikron::IamCharacterValueInitialState m_values;
};

using CurrentCharacterState = Omikron::IamCharacterDefinition;

/// Canonical mutable semantic state copied from immutable IAM/START data.
/// Transient AREA/SCENE contexts and live runtime entities remain elsewhere.
class GameState {
 public:
  [[nodiscard]] static std::expected<GameState, std::string> from_start(
      const Omikron::IamStart& start);

  [[nodiscard]] std::uint32_t format_revision() const {
    return m_format_revision;
  }
  [[nodiscard]] std::uint32_t build_date() const {
    return m_build_date;
  }
  [[nodiscard]] const std::array<std::byte, 0x0C>& opaque_header_state() const {
    return m_opaque_header_state;
  }
  [[nodiscard]] const std::array<std::int32_t, 3>& saved_position() const {
    return m_saved_position;
  }
  [[nodiscard]] std::int32_t saved_orientation() const {
    return m_saved_orientation;
  }
  [[nodiscard]] const std::array<std::byte, 2>& opaque_area_state() const {
    return m_opaque_area_state;
  }

  [[nodiscard]] std::int16_t current_area() const {
    return m_current_area;
  }
  [[nodiscard]] std::int16_t linked_area() const {
    return m_linked_area;
  }
  void set_current_area(std::int16_t area_id) {
    m_current_area = area_id;
  }
  void set_linked_area(std::int16_t area_id) {
    m_linked_area = area_id;
  }

  [[nodiscard]] std::expected<std::int32_t, std::string> global_variable(std::uint16_t id) const;
  [[nodiscard]] std::expected<void, std::string> set_global_variable(
      std::uint16_t id, std::int32_t value);
  [[nodiscard]] std::span<const std::int32_t> global_variables() const;

  [[nodiscard]] std::expected<std::int16_t, std::string> area_mapping(std::int32_t area_id) const;
  [[nodiscard]] std::expected<void, std::string> set_area_mapping(
      std::int32_t area_id, std::int16_t value);
  [[nodiscard]] std::span<const std::int16_t> area_mappings() const;

  [[nodiscard]] std::expected<std::uint8_t, std::string> packed_state(std::size_t index) const;
  [[nodiscard]] std::expected<void, std::string> set_packed_state(
      std::size_t index, std::uint8_t value);
  [[nodiscard]] std::span<const std::uint8_t> packed_state_raw() const;

  [[nodiscard]] std::expected<bool, std::string> character_flag(std::uint16_t id) const;
  [[nodiscard]] std::expected<void, std::string> set_character_flag(std::uint16_t id, bool enabled);
  [[nodiscard]] std::span<const std::uint8_t> character_flags_raw() const;

  /// Existing compatibility read: out-of-range ADDRESS IDs remain false.
  [[nodiscard]] bool address_flag(std::uint16_t address_id) const;
  [[nodiscard]] std::expected<void, std::string> set_address_flag(
      std::uint16_t address_id, bool enabled);
  [[nodiscard]] std::span<const std::uint8_t> address_flags_raw() const;

  [[nodiscard]] std::expected<bool, std::string> zone_flag(std::uint16_t id) const;
  [[nodiscard]] std::expected<void, std::string> set_zone_flag(std::uint16_t id, bool enabled);
  [[nodiscard]] std::span<const std::uint8_t> zone_flags_raw() const;

  [[nodiscard]] const std::optional<CurrentCharacterState>& current_character() const {
    return m_current_character;
  }
  /// Promotes an authored definition into persistent current-character state,
  /// preserving an existing mutable profile for the same character.
  void establish_current_character(const Omikron::IamCharacterDefinition& definition);

  void ensure_character_profile(
      std::int16_t character_id, const Omikron::IamCharacterValueInitialState& initial_values);
  [[nodiscard]] bool has_character_profile(std::int16_t character_id) const;
  [[nodiscard]] std::expected<std::int32_t, std::string> character_value(
      std::int16_t character_id, std::int16_t kind) const;
  [[nodiscard]] std::expected<void, std::string> set_character_value(
      std::int16_t character_id, std::int16_t kind, std::int32_t value);

  [[nodiscard]] std::expected<std::span<const std::int16_t>, std::string>
  persistent_object_collection(std::uint16_t kind) const;
  [[nodiscard]] std::expected<bool, std::string> add_object_to_collection(
      std::uint16_t kind, std::int16_t object_id);

 private:
  [[nodiscard]] static std::expected<bool, std::string> read_bit(
      std::span<const std::uint8_t> bytes, std::size_t id, const std::string& label);
  [[nodiscard]] static std::expected<void, std::string> write_bit(
      std::span<std::uint8_t> bytes, std::size_t id, bool enabled, const std::string& label);

  std::uint32_t m_format_revision{0};
  std::uint32_t m_build_date{0};
  std::array<std::byte, 0x0C> m_opaque_header_state{};
  std::array<std::int32_t, 3> m_saved_position{};
  std::int32_t m_saved_orientation{0};
  std::int16_t m_current_area{0};
  std::int16_t m_linked_area{0};
  std::array<std::byte, 2> m_opaque_area_state{};

  std::vector<std::int32_t> m_global_variables;
  std::vector<std::int16_t> m_area_mappings;
  std::vector<std::uint8_t> m_packed_state;
  std::vector<std::uint8_t> m_character_flags;
  std::vector<std::uint8_t> m_address_flags;
  std::vector<std::uint8_t> m_zone_flags;
  std::array<PersistentObjectCollection, 3> m_object_collections;
  std::optional<CurrentCharacterState> m_current_character;
  std::unordered_map<std::int16_t, CharacterValueState> m_character_profiles;
};

}  // namespace App
