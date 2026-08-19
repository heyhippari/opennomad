#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Core/Sprite/SpriteRenderMode.hpp"

namespace App::Sprite {

/// Stable identifier of one pool slot: an index plus a generation counter.
/// The generation invalidates handles after the slot is destroyed, so a
/// stale handle can never reach a recycled slot.
struct SpriteHandle {
  static constexpr std::uint32_t k_invalid_index{0xFFFFFFFFU};

  std::uint32_t index{k_invalid_index};
  std::uint32_t generation{0};

  [[nodiscard]] constexpr bool valid() const { return index != k_invalid_index; }
  friend constexpr bool operator==(const SpriteHandle&, const SpriteHandle&) = default;
};

/// One runtime-style sprite instance. The representation preserves the
/// behavioural semantics of the original 0x40-byte SpriteInstance without
/// reproducing its binary layout.
struct SpriteInstance {
  /// Sentinel for "no frame selected / not rendered", as in the original.
  static constexpr std::uint16_t k_invalid_frame{0xFFFFU};

  SpriteHandle handle;
  /// Index into the scene's sprite resource registry (owns the decoded
  /// effect model and texture data).
  std::size_t resource_index{0};
  /// Object within the resource whose frame table drives this sprite.
  std::size_t object_index{0};
  std::array<float, 3> position{0.0F, 0.0F, 0.0F};
  SpriteRenderMode render_mode{SpriteRenderMode::k_default};
  std::uint16_t frame_index{k_invalid_frame};
  /// Runtime +0x14: sprite type, written by the SetSpriteType native opcode.
  /// Preserved as a 16-bit value even though the default is zero.
  std::uint16_t type{0};
  float scale_x{1.0F};  ///< Runtime default 1.0.
  float scale_y{1.0F};  ///< Runtime default 1.0.
  /// Radians, around the billboard centre (Runtime +0x20).
  float rotation{0.0F};
  /// Runtime +0x24, default 0.9. Semantics not yet understood — kept with an
  /// explicitly provisional name and never interpreted by the renderer.
  float unknown_24{0.9F};
  float texture_offset_u{0.0F};
  float texture_offset_v{0.0F};
  /// Diffuse tint from Runtime's 0x00RRGGBB colour (linear RGB in [0, 1]).
  std::array<float, 3> tint{1.0F, 1.0F, 1.0F};
  /// Opaque gameplay association; cleared on destruction, never dereferenced
  /// by the sprite system.
  void* external_association{nullptr};
  /// Owning render list while attached; nullptr when detached. Distinct from
  /// resource ownership: a sprite may use a resource owned by one model
  /// while being attached to another render scene.
  void* render_list_owner{nullptr};
};

}  // namespace App::Sprite
