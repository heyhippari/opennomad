#include "Core/Omikron/IamScene.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Omikron/IamCamera.hpp"
#include "Core/Omikron/IamCharacterDefinition.hpp"
#include "Core/Omikron/IamObjectPlacement.hpp"
#include "Core/Omikron/IamZone.hpp"

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

std::expected<std::size_t, std::string> span_end(const std::uint32_t offset,
    const std::int16_t count,
    const std::size_t stride,
    const std::size_t record_size,
    const std::size_t table_index) {
  if (count < 0) {
    return std::expected<std::size_t, std::string>{std::unexpect,
        fmt::format("IAM/SCENE record: table {} has negative count {}", table_index, count)};
  }
  const std::uint64_t size{static_cast<std::uint64_t>(count) * stride};
  const std::uint64_t end{static_cast<std::uint64_t>(offset) + size};
  if (offset > record_size || end > record_size || end > std::numeric_limits<std::size_t>::max()) {
    return std::expected<std::size_t, std::string>{std::unexpect,
        fmt::format("IAM/SCENE record: table {} span [{:#x}, {:#x}) exceeds the {:#x}-byte "
                    "record",
            table_index,
            offset,
            end,
            record_size)};
  }
  return static_cast<std::size_t>(end);
}

}  // namespace

std::optional<std::size_t> IamSceneRecord::table_stride(const std::size_t index) {
  constexpr std::array<std::size_t, k_table_count> k_strides{
      0x14U, 0x18U, 0x44U, 0x18U, 0x114U, 0U, 0x2CU, 0x08U};
  return index < k_strides.size() && k_strides.at(index) != 0U
             ? std::optional<std::size_t>{k_strides.at(index)}
             : std::nullopt;
}

