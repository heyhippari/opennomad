#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

/// One Runtime hierarchical-object animation channel from a 3DA payload.
struct Animation3DAChannel {
  std::uint32_t channel_id{0};
  std::string name;
  std::uint32_t translation_sample_count{0};
  std::uint32_t translation_stream_offset{0};
  std::uint32_t rotation_sample_count{0};
  std::uint32_t rotation_stream_offset{0};
  std::vector<Runtime::Vec3> translations;
  std::vector<Runtime::Quaternion> rotations;

  /// Samples a translation stream continuously. A serialized null stream
  /// remains absent even when its nominal count is nonzero.
  [[nodiscard]] std::optional<Runtime::Vec3> sample_translation(float progress) const;
  /// Runtime body orientation selects the floor frame, clamped to frame 1.
  [[nodiscard]] std::optional<Runtime::Quaternion> sample_rotation(float progress) const;
};

/// Immutable decoded SCX 3DA animation payload.
struct Animation3DA {
  std::uint32_t max_frame_index{0};
  std::vector<Animation3DAChannel> channels;

  [[nodiscard]] static std::expected<Animation3DA, std::string> load(
      std::span<const std::byte> data);
  [[nodiscard]] const Animation3DAChannel* channel_by_id(std::uint32_t channel_id) const;
};

}  // namespace App::Omikron
