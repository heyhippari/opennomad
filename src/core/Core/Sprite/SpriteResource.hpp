#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/Sprite/SpriteFrame.hpp"

namespace App::Sprite {

/// One decoded embedded effect resource: the parsed model core, its decoded
/// texture images and the per-object frame tables. Pure CPU data — the GPU
/// textures live with the scene so the type stays unit-testable without a
/// GL context.
struct SpriteResource {
  std::string name;
  std::uint32_t sprite_id{0};
  Omikron::Model3DOData model;
  std::vector<Omikron::Texture3DTImage> images;

  /// Decodes one embedded model package (core + auxiliary byte spans inside
  /// the SCX stream) through the shared Model3DO / Texture3DT pipeline.
  [[nodiscard]] static std::expected<SpriteResource, std::string> create(
      std::span<const std::byte> scx_bytes,
      const Omikron::ScxModelResource& resource,
      const Omikron::ScxSpriteEntry& sprite);

  [[nodiscard]] std::size_t object_count() const;
  /// Frame count of one object (see Sprite::frame_count for the provisional
  /// serialized-frame-count rule).
  [[nodiscard]] std::size_t frame_count(std::size_t object_index) const;
  [[nodiscard]] std::expected<SpriteFrame, SpriteFrameError> resolve_frame(std::size_t object_index,
      std::uint16_t frame_index,
      float texture_offset_u,
      float texture_offset_v) const;
  /// The first object whose frame table is non-empty, or 0 when none has
  /// frames — a convenient default for spawning.
  [[nodiscard]] std::size_t default_object_index() const;
};

}  // namespace App::Sprite
