#include "Core/Omikron/IamArea.hpp"

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
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Omikron/IamCamera.hpp"
#include "Core/Omikron/IamCharacterDefinition.hpp"
#include "Core/Omikron/IamZone.hpp"

namespace App::Omikron {

namespace {

std::int16_t read_i16_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::int16_t value{0};
  std::memcpy(&value, data.subspan(offset, 2U).data(), sizeof(value));
  return value;
}

std::uint16_t read_u16_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::uint16_t value{0};
  std::memcpy(&value, data.subspan(offset, 2U).data(), sizeof(value));
  return value;
}

std::int32_t read_i32_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::int32_t value{0};
  std::memcpy(&value, data.subspan(offset, 4U).data(), sizeof(value));
  return value;
}

std::uint32_t read_u32_at(const std::span<const std::byte> data, const std::size_t offset) {
  std::uint32_t value{0};
  std::memcpy(&value, data.subspan(offset, 4U).data(), sizeof(value));
  return value;
}

/// Extracts a fixed-width NUL-padded field, trimming at the first NUL.
std::string fixed_string(const std::span<const std::byte> bytes) {
  const void* raw{bytes.data()};
  const char* begin{static_cast<const char*>(raw)};
  const void* nul{std::memchr(raw, '\0', bytes.size())};
  const std::size_t length{nul == nullptr
                               ? bytes.size()
                               : static_cast<std::size_t>(static_cast<const char*>(nul) - begin)};
  return std::string{begin, length};
}

}  // namespace

std::optional<std::size_t> IamAreaRecord::known_table_stride(const std::size_t index) {
  switch (index) {
    case 0:
      return 0x14;
    case 1:
      return 0x18;
    case 2:
      return 0x44;
    case 4:
      return 0x114;
    case 5:
      return 0x10;
    case 6:
      return 0x2C;
    case 7:
      return 0x08;
    default:
      return std::nullopt;  // Table 3 (and out-of-range): unresolved.
  }
}

std::expected<IamAreaRecord, std::string> IamAreaRecord::load(
    const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  if (data.size() < k_header_size) {
    return std::expected<IamAreaRecord, std::string>{std::unexpect,
        fmt::format("IAM/AREA record: too small ({} bytes, expected at least {:#x})",
            data.size(),
            k_header_size)};
  }

  const std::uint32_t script_offset{read_u32_at(data, k_offset_script)};
  if (script_offset > data.size()) {
    return std::expected<IamAreaRecord, std::string>{std::unexpect,
        fmt::format("IAM/AREA record: script offset {:#x} is outside the {:#x}-byte record",
            script_offset,
            data.size())};
  }

  // Validate every table span whose stride is known; unresolved tables are
  // exposed as checked raw-byte views instead (see table_view).
  for (std::size_t index{0}; index < k_table_count; ++index) {
    const std::optional<std::size_t> stride{known_table_stride(index)};
    if (!stride) {
      continue;
    }
    const std::uint32_t offset{read_u32_at(data, k_offset_table_offsets + (index * 4U))};
    const std::uint16_t count{read_u16_at(data, k_offset_table_counts + (index * 2U))};
    const std::uint64_t span_end{
        static_cast<std::uint64_t>(offset) + (static_cast<std::uint64_t>(count) * (*stride))};
    if (offset > data.size() || span_end > data.size()) {
      return std::expected<IamAreaRecord, std::string>{std::unexpect,
          fmt::format("IAM/AREA record: table {} span [{:#x}, {:#x}) exceeds the {:#x}-byte "
                      "record",
              index,
              offset,
              span_end,
              data.size())};
    }
  }

  constexpr std::size_t k_zone_table_index{2};
  constexpr std::size_t k_zone_stride{0x44};
  const std::uint32_t zone_offset{
      read_u32_at(data, k_offset_table_offsets + (k_zone_table_index * 4U))};
  const std::uint16_t zone_count{
      read_u16_at(data, k_offset_table_counts + (k_zone_table_index * 2U))};
  for (std::size_t index{0}; index < zone_count; ++index) {
    const std::span<const std::byte> zone{
        data.subspan(zone_offset + (index * k_zone_stride), k_zone_stride)};
    for (std::size_t event{0}; event < 3U; ++event) {
      const std::uint32_t event_offset{read_u32_at(zone, event * sizeof(std::uint32_t))};
      if (event_offset != 0U && event_offset >= data.size()) {
        return std::expected<IamAreaRecord, std::string>{std::unexpect,
            fmt::format("IAM/AREA record: zone {} event {} offset {:#x} is outside record",
                index,
                event + 1U,
                event_offset)};
      }
    }
  }

  constexpr std::size_t k_definition_table_index{4};
  constexpr std::size_t k_definition_stride{0x114};
  const std::uint32_t definition_offset{
      read_u32_at(data, k_offset_table_offsets + (k_definition_table_index * 4U))};
  const std::uint16_t definition_count{
      read_u16_at(data, k_offset_table_counts + (k_definition_table_index * 2U))};
  for (std::size_t index{0}; index < definition_count; ++index) {
    const std::span<const std::byte> definition{
        data.subspan(definition_offset + (index * k_definition_stride), k_definition_stride)};
    std::array<std::optional<std::string>, 2> strings{};
    for (std::size_t field{0}; field < strings.size(); ++field) {
      const std::uint32_t string_offset{read_u32_at(definition, field * sizeof(std::uint32_t))};
      if (string_offset == 0U) {
        continue;
      }
      if (string_offset >= data.size()) {
        return std::expected<IamAreaRecord, std::string>{std::unexpect,
            fmt::format(
                "IAM/AREA record: table-4 string at {:#x} is outside record", string_offset)};
      }
      const std::span<const std::byte> suffix{data.subspan(string_offset)};
      if (std::memchr(suffix.data(), '\0', suffix.size()) == nullptr) {
        return std::expected<IamAreaRecord, std::string>{std::unexpect,
            fmt::format("IAM/AREA record: table-4 string at {:#x} is not NUL terminated in record",
                string_offset)};
      }
      strings.at(field) = fixed_string(suffix);
    }
    auto parsed{parse_iam_character_definition(
        definition, std::move(strings.at(0)), std::move(strings.at(1)))};
    if (!parsed) {
      return std::expected<IamAreaRecord, std::string>{std::unexpect,
          fmt::format("IAM/AREA record: table-4 character {}: {}", index, parsed.error())};
    }
  }

  return IamAreaRecord{std::vector<std::byte>{data.begin(), data.end()}};
}

