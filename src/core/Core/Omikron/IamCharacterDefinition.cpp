#include "Core/Omikron/IamCharacterDefinition.hpp"

#include <fmt/format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace App::Omikron {

namespace {

template <typename Value>
Value read_at(const std::span<const std::byte> data, const std::size_t offset) {
  Value value{};
  std::memcpy(&value, data.subspan(offset, sizeof(Value)).data(), sizeof(value));
  return value;
}

std::string fixed_string(const std::span<const std::byte> data) {
  const void* raw{data.data()};
  const char* begin{static_cast<const char*>(raw)};
  const void* nul{std::memchr(raw, '\0', data.size())};
  const std::size_t size{nul == nullptr
                             ? data.size()
                             : static_cast<std::size_t>(static_cast<const char*>(nul) - begin)};
  return std::string{begin, size};
}

template <std::size_t Size>
std::array<std::int16_t, Size> read_i16_array(
    const std::span<const std::byte> record, const std::size_t offset) {
  std::array<std::int16_t, Size> result{};
  for (std::size_t index{0}; index < Size; ++index) {
    result.at(index) = read_at<std::int16_t>(record, offset + (index * sizeof(std::int16_t)));
  }
  return result;
}

}  // namespace

std::expected<IamCharacterDefinition, std::string> parse_iam_character_definition(
    const std::span<const std::byte> record,
    std::optional<std::string> signs,
    std::optional<std::string> interests) {
  constexpr std::size_t k_serialized_size{0x114};
  if (record.size() != k_serialized_size) {
    return std::expected<IamCharacterDefinition, std::string>{std::unexpect,
        fmt::format("IAM character definition: expected {:#x} bytes, got {:#x}",
            k_serialized_size,
            record.size())};
  }

  const std::uint32_t character_type_value{read_at<std::uint32_t>(record, 0x0B0U)};
  constexpr std::uint32_t k_last_character_type{
      static_cast<std::uint32_t>(CharacterType::Astaroth)};
  if (character_type_value > k_last_character_type) {
    return std::expected<IamCharacterDefinition, std::string>{std::unexpect,
        fmt::format("IAM character definition: character type {} is outside [0, {}]",
            character_type_value,
            k_last_character_type)};
  }

  return IamCharacterDefinition{.signs = std::move(signs),
      .interests = std::move(interests),
      .name = fixed_string(record.subspan(0x008U, 32U)),
      .job = fixed_string(record.subspan(0x028U, 32U)),
      .adventure_control_set = fixed_string(record.subspan(0x048U, 18U)),
      .combat_control_set = fixed_string(record.subspan(0x05AU, 18U)),
      .sex = fixed_string(record.subspan(0x06CU, 8U)),
      .eyes = fixed_string(record.subspan(0x074U, 8U)),
      .blood_type = fixed_string(record.subspan(0x07CU, 4U)),
      .height = fixed_string(record.subspan(0x080U, 8U)),
      .weight = fixed_string(record.subspan(0x088U, 8U)),
      .model_resource = fixed_string(record.subspan(0x090U, 10U)),
      .age = read_at<std::int16_t>(record, 0x09AU),
      .values = {.mana = read_at<std::int16_t>(record, 0x09CU),
          .speed = read_at<std::int16_t>(record, 0x09EU),
          .attack = read_at<std::int16_t>(record, 0x0A0U),
          .body_resistance = read_at<std::int16_t>(record, 0x0A2U),
          .dodge = read_at<std::int16_t>(record, 0x0A4U),
          .fight_experience = read_at<std::int16_t>(record, 0x0A6U),
          .unknown_characteristic_a8 = read_at<std::int16_t>(record, 0x0A8U),
          .energy = read_at<std::int16_t>(record, 0x0AAU),
          .seteks = read_at<std::uint16_t>(record, 0x0ACU),
          .rings = read_at<std::int16_t>(record, 0x0AEU)},
      .character_type = static_cast<CharacterType>(character_type_value),
      .behavior_parameters = read_i16_array<11>(record, 0x0B4U),
      .tuple_a = read_i16_array<4>(record, 0x0CAU),
      .tuple_b = read_i16_array<4>(record, 0x0D2U),
      .tuple_c = read_i16_array<4>(record, 0x0DAU),
      .lookup_keys = read_i16_array<4>(record, 0x0E2U),
      .lookup_values_a = read_i16_array<4>(record, 0x0EAU),
      .lookup_values_b = read_i16_array<4>(record, 0x0F2U),
      .weapon_family_parameters = read_i16_array<5>(record, 0x0FAU),
      .ammunition = read_i16_array<5>(record, 0x104U),
      .linked_object_id = read_at<std::int16_t>(record, 0x10EU),
      .character_id = read_at<std::int16_t>(record, 0x110U),
      .unknown_112 = read_at<std::int16_t>(record, 0x112U)};
}

}  // namespace App::Omikron
