#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

namespace App::Omikron {

/// Recovered authored character classification stored at definition +0xB0.
enum class CharacterType : std::uint32_t {
  /// Retail-authored default/sentinel. Its original Runtime source name is unknown.
  Unspecified = 0xFFFFFFFFU,

  None = 0,
  MalePasser = 1,
  FemalePasser = 2,
  MaleEnemy = 3,
  FemaleEnemy = 4,
  Mecagarde = 5,
  Mecadog = 6,
  XTech = 7,
  ZTech = 8,
  Incarnable = 9,
  Gandhar = 10,
  Zombie = 11,
  Spectre = 12,
  Astaroth = 13,
};

/// Mutable numeric subset addressed by compact CHARACTER VALUE kinds.
struct IamCharacterValueInitialState {
  std::int16_t mana{0};
  std::int16_t speed{0};
  std::int16_t attack{0};
  std::int16_t body_resistance{0};
  std::int16_t dodge{0};
  std::int16_t fight_experience{0};
  std::int16_t unknown_characteristic_a8{0};
  std::int16_t energy{0};
  std::uint16_t seteks{0};
  std::int16_t rings{0};
};

/// Complete semantic representation of one recovered 0x114-byte IAM
/// character definition. Serialized pointer/offset fields are resolved into
/// owned optional strings and never escape this decoder.
struct IamCharacterDefinition {
  std::optional<std::string> signs;
  std::optional<std::string> interests;

  std::string name;
  std::string job;
  std::string adventure_control_set;
  std::string combat_control_set;
  std::string sex;
  std::string eyes;
  std::string blood_type;
  std::string height;
  std::string weight;
  std::string model_resource;

  std::int16_t age{0};
  IamCharacterValueInitialState values;
  CharacterType character_type{CharacterType::None};
  std::array<std::int16_t, 11> behavior_parameters{};
  std::array<std::int16_t, 4> tuple_a{};
  std::array<std::int16_t, 4> tuple_b{};
  std::array<std::int16_t, 4> tuple_c{};
  std::array<std::int16_t, 4> lookup_keys{};
  std::array<std::int16_t, 4> lookup_values_a{};
  std::array<std::int16_t, 4> lookup_values_b{};
  std::array<std::int16_t, 5> weapon_family_parameters{};
  std::array<std::int16_t, 5> ammunition{};
  std::int16_t linked_object_id{0};
  std::int16_t character_id{0};
  std::int16_t unknown_112{0};
};

/// Decodes the fixed portion of one character definition after its first two
/// representation fields have been resolved by the owning IAM record.
[[nodiscard]] std::expected<IamCharacterDefinition, std::string> parse_iam_character_definition(
    std::span<const std::byte> record,
    std::optional<std::string> signs,
    std::optional<std::string> interests);

}  // namespace App::Omikron