std::uint32_t IamAreaRecord::runtime_context() const {
  return read_u32_at(m_bytes, k_offset_runtime_context);
}

std::uint32_t IamAreaRecord::script_offset() const {
  return read_u32_at(m_bytes, k_offset_script);
}

std::string IamAreaRecord::name_at(const std::size_t offset) const {
  return fixed_string(std::span<const std::byte>{m_bytes}.subspan(offset, k_name_field_size));
}

std::string IamAreaRecord::model3do_name() const {
  return name_at(k_offset_model3do_name);
}
std::string IamAreaRecord::scenario_scx_name() const {
  return name_at(k_offset_scenario_scx_name);
}
std::string IamAreaRecord::map_mpt_name() const {
  return name_at(k_offset_map_mpt_name);
}
std::string IamAreaRecord::options_opt_name() const {
  return name_at(k_offset_options_opt_name);
}
std::string IamAreaRecord::animation_ani_name() const {
  return name_at(k_offset_animation_ani_name);
}
std::string IamAreaRecord::sky_3do_name() const {
  return name_at(k_offset_sky_3do_name);
}

std::span<const std::byte> IamAreaRecord::script_bytes() const {
  return std::span<const std::byte>{m_bytes}.subspan(script_offset());
}

std::uint32_t IamAreaRecord::table_offset(const std::size_t index) const {
  if (index >= k_table_count) {
    return 0;
  }
  return read_u32_at(m_bytes, k_offset_table_offsets + (index * 4U));
}

std::uint16_t IamAreaRecord::table_count(const std::size_t index) const {
  if (index >= k_table_count) {
    return 0;
  }
  return read_u16_at(m_bytes, k_offset_table_counts + (index * 2U));
}

std::expected<std::span<const std::byte>, std::string> IamAreaRecord::table_view(
    const std::size_t index) const {
  if (index >= k_table_count) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format(
            "IAM/AREA record: table index {} is out of range ({} tables)", index, k_table_count)};
  }

  const std::uint32_t offset{table_offset(index)};
  const std::uint16_t count{table_count(index)};

  if (count == 0U) {
    if (offset > m_bytes.size()) {
      return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
          fmt::format(
              "IAM/AREA record: empty table {} offset {:#x} is outside the record", index, offset)};
    }
    return std::span<const std::byte>{m_bytes}.subspan(offset, 0U);
  }

  const std::optional<std::size_t> stride{known_table_stride(index)};
  if (!stride) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format(
            "IAM/AREA record: table {} has {} entries but its stride is unresolved", index, count)};
  }

  const std::uint64_t span_end{
      static_cast<std::uint64_t>(offset) + (static_cast<std::uint64_t>(count) * (*stride))};
  if (offset > m_bytes.size() || span_end > m_bytes.size()) {
    return std::expected<std::span<const std::byte>, std::string>{std::unexpect,
        fmt::format("IAM/AREA record: table {} span [{:#x}, {:#x}) exceeds the record",
            index,
            offset,
            span_end)};
  }
  return std::span<const std::byte>{m_bytes}.subspan(offset, count * (*stride));
}