std::expected<IamSceneRecord, std::string> IamSceneRecord::load(
    const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  if (data.size() < k_header_size) {
    return std::expected<IamSceneRecord, std::string>{std::unexpect,
        fmt::format("IAM/SCENE record: too small ({} bytes, expected at least {:#x})",
            data.size(),
            k_header_size)};
  }

  std::array<std::uint32_t, k_table_count> offsets{};
  std::array<std::int16_t, k_table_count> counts{};
  std::array<std::size_t, k_table_count> ends{};
  for (std::size_t index{0}; index < k_table_count; ++index) {
    offsets.at(index) = read_at<std::uint32_t>(data, k_offset_table_offsets + (index * 4U));
    counts.at(index) = read_at<std::int16_t>(data, k_offset_table_counts + (index * 2U));
    if (counts.at(index) < 0) {
      return std::expected<IamSceneRecord, std::string>{std::unexpect,
          fmt::format("IAM/SCENE record: table {} has negative count {}", index, counts.at(index))};
    }
    if (index == 5U) {
      if (counts.at(index) != 0 || offsets.at(index) != 0U) {
        return std::expected<IamSceneRecord, std::string>{
            std::unexpect, "IAM/SCENE record: table 5 is unsupported and must be empty"};
      }
      continue;
    }
    const std::optional<std::size_t> stride{table_stride(index)};
    if (!stride.has_value()) {
      return std::expected<IamSceneRecord, std::string>{
          std::unexpect, fmt::format("IAM/SCENE record: table {} has no known stride", index)};
    }
    auto end{span_end(offsets.at(index), counts.at(index), stride.value(), data.size(), index)};
    if (!end) {
      return std::expected<IamSceneRecord, std::string>{std::unexpect, std::move(end).error()};
    }
    ends.at(index) = end.value();
  }

  if (counts.at(0) != 0 && offsets.at(0) < k_header_size) {
    return std::expected<IamSceneRecord, std::string>{std::unexpect,
        fmt::format("IAM/SCENE record: table 0 starts at {:#x}, before the {:#x}-byte header",
            offsets.at(0),
            k_header_size)};
  }
  for (std::size_t index{1}; index <= 4U; ++index) {
    if (counts.at(index) != 0 && offsets.at(index) < ends.at(index - 1U)) {
      return std::expected<IamSceneRecord, std::string>{std::unexpect,
          fmt::format("IAM/SCENE record: table {} starts before table {} ends", index, index - 1U)};
    }
  }
  if (counts.at(7) != 0 && offsets.at(7) < ends.at(4)) {
    return std::expected<IamSceneRecord, std::string>{
        std::unexpect, "IAM/SCENE record: table 7 starts before table 4/string region ends"};
  }

  if (counts.at(6) != 0 && offsets.at(6) < ends.at(7)) {
    return std::expected<IamSceneRecord, std::string>{
        std::unexpect, "IAM/SCENE record: table 6 cameras start before table 7/script region ends"};
  }

  const std::uint32_t script_offset{read_at<std::uint32_t>(data, k_offset_script)};
  if (script_offset != 0U && (script_offset < ends.at(7) || script_offset > offsets.at(6))) {
    return std::expected<IamSceneRecord, std::string>{std::unexpect,
        fmt::format("IAM/SCENE record: script offset {:#x} is outside [{:#x}, {:#x}]",
            script_offset,
            ends.at(7),
            offsets.at(6))};
  }

  const std::size_t definition_count{static_cast<std::size_t>(counts.at(4))};
  for (std::size_t index{0}; index < definition_count; ++index) {
    const std::size_t record_offset{offsets.at(4) + (index * 0x114U)};
    std::array<std::optional<std::string>, 2> strings{};
    for (const std::size_t string_field : {0U, 4U}) {
      const std::uint32_t string_offset{read_at<std::uint32_t>(data, record_offset + string_field)};
      if (string_offset == 0U) {
        continue;
      }
      if (string_offset >= data.size() ||
          std::memchr(data.subspan(string_offset).data(), '\0', data.size() - string_offset) ==
              nullptr) {
        return std::expected<IamSceneRecord, std::string>{std::unexpect,
            fmt::format("IAM/SCENE record: table-4 string at {:#x} is not NUL terminated in record",
                string_offset)};
      }
      strings.at(string_field / sizeof(std::uint32_t)) = fixed_string(data.subspan(string_offset));
    }
    auto parsed{parse_iam_character_definition(
        data.subspan(record_offset, 0x114U), std::move(strings.at(0)), std::move(strings.at(1)))};
    if (!parsed) {
      return std::expected<IamSceneRecord, std::string>{std::unexpect,
          fmt::format("IAM/SCENE record: table-4 character {}: {}", index, parsed.error())};
    }
  }

  const std::size_t zone_count{static_cast<std::size_t>(counts.at(2))};
  for (std::size_t index{0}; index < zone_count; ++index) {
    const std::size_t record_offset{offsets.at(2) + (index * 0x44U)};
    for (std::size_t event{0}; event < 3U; ++event) {
      const std::uint32_t event_offset{read_at<std::uint32_t>(data, record_offset + (event * 4U))};
      if (event_offset != 0U && event_offset >= data.size()) {
        return std::expected<IamSceneRecord, std::string>{std::unexpect,
            fmt::format("IAM/SCENE record: zone {} event {} offset {:#x} is outside record",
                index,
                event + 1U,
                event_offset)};
      }
    }
  }
  const std::size_t link_count{static_cast<std::size_t>(counts.at(7))};
  for (std::size_t index{0}; index < link_count; ++index) {
    const std::size_t record_offset{offsets.at(7) + (index * 0x08U)};
    const std::uint32_t program_offset{read_at<std::uint32_t>(data, record_offset)};
    if (program_offset != 0U && program_offset >= data.size()) {
      return std::expected<IamSceneRecord, std::string>{std::unexpect,
          fmt::format("IAM/SCENE record: script link {} offset {:#x} is outside record",
              index,
              program_offset)};
    }
  }
  const std::size_t camera_count{static_cast<std::size_t>(counts.at(6))};
  for (std::size_t index{0}; index < camera_count; ++index) {
    const std::size_t record_offset{offsets.at(6) + (index * IamCameraRecord::k_serialized_size)};
    auto camera{parse_iam_camera(data.subspan(record_offset, IamCameraRecord::k_serialized_size))};
    if (!camera) {
      return std::expected<IamSceneRecord, std::string>{
          std::unexpect, fmt::format("IAM/SCENE record: camera {}: {}", index, camera.error())};
    }
  }

  return IamSceneRecord{std::vector<std::byte>{data.begin(), data.end()}};
}

std::uint32_t IamSceneRecord::runtime_context_placeholder() const {
  return read_at<std::uint32_t>(m_bytes, k_offset_runtime_context);
}

