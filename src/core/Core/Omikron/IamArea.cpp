#include "Core/Omikron/IamArea.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"

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
    character.field_12 = read_u16_at(record, 0x12U);

    if (character.character_id == character_id) {
      return character;
    }
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
    IamAreaCameraRecord camera;
    for (std::size_t axis{0}; axis < 3U; ++axis) {
      camera.serialized_eye.at(axis) = read_i32_at(record, axis * 4U);
      camera.serialized_target.at(axis) = read_i32_at(record, 0x0CU + (axis * 4U));
    }
    camera.camera_id = read_i16_at(record, 0x18U);
    camera.camera_type = read_u16_at(record, 0x1AU);
    camera.roll_units = read_i16_at(record, 0x1CU);
    camera.horizontal_fov_units = read_i16_at(record, 0x1EU);
    camera.field_20 = read_i16_at(record, 0x20U);
    camera.field_22 = read_i16_at(record, 0x22U);
    for (std::size_t slot{0}; slot < camera.tail_fields.size(); ++slot) {
      camera.tail_fields.at(slot) = read_u16_at(record, 0x24U + (slot * 2U));
    }
    if (camera.camera_id == camera_id) {
      return camera;
    }
  }
  return std::nullopt;
}

}  // namespace App::Omikron
