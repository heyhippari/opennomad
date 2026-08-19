#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

#include "Core/Omikron/Model3DO.hpp"

namespace App::Sprite {

/// Typed resolution failure of one sprite frame. The kind drives inspector
/// visibility diagnostics; the message carries the logging context.
struct SpriteFrameError {
  enum class Kind : std::uint8_t {
    k_object_out_of_range,
    k_frame_out_of_range,
    k_point_out_of_range,
    k_texture_out_of_range,
    k_degenerate_dimensions,
  };

  Kind kind{Kind::k_frame_out_of_range};
  std::string message;
};

/// One resolved sprite frame: the two point records, their converted UVs
/// (byte / 256 plus the per-instance texture offsets) and the texture index.
struct SpriteFrame {
  /// First two floats of each point record (world units).
  std::array<float, 2> point0{};
  std::array<float, 2> point1{};
  /// Converted UVs of the frame descriptor's first and third UV byte pairs
  /// (offsets +0x08/+0x09 and +0x0C/+0x0D) plus instance offsets.
  std::array<float, 2> uv0{};
  std::array<float, 2> uv1{};
  /// Index into the resource's material/texture table.
  std::int32_t texture_index{-1};
  /// Derived from the two point records, which are opposite corners of the
  /// quad: width = |point1.x - point0.x| and height = |point1.y - point0.y|.
  /// A zero dimension is degenerate.
  float width{0.0F};
  float height{0.0F};
};

/// Frame count of one object. Provisional rule: the serialized root frame
/// count wins when non-zero, otherwise the per-object rectangle (frame
/// descriptor) table size — the root field is 0 in every observed file.
[[nodiscard]] std::size_t frame_count(const Omikron::Model3DOData& model,
                                      std::size_t object_index);

/// Resolves one frame descriptor of an object into a renderable frame.
///
/// Point indices resolve against the object's own vertex block; the texture
/// index against the model's material table. UV bytes convert exactly as
/// byteValue / 256.0f and the per-instance texture offsets are added.
[[nodiscard]] std::expected<SpriteFrame, SpriteFrameError> resolve_frame(
    const Omikron::Model3DOData& model,
    std::size_t object_index,
    std::uint16_t frame_index,
    float texture_offset_u,
    float texture_offset_v);

}  // namespace App::Sprite
