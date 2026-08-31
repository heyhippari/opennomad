#include "Core/Omikron/SFX.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Omikron/BinaryReader.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

namespace {

constexpr std::size_t K_RECORD_A_SIZE{0x28U};
constexpr std::size_t K_RECORD_B_SIZE{0x2CU};
constexpr std::size_t K_DEFINITION_SIZE{0x50U};
constexpr std::size_t K_SECTION_D_SIZE{0x10U};
constexpr std::size_t K_NODE_SIZE{0x4CU};
constexpr std::size_t K_TRACK_HEADER_SIZE{0x10U};
constexpr std::size_t K_TRACK_POINT_SIZE{0x24U};

std::size_t parsed_count(const std::uint32_t raw_count) {
  return static_cast<std::size_t>(raw_count & 0xFFU);
}

std::string read_fixed_string(BinaryReader& reader, const std::size_t width) {
  const std::span<const std::byte> bytes{reader.read_bytes(width)};
  std::string value;
  value.reserve(width);
  for (const std::byte byte : bytes) {
    const char character{static_cast<char>(byte)};
    if (character == '\0') {
      break;
    }
    value.push_back(character);
  }
  return value;
}

template <std::size_t Size>
std::array<std::byte, Size> read_raw(BinaryReader& reader) {
  std::array<std::byte, Size> result{};
  const std::span<const std::byte> source{reader.read_bytes(Size)};
  if (!reader.has_error()) {
    std::ranges::copy(source, result.begin());
  }
  return result;
}

std::expected<void, std::string> require_records(BinaryReader& reader,
    const std::size_t count,
    const std::size_t stride,
    const std::string_view section) {
  if (count > (reader.remaining() / stride)) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("SFX {} count {} requires {} bytes at offset {}, only {} remain",
            section,
            count,
            count * stride,
            reader.tell(),
            reader.remaining())};
  }
  return {};
}

SfxDefinition read_definition(BinaryReader& reader) {
  SfxDefinition definition;
  definition.definition_id = reader.read_i32();
  definition.sound_id = reader.read_i32();
  definition.sprite_id_raw = reader.read_u32();
  definition.flags = reader.read_u32();
  definition.direction =
      Runtime::Vec3{.x = reader.read_f32(), .y = reader.read_f32(), .z = reader.read_f32()};
  definition.vertical_acceleration = reader.read_f32();
  definition.lifetime = reader.read_f32();
  definition.sound_delay = reader.read_f32();
  definition.emission_delay = reader.read_f32();
  definition.raw_2c = reader.read_f32();
  definition.start_color_rgb = reader.read_u32();
  definition.end_color_rgb = reader.read_u32();
  definition.initial_scale = reader.read_f32();
  definition.cone_angle_degrees = reader.read_f32();
  definition.angular_velocity_degrees = reader.read_f32();
  definition.spawn_count = std::bit_cast<std::int16_t>(reader.read_u16());
  definition.name = read_fixed_string(reader, 8U);
  definition.sprite_render_mode = reader.read_u8();
  definition.raw_4f = reader.read_u8();
  return definition;
}

SfxCinAnimationRecord read_cin_animation_record(BinaryReader& reader) {
  SfxCinAnimationRecord record;
  record.association_id = reader.read_u32();
  record.animation_lookup_raw = reader.read_u32();
  record.flags = reader.read_u32();
  record.channel1_definition_id = reader.read_i32();
  record.channel1_start = reader.read_f32();
  record.channel1_end = reader.read_f32();
  record.channel1_object_ref = reader.read_i32();
  record.channel2_definition_id = reader.read_i32();
  record.channel2_start = reader.read_f32();
  record.channel2_end = reader.read_f32();
  record.channel2_object_ref = reader.read_i32();
  return record;
}

SfxStaticEmitterRecord read_static_emitter_record(BinaryReader& reader) {
  SfxStaticEmitterRecord record;
  record.file_offset = reader.tell();
  record.definition_id = reader.read_i32();
  for (std::size_t index{0}; index < record.object_name_prefix.size(); ++index) {
    record.object_name_prefix.at(index) = static_cast<char>(reader.read_u8());
  }
  record.duration = reader.read_f32();
  record.emission_interval = reader.read_f32();
  return record;
}