std::optional<IamAreaCharacterRecord> IamAreaRecord::character_by_id(
    const std::int16_t character_id) const {
  constexpr std::size_t k_character_table_index{0};
  constexpr std::size_t k_character_stride{0x14};

  auto table{table_view(k_character_table_index)};
  if (!table) {
    return std::nullopt;
  }

  for (std::size_t index{0}; index < table_count(k_character_table_index); ++index) {
    const std::span<const std::byte> record{
        table->subspan(index * k_character_stride, k_character_stride)};

    IamAreaCharacterRecord character;
    character.field_00 = read_i16_at(record, 0x00U);
    character.character_id = read_i16_at(record, 0x02U);
    character.serialized_position.at(0) = read_i32_at(record, 0x04U);
    character.serialized_position.at(1) = read_i32_at(record, 0x08U);
    character.serialized_position.at(2) = read_i32_at(record, 0x0CU);
    character.orientation_units = read_i16_at(record, 0x10U);
    character.state_bit_index = read_u16_at(record, 0x12U);

    if (character.character_id == character_id) {
      return character;
    }
  }

  return std::nullopt;
}

std::optional<IamAreaCharacterDefinitionRecord> IamAreaRecord::character_definition_by_character_id(
    const std::int16_t character_id) const {
  constexpr std::size_t k_definition_table_index{4};
  constexpr std::size_t k_definition_stride{0x114};

  auto table{table_view(k_definition_table_index)};
  if (!table) {
    return std::nullopt;
  }

  for (std::size_t index{0}; index < table_count(k_definition_table_index); ++index) {
    const std::span<const std::byte> record{
        table->subspan(index * k_definition_stride, k_definition_stride)};
    if (read_i16_at(record, 0x110U) != character_id) {
      continue;
    }
    std::array<std::optional<std::string>, 2> strings{};
    for (std::size_t field{0}; field < strings.size(); ++field) {
      const std::uint32_t string_offset{read_u32_at(record, field * sizeof(std::uint32_t))};
      if (string_offset != 0U) {
        strings.at(field) =
            fixed_string(std::span<const std::byte>{m_bytes}.subspan(string_offset));
      }
    }
    auto definition{
        parse_iam_character_definition(record, std::move(strings.at(0)), std::move(strings.at(1)))};
    return definition ? std::optional<IamAreaCharacterDefinitionRecord>{std::move(*definition)}
                      : std::nullopt;
  }
  return std::nullopt;
}

std::optional<IamAreaCameraRecord> IamAreaRecord::camera_by_id(const std::int16_t camera_id) const {
  constexpr std::size_t k_camera_table_index{6};
  constexpr std::size_t k_camera_stride{0x2C};

  auto table{table_view(k_camera_table_index)};
  if (!table) {
    return std::nullopt;
  }

  for (std::size_t index{0}; index < table_count(k_camera_table_index); ++index) {
    const std::span<const std::byte> record{
        table->subspan(index * k_camera_stride, k_camera_stride)};
    const auto camera{parse_iam_camera(record)};
    if (!camera) {
      return std::nullopt;
    }
    if (camera->camera_id == camera_id) {
      return camera.value();
    }
  }
  return std::nullopt;
}

std::optional<IamAreaAddressRecord> IamAreaRecord::address_by_id(
    const std::int16_t address_id) const {
  constexpr std::size_t k_address_table_index{5};
  constexpr std::size_t k_address_stride{0x10};

  auto table{table_view(k_address_table_index)};
  if (!table) {
    return std::nullopt;
  }
  for (std::size_t index{0}; index < table_count(k_address_table_index); ++index) {
    const std::span<const std::byte> record{
        table->subspan(index * k_address_stride, k_address_stride)};
    IamAreaAddressRecord address{.serialized_position = {read_i32_at(record, 0x00U),
                                     read_i32_at(record, 0x04U),
                                     read_i32_at(record, 0x08U)},
        .orientation_units = read_i16_at(record, 0x0CU),
        .address_id = read_i16_at(record, 0x0EU)};
    if (address.address_id == address_id) {
      return address;
    }
  }
  return std::nullopt;
}

std::vector<IamAreaZoneRecord> IamAreaRecord::zones() const {
  constexpr std::size_t k_zone_table_index{2};
  constexpr std::size_t k_zone_stride{0x44};

  std::vector<IamAreaZoneRecord> result;
  const auto table{table_view(k_zone_table_index)};
  if (!table) {
    return result;
  }
  result.reserve(static_cast<std::size_t>(table_count(k_zone_table_index)));
  for (std::size_t index{0}; index < table_count(k_zone_table_index); ++index) {
    const std::span<const std::byte, k_zone_stride> record{
        table->subspan(index * k_zone_stride, k_zone_stride)};
    result.push_back(parse_iam_zone_record(record));
  }
  return result;
}

}  // namespace App::Omikron
