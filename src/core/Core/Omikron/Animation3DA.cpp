#include "Core/Omikron/Animation3DA.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/BinaryReader.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

namespace {

constexpr std::uint32_t K_MAX_CHANNEL_COUNT{65536};
constexpr std::uint32_t K_MAX_SAMPLE_COUNT{4U * 1024U * 1024U};

std::string read_fixed_string(BinaryReader& reader, const std::size_t length) {
  const std::span<const std::byte> bytes{reader.read_bytes(length)};
  std::string result;
  result.reserve(length);
  for (const std::byte byte : bytes) {
    const char character{static_cast<char>(byte)};
    if (character == '\0') {
      break;
    }
    result.push_back(character);
  }
  return result;
}

Runtime::Vec3 read_vec3(BinaryReader& reader) {
  return Runtime::Vec3{
      .x = reader.read_f32(), .y = reader.read_f32(), .z = reader.read_f32()};
}

Runtime::Quaternion read_quaternion(BinaryReader& reader) {
  return Runtime::Quaternion{.w = reader.read_f32(),
      .x = reader.read_f32(),
      .y = reader.read_f32(),
      .z = reader.read_f32()};
}

}  // namespace

std::optional<Runtime::Vec3> Animation3DAChannel::sample_translation(const float progress) const {
  if (translations.empty()) {
    return std::nullopt;
  }
  const float clamped{std::clamp(progress, 0.0F, static_cast<float>(translations.size() - 1U))};
  const std::size_t first{static_cast<std::size_t>(std::floor(clamped))};
  const std::size_t second{std::min(first + 1U, translations.size() - 1U)};
  const float amount{clamped - static_cast<float>(first)};
  const Runtime::Vec3& from{translations.at(first)};
  const Runtime::Vec3& to{translations.at(second)};
  return Runtime::Vec3{.x = from.x + ((to.x - from.x) * amount),
      .y = from.y + ((to.y - from.y) * amount),
      .z = from.z + ((to.z - from.z) * amount)};
}

std::optional<Runtime::Quaternion> Animation3DAChannel::sample_rotation(const float progress) const {
  if (rotations.empty()) {
    return std::nullopt;
  }
  const float frame{std::clamp(
      std::max(progress, 1.0F), 0.0F, static_cast<float>(rotations.size() - 1U))};
  return rotations.at(static_cast<std::size_t>(std::floor(frame)));
}

std::expected<Animation3DA, std::string> Animation3DA::load(
    const std::span<const std::byte> data) {
  BinaryReader reader{data};
  Animation3DA animation;
  animation.max_frame_index = reader.read_u32();
  const std::uint32_t channel_count{reader.read_u32()};
  if (reader.has_error()) {
    return std::expected<Animation3DA, std::string>{std::unexpect, reader.error()};
  }
  if (channel_count > K_MAX_CHANNEL_COUNT) {
    return std::expected<Animation3DA, std::string>{
        std::unexpect, fmt::format("implausible 3DA channel count {}", channel_count)};
  }

  animation.channels.reserve(channel_count);
  for (std::uint32_t index{0}; index < channel_count; ++index) {
    Animation3DAChannel channel;
    channel.channel_id = reader.read_u32();
    channel.name = read_fixed_string(reader, 20);
    channel.translation_sample_count = reader.read_u32();
    channel.translation_stream_offset = reader.read_u32();
    channel.rotation_sample_count = reader.read_u32();
    channel.rotation_stream_offset = reader.read_u32();
    animation.channels.push_back(std::move(channel));
  }
  if (reader.has_error()) {
    return std::expected<Animation3DA, std::string>{
        std::unexpect, fmt::format("3DA channel descriptors: {}", reader.error())};
  }

  for (Animation3DAChannel& channel : animation.channels) {
    if (channel.translation_sample_count > K_MAX_SAMPLE_COUNT ||
        channel.rotation_sample_count > K_MAX_SAMPLE_COUNT) {
      return std::expected<Animation3DA, std::string>{std::unexpect,
          fmt::format("3DA channel '{}' has implausible sample counts {}/{}",
              channel.name,
              channel.translation_sample_count,
              channel.rotation_sample_count)};
    }
    if (channel.translation_stream_offset != 0U) {
      reader.seek(channel.translation_stream_offset);
      channel.translations.reserve(channel.translation_sample_count);
      for (std::uint32_t index{0}; index < channel.translation_sample_count; ++index) {
        channel.translations.push_back(read_vec3(reader));
      }
    }
    if (channel.rotation_stream_offset != 0U) {
      reader.seek(channel.rotation_stream_offset);
      channel.rotations.reserve(channel.rotation_sample_count);
      for (std::uint32_t index{0}; index < channel.rotation_sample_count; ++index) {
        channel.rotations.push_back(read_quaternion(reader));
      }
    }
    if (reader.has_error()) {
      return std::expected<Animation3DA, std::string>{std::unexpect,
          fmt::format("3DA channel '{}' streams: {}", channel.name, reader.error())};
    }
  }
  return animation;
}

const Animation3DAChannel* Animation3DA::channel_by_id(const std::uint32_t channel_id) const {
  const auto found{std::ranges::find(channels, channel_id, &Animation3DAChannel::channel_id)};
  return found == channels.end() ? nullptr : &(*found);
}

}  // namespace App::Omikron