SfxNode read_node(BinaryReader& reader) {
  SfxNode node;
  node.node_id = reader.read_i32();
  node.label = read_fixed_string(reader, 4U);
  node.trigger_type = reader.read_i32();
  node.trigger_id = reader.read_i32();
  node.track_id = reader.read_i32();
  node.serialized_track_ptr = reader.read_u32();
  node.serialized_point_ptr = reader.read_u32();
  node.serialized_runtime_position =
      Runtime::Vec3{.x = reader.read_f32(), .y = reader.read_f32(), .z = reader.read_f32()};
  node.anchor_reference_type = reader.read_i32();
  node.anchor_reference_id = reader.read_i32();
  node.serialized_anchor_ptr = reader.read_u32();
  node.fixed_definition_id = reader.read_i32();
  node.startup_delay = reader.read_f32();
  node.serialized_elapsed = reader.read_f32();
  node.repeat_limit = reader.read_i32();
  node.serialized_repeat_index = reader.read_i32();
  node.flags = reader.read_u32();
  return node;
}

SfxTrackPoint read_track_point(BinaryReader& reader) {
  SfxTrackPoint point;
  point.point_id = reader.read_i32();
  point.definition_id = reader.read_i32();
  point.position =
      Runtime::Vec3{.x = reader.read_f32(), .y = reader.read_f32(), .z = reader.read_f32()};
  point.segment_duration = reader.read_f32();
  point.reference_type = reader.read_i32();
  point.reference_id = reader.read_i32();
  point.serialized_reference_ptr = reader.read_u32();
  return point;
}

}  // namespace