std::uint32_t IamSceneRecord::script_offset() const {
  return read_at<std::uint32_t>(m_bytes, k_offset_script);
}

std::uint32_t IamSceneRecord::table_offset(const std::size_t index) const {
  return index < k_table_count
             ? read_at<std::uint32_t>(m_bytes, k_offset_table_offsets + (index * 4U))
             : 0U;
}

std::int16_t IamSceneRecord::table_count(const std::size_t index) const {
  return index < k_table_count
             ? read_at<std::int16_t>(m_bytes, k_offset_table_counts + (index * 2U))
             : 0;
}

std::expected<std::span<const std::byte>, std::string> IamSceneRecord::table_view(
    const std::size_t index) const {
  if (index >= k_table_count) {
    return std::expected<std::span<const std::byte>, std::string>{
        std::unexpect, fmt::format("IAM/SCENE record: table index {} is out of range", index)};
  }
  if (index == 5U) {
    return std::span<const std::byte>{};
  }
  const auto stride{table_stride(index)};
  if (!stride.has_value()) {
    return std::expected<std::span<const std::byte>, std::string>{
        std::unexpect, fmt::format("IAM/SCENE record: table {} has no known stride", index)};
  }
  const std::int16_t count{table_count(index)};
  if (count < 0) {
    return std::expected<std::span<const std::byte>, std::string>{
        std::unexpect, fmt::format("IAM/SCENE record: table {} has negative count", index)};
  }
  const std::size_t offset{table_offset(index)};
  const std::size_t size{static_cast<std::size_t>(count) * stride.value()};
  if (offset > m_bytes.size() || size > m_bytes.size() - offset) {
    return std::expected<std::span<const std::byte>, std::string>{
        std::unexpect, fmt::format("IAM/SCENE record: table {} view is outside record", index)};
  }
  return std::span<const std::byte>{m_bytes}.subspan(offset, size);
}

std::span<const std::byte> IamSceneRecord::script_bytes() const {
  if (script_offset() == 0U) {
    return {};
  }
  const std::size_t start{script_offset()};
  const std::size_t end{table_offset(6)};
  return std::span<const std::byte>{m_bytes}.subspan(start, end - start);
}

std::optional<std::string> IamSceneRecord::optional_record_string(
    const std::uint32_t offset) const {
  if (offset == 0U) {
    return std::nullopt;
  }
  return fixed_string(std::span<const std::byte>{m_bytes}.subspan(offset));
}

std::optional<IamSceneCharacterRecord> IamSceneRecord::character_by_id(
    const std::int16_t character_id) const {
  const std::vector<IamSceneCharacterRecord> characters{character_placements()};
  for (const IamSceneCharacterRecord& character : characters) {
    if (character.character_id == character_id) {
      return character;
    }
  }
  return std::nullopt;
}

std::vector<IamSceneCharacterRecord> IamSceneRecord::character_placements() const {
  std::vector<IamSceneCharacterRecord> result;
  const auto table{table_view(0)};
  if (!table) {
    return result;
  }
  const std::size_t count{static_cast<std::size_t>(table_count(0))};
  result.reserve(count);
  for (std::size_t index{0}; index < count; ++index) {
    const std::span<const std::byte> record{table->subspan(index * 0x14U, 0x14U)};
    const IamSceneCharacterRecord character{.runtime_slot_seed = read_at<std::int16_t>(record, 0U),
        .character_id = read_at<std::int16_t>(record, 2U),
        .serialized_position = {read_at<std::int32_t>(record, 4U),
            read_at<std::int32_t>(record, 8U),
            read_at<std::int32_t>(record, 12U)},
        .orientation_units = read_at<std::int16_t>(record, 16U),
        .state_bit_index = read_at<std::uint16_t>(record, 18U)};
    result.push_back(character);
  }
  return result;
}

std::optional<IamSceneCharacterDefinitionRecord>
IamSceneRecord::character_definition_by_character_id(const std::int16_t character_id) const {
  const auto table{table_view(4)};
  if (!table) {
    return std::nullopt;
  }
  const std::size_t count{static_cast<std::size_t>(table_count(4))};
  for (std::size_t index{0}; index < count; ++index) {
    const std::span<const std::byte> record{table->subspan(index * 0x114U, 0x114U)};
    if (read_at<std::int16_t>(record, 0x110U) != character_id) {
      continue;
    }
    auto definition{parse_iam_character_definition(record,
        optional_record_string(read_at<std::uint32_t>(record, 0U)),
        optional_record_string(read_at<std::uint32_t>(record, 4U)))};
    return definition ? std::optional<IamSceneCharacterDefinitionRecord>{std::move(*definition)}
                      : std::nullopt;
  }
  return std::nullopt;
}

