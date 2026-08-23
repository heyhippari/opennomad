#include "Core/Omikron/ThreeDM.hpp"

#include <fmt/format.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/BinaryReader.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Omikron {
namespace {

[[nodiscard]] bool checked_add(const std::size_t first,
    const std::size_t second,
    std::size_t& result) {
  if (first > std::numeric_limits<std::size_t>::max() - second) {
    return false;
  }
  result = first + second;
  return true;
}

[[nodiscard]] bool checked_multiply(const std::size_t first,
    const std::size_t second,
    std::size_t& result) {
  if (first != 0U && second > std::numeric_limits<std::size_t>::max() / first) {
    return false;
  }
  result = first * second;
  return true;
}

[[nodiscard]] Runtime::Vec3 read_vec3(BinaryReader& reader) {
  return Runtime::Vec3{.x = reader.read_f32(), .y = reader.read_f32(), .z = reader.read_f32()};
}

[[nodiscard]] bool finite(const Runtime::Vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] std::expected<Runtime::Quaternion, std::string> read_quaternion(
    BinaryReader& reader, const std::size_t slot) {
  Runtime::Quaternion value{.w = reader.read_f32(),
      .x = reader.read_f32(),
      .y = reader.read_f32(),
      .z = reader.read_f32()};
  if (reader.has_error()) {
    return std::expected<Runtime::Quaternion, std::string>{std::unexpect, reader.error()};
  }
  const double length_squared{(static_cast<double>(value.w) * value.w) +
                              (static_cast<double>(value.x) * value.x) +
                              (static_cast<double>(value.y) * value.y) +
                              (static_cast<double>(value.z) * value.z)};
  if (!std::isfinite(length_squared) || length_squared <= 0.0) {
    return std::expected<Runtime::Quaternion, std::string>{std::unexpect,
        fmt::format("3DM object slot {} contains a non-finite or zero quaternion", slot)};
  }
  const float inverse_length{static_cast<float>(1.0 / std::sqrt(length_squared))};
  value.w *= inverse_length;
  value.x *= inverse_length;
  value.y *= inverse_length;
  value.z *= inverse_length;
  return value;
}

}  // namespace

std::expected<ThreeDM, std::string> ThreeDM::load(const std::span<const std::byte> data) {
  BinaryReader reader{data};
  const std::uint32_t packed{reader.read_u32()};
  ThreeDM clip;
  clip.m_header.stream_mode = static_cast<std::uint8_t>(packed >> 24U);
  clip.m_header.audio_bytes_per_frame = packed & 0x00FFFFFFU;
  clip.m_header.morph_vertex_count = reader.read_u32();
  clip.m_header.field_08 = reader.read_u32();
  const std::uint32_t object_count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<ThreeDM, std::string>{std::unexpect,
        fmt::format("truncated 3DM header: {}", reader.error())};
  }
  if (clip.m_header.stream_mode != 0U) {
    return std::expected<ThreeDM, std::string>{std::unexpect,
        fmt::format("unsupported 3DM stream mode {}", clip.m_header.stream_mode)};
  }
  if (clip.m_header.morph_vertex_count > k_max_morph_vertices) {
    return std::expected<ThreeDM, std::string>{std::unexpect,
        fmt::format("3DM morph vertex count {} exceeds limit {}",
            clip.m_header.morph_vertex_count,
            k_max_morph_vertices)};
  }
  if (object_count > k_max_objects) {
    return std::expected<ThreeDM, std::string>{std::unexpect,
        fmt::format("3DM object count {} exceeds limit {}", object_count, k_max_objects)};
  }
  clip.m_header.object_ids.reserve(object_count);
  for (std::uint32_t index{0}; index < object_count; ++index) {
    clip.m_header.object_ids.push_back(reader.read_u32());
  }
  if (reader.has_error()) {
    return std::expected<ThreeDM, std::string>{std::unexpect,
        fmt::format("truncated 3DM object ID table: {}", reader.error())};
  }

  std::size_t object_bytes{0};
  std::size_t morph_bytes{0};
  std::size_t motion_size{0};
  if (!checked_multiply(object_count, 16U, object_bytes) ||
      !checked_multiply(clip.m_header.morph_vertex_count, 24U, morph_bytes) ||
      !checked_add(object_bytes, 12U, motion_size) ||
      !checked_add(motion_size, morph_bytes, motion_size)) {
    return std::expected<ThreeDM, std::string>{std::unexpect, "3DM motion size overflows"};
  }
  std::size_t full_record_size{0};
  if (!checked_add(motion_size, clip.m_header.audio_bytes_per_frame, full_record_size) ||
      full_record_size == 0U) {
    return std::expected<ThreeDM, std::string>{std::unexpect, "3DM record size overflows"};
  }

  const std::size_t payload_offset{reader.tell()};
  const std::size_t payload_size{reader.remaining()};
  const std::size_t full_count{payload_size / full_record_size};
  const std::size_t remainder{payload_size % full_record_size};
  if (remainder != 0U && remainder != motion_size) {
    return std::expected<ThreeDM, std::string>{std::unexpect,
        fmt::format("3DM payload has invalid {}-byte trailing remainder", remainder)};
  }

  clip.m_motion_size = motion_size;
  clip.m_bytes.assign(data.begin(), data.end());
  clip.m_frames.reserve(full_count + (remainder == motion_size ? 1U : 0U));
  std::size_t offset{payload_offset};
  for (std::size_t index{0}; index < full_count; ++index) {
    clip.m_frames.push_back(ThreeDmFrameLocation{
        .motion_offset = offset, .audio_offset = offset + motion_size});
    offset += full_record_size;
  }
  if (remainder == motion_size) {
    clip.m_frames.push_back(ThreeDmFrameLocation{.motion_offset = offset, .audio_offset = {}});
  }
  return clip;
}