std::expected<SfxData, std::string> SFX::load(const std::span<const std::byte> data) {
  BinaryReader reader{data};
  SfxData result;
  result.magic = reader.read_u32();
  if (reader.has_error()) {
    return std::expected<SfxData, std::string>{
        std::unexpect, fmt::format("SFX header: {}", reader.error())};
  }
  if (result.magic != k_sfx_magic) {
    return std::expected<SfxData, std::string>{std::unexpect,
        fmt::format("invalid SFX magic {:#010x}, expected {:#010x}", result.magic, k_sfx_magic)};
  }

  result.raw_count_a = reader.read_u32();
  if (reader.has_error()) {
    return std::expected<SfxData, std::string>{
        std::unexpect, fmt::format("SFX section A count: {}", reader.error())};
  }
  const std::size_t count_a{parsed_count(result.raw_count_a)};
  if (auto valid{require_records(reader, count_a, K_RECORD_A_SIZE, "section A")}; !valid) {
    return std::expected<SfxData, std::string>{std::unexpect, std::move(valid).error()};
  }
  result.records_a.reserve(count_a);
  for (std::size_t index{0}; index < count_a; ++index) {
    result.records_a.push_back(SfxRawRecord28{.bytes = read_raw<K_RECORD_A_SIZE>(reader)});
  }

  result.raw_count_b = reader.read_u32();
  if (reader.has_error()) {
    return std::expected<SfxData, std::string>{
        std::unexpect, fmt::format("SFX section B count: {}", reader.error())};
  }
  const std::size_t count_b{parsed_count(result.raw_count_b)};
  if (auto valid{require_records(reader, count_b, K_RECORD_B_SIZE, "section B")}; !valid) {
    return std::expected<SfxData, std::string>{std::unexpect, std::move(valid).error()};
  }
  result.records_b.reserve(count_b);
  for (std::size_t index{0}; index < count_b; ++index) {
    result.records_b.push_back(read_cin_animation_record(reader));
  }

  result.raw_definition_count = reader.read_u32();
  if (reader.has_error()) {
    return std::expected<SfxData, std::string>{
        std::unexpect, fmt::format("SFX definition count: {}", reader.error())};
  }
  const std::size_t definition_count{parsed_count(result.raw_definition_count)};
  if (auto valid{require_records(reader, definition_count, K_DEFINITION_SIZE, "definition")};
      !valid) {
    return std::expected<SfxData, std::string>{std::unexpect, std::move(valid).error()};
  }
  result.definitions.reserve(definition_count);
  for (std::size_t index{0}; index < definition_count; ++index) {
    result.definitions.push_back(read_definition(reader));
  }

  // Retail permits the recovered tail grammar to be absent when the file
  // ends immediately after definitions.
  if (reader.remaining() == 0U) {
    return result;
  }

  result.raw_section_d_count = reader.read_u32();
  if (reader.has_error()) {
    return std::expected<SfxData, std::string>{
        std::unexpect, fmt::format("SFX section D count: {}", reader.error())};
  }
  const std::size_t section_d_count{parsed_count(result.raw_section_d_count)};
  if (auto valid{require_records(reader, section_d_count, K_SECTION_D_SIZE, "section D")}; !valid) {
    return std::expected<SfxData, std::string>{std::unexpect, std::move(valid).error()};
  }
  result.section_d.reserve(section_d_count);
  for (std::size_t index{0}; index < section_d_count; ++index) {
    result.section_d.push_back(read_static_emitter_record(reader));
  }

  if (reader.remaining() == 0U) {
    return result;
  }

  result.raw_node_count = reader.read_u32();
  if (reader.has_error()) {
    return std::expected<SfxData, std::string>{
        std::unexpect, fmt::format("SFX node count: {}", reader.error())};
  }
  const std::size_t node_count{parsed_count(result.raw_node_count)};
  if (reader.remaining() == 0U) {
    return result;
  }
  if (auto valid{require_records(reader, node_count, K_NODE_SIZE, "node")}; !valid) {
    return std::expected<SfxData, std::string>{std::unexpect, std::move(valid).error()};
  }
  result.nodes.reserve(node_count);
  for (std::size_t index{0}; index < node_count; ++index) {
    result.nodes.push_back(read_node(reader));
  }

  if (reader.remaining() == 0U) {
    return result;
  }

  result.raw_track_count = reader.read_u32();
  if (reader.has_error()) {
    return std::expected<SfxData, std::string>{
        std::unexpect, fmt::format("SFX track count: {}", reader.error())};
  }
  const std::size_t track_count{parsed_count(result.raw_track_count)};
  result.tracks.reserve(track_count);
  for (std::size_t track_index{0}; track_index < track_count; ++track_index) {
    if (auto valid{require_records(reader, 1U, K_TRACK_HEADER_SIZE, "track header")}; !valid) {
      return std::expected<SfxData, std::string>{
          std::unexpect, fmt::format("SFX track {}: {}", track_index, valid.error())};
    }
    SfxTrack track;
    track.track_id = reader.read_i32();
    track.label = read_fixed_string(reader, 4U);
    track.point_count = reader.read_u32();
    track.mutable_duration_seed = reader.read_f32();
    if (track.point_count > (reader.remaining() / K_TRACK_POINT_SIZE)) {
      return std::expected<SfxData, std::string>{std::unexpect,
          fmt::format(
              "SFX track {} ID {} point count {} requires {} bytes at offset {}, only {} remain",
              track_index,
              track.track_id,
              track.point_count,
              static_cast<std::uint64_t>(track.point_count) * K_TRACK_POINT_SIZE,
              reader.tell(),
              reader.remaining())};
    }
    track.points.reserve(track.point_count);
    for (std::uint32_t point_index{0}; point_index < track.point_count; ++point_index) {
      track.points.push_back(read_track_point(reader));
    }
    result.tracks.push_back(std::move(track));
  }

  if (reader.remaining() != 0U) {
    return std::expected<SfxData, std::string>{std::unexpect,
        fmt::format("SFX has {} trailing byte(s) at offset {}", reader.remaining(), reader.tell())};
  }
  return result;
}

}  // namespace App::Omikron