std::optional<IamCameraRecord> IamSceneRecord::camera_by_id(const std::int16_t camera_id) const {
  const auto table{table_view(6)};
  if (!table) {
    return std::nullopt;
  }
  const std::size_t count{static_cast<std::size_t>(table_count(6))};
  for (std::size_t index{0}; index < count; ++index) {
    auto camera{parse_iam_camera(table->subspan(index * 0x2CU, 0x2CU))};
    if (camera && camera->camera_id == camera_id) {
      return camera.value();
    }
  }
  return std::nullopt;
}

std::vector<IamSceneObjectPlacementRecord> IamSceneRecord::object_placements() const {
  std::vector<IamSceneObjectPlacementRecord> result;
  const auto table{table_view(1)};
  if (!table) {
    return result;
  }
  result.reserve(static_cast<std::size_t>(table_count(1)));
  const std::size_t count{static_cast<std::size_t>(table_count(1))};
  for (std::size_t index{0}; index < count; ++index) {
    const std::span<const std::byte, 0x18> record{table->subspan(index * 0x18U, 0x18U)};
    result.push_back(parse_iam_object_placement(record));
  }
  return result;
}

std::optional<IamSceneObjectPlacementRecord> IamSceneRecord::object_by_id(
    const std::int16_t object_id) const {
  const auto placements{object_placements()};
  const auto found{
      std::ranges::find(placements, object_id, &IamSceneObjectPlacementRecord::object_id)};
  return found == placements.end() ? std::nullopt
                                   : std::optional<IamSceneObjectPlacementRecord>{*found};
}

std::vector<IamSceneObjectDefinitionRecord> IamSceneRecord::object_definitions() const {
  std::vector<IamSceneObjectDefinitionRecord> result;
  const auto table{table_view(3)};
  if (!table) {
    return result;
  }
  result.reserve(static_cast<std::size_t>(table_count(3)));
  const std::size_t count{static_cast<std::size_t>(table_count(3))};
  for (std::size_t index{0}; index < count; ++index) {
    const std::span<const std::byte, 0x18> record{table->subspan(index * 0x18U, 0x18U)};
    result.push_back(parse_iam_object_definition(record));
  }
  return result;
}

std::optional<IamSceneObjectDefinitionRecord> IamSceneRecord::object_definition_by_object_id(
    const std::int16_t object_id) const {
  const auto definitions{object_definitions()};
  const auto found{
      std::ranges::find(definitions, object_id, &IamSceneObjectDefinitionRecord::object_id)};
  return found == definitions.end() ? std::nullopt
                                    : std::optional<IamSceneObjectDefinitionRecord>{*found};
}

std::vector<IamSceneZoneRecord> IamSceneRecord::zones() const {
  std::vector<IamSceneZoneRecord> result;
  const auto table{table_view(2)};
  if (!table) {
    return result;
  }
  result.reserve(static_cast<std::size_t>(table_count(2)));
  const std::size_t count{static_cast<std::size_t>(table_count(2))};
  for (std::size_t index{0}; index < count; ++index) {
    const std::span<const std::byte, 0x44> record{table->subspan(index * 0x44U, 0x44U)};
    result.push_back(parse_iam_zone_record(record));
  }
  return result;
}

std::vector<IamSceneScriptLinkRecord> IamSceneRecord::script_links() const {
  std::vector<IamSceneScriptLinkRecord> result;
  const auto table{table_view(7)};
  if (!table) {
    return result;
  }
  result.reserve(static_cast<std::size_t>(table_count(7)));
  const std::size_t count{static_cast<std::size_t>(table_count(7))};
  for (std::size_t index{0}; index < count; ++index) {
    const std::span<const std::byte> record{table->subspan(index * 8U, 8U)};
    result.push_back(IamSceneScriptLinkRecord{.program_offset = read_at<std::uint32_t>(record, 0U),
        .field_04 = read_at<std::int32_t>(record, 4U)});
  }
  return result;
}

}  // namespace App::Omikron