std::size_t ThreeDM::audio_chunk_count() const {
  std::size_t count{0};
  for (const ThreeDmFrameLocation& frame : m_frames) {
    count += frame.audio_offset.has_value() ? 1U : 0U;
  }
  return count;
}

std::expected<ThreeDmFrame, std::string> ThreeDM::decode_frame(
    const std::size_t frame_index, const std::size_t root_object_slot) const {
  if (frame_index >= m_frames.size()) {
    return std::expected<ThreeDmFrame, std::string>{std::unexpect,
        fmt::format("3DM frame {} out of range ({})", frame_index, m_frames.size())};
  }
  if (root_object_slot >= m_header.object_ids.size()) {
    return std::expected<ThreeDmFrame, std::string>{std::unexpect,
        fmt::format("3DM root object slot {} out of range ({})",
            root_object_slot,
            m_header.object_ids.size())};
  }
  const ThreeDmFrameLocation& location{m_frames.at(frame_index)};
  BinaryReader reader{std::span<const std::byte>{m_bytes}.subspan(location.motion_offset,
      m_motion_size)};
  ThreeDmFrame frame;
  frame.object_rotations.reserve(m_header.object_ids.size());
  for (std::size_t slot{0}; slot < m_header.object_ids.size(); ++slot) {
    if (slot == root_object_slot) {
      frame.root_translation = read_vec3(reader);
      if (!finite(frame.root_translation)) {
        return std::expected<ThreeDmFrame, std::string>{
            std::unexpect, "3DM root translation is non-finite"};
      }
    }
    auto quaternion{read_quaternion(reader, slot)};
    if (!quaternion) {
      return std::expected<ThreeDmFrame, std::string>{
          std::unexpect, std::move(quaternion).error()};
    }
    frame.object_rotations.push_back(quaternion.value());
  }
  frame.morph_vertices.reserve(m_header.morph_vertex_count);
  for (std::uint32_t index{0}; index < m_header.morph_vertex_count; ++index) {
    const ThreeDmMorphVertex vertex{.position = read_vec3(reader), .normal = read_vec3(reader)};
    if (reader.has_error() || !finite(vertex.position) || !finite(vertex.normal)) {
      return std::expected<ThreeDmFrame, std::string>{std::unexpect,
          fmt::format("3DM morph vertex {} is truncated or non-finite", index)};
    }
    frame.morph_vertices.push_back(vertex);
  }
  if (reader.has_error() || reader.remaining() != 0U) {
    return std::expected<ThreeDmFrame, std::string>{std::unexpect,
        reader.has_error() ? reader.error() : "3DM motion record size mismatch"};
  }
  return frame;
}

std::span<const std::byte> ThreeDM::audio_chunk(const std::size_t frame_index) const {
  if (frame_index >= m_frames.size() || !m_frames.at(frame_index).audio_offset.has_value()) {
    return {};
  }
  const std::size_t offset{m_frames.at(frame_index).audio_offset.value_or(0U)};
  return std::span<const std::byte>{m_bytes}.subspan(offset, m_header.audio_bytes_per_frame);
}

}  // namespace App::Omikron
