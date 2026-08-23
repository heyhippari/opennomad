#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "Core/Sprite/SpriteInstance.hpp"

namespace App::Sprite {

/// Pool of sprite instances with stable generation-counted handles.
///
/// The pool grows on demand (the original runtime used a fixed 2,048-slot
/// pool; k_default_capacity keeps that size as the initial reservation).
/// Slots are never moved after creation, so indices remain valid; a freed
/// slot is recycled with a bumped generation, which invalidates every
/// outstanding handle to it.
///
/// The pool also maintains the attached render list: attach() inserts at the
/// head (matching the original runtime), detach() unlinks and repairs the
/// neighbours, and destroy() detaches automatically.
class SpritePool {
 public:
  /// Runtime-compatible starting capacity (the original pool held 2,048
  /// instances); the pool grows beyond it instead of failing.
  static constexpr std::size_t k_default_capacity{2048U};

  SpritePool();
  SpritePool(const SpritePool&) = delete;
  SpritePool(SpritePool&&) = delete;
  SpritePool& operator=(const SpritePool&) = delete;
  SpritePool& operator=(SpritePool&&) = delete;
  ~SpritePool() = default;

  /// Allocates a free slot with the Runtime creation defaults: scale 1/1,
  /// diffuse alpha 0.9, frame index 0xFFFF, tint white, render mode default.
  /// frame_count is the object's frame table size and drives set_frame().
  [[nodiscard]] std::expected<SpriteHandle, std::string> create(
      std::size_t resource_index,
      std::size_t object_index,
      std::size_t frame_count,
      std::array<float, 3> position = {0.0F, 0.0F, 0.0F});

  /// Inserts the instance at the head of the render list.
  [[nodiscard]] std::expected<void, std::string> attach(SpriteHandle handle);

  /// Unlinks the instance from the render list, repairing the neighbours.
  [[nodiscard]] std::expected<void, std::string> detach(SpriteHandle handle);

  /// Destroys the instance: detaches it when attached, clears the external
  /// association and frees the slot (invalidating the handle).
  [[nodiscard]] std::expected<void, std::string> destroy(SpriteHandle handle);

  /// The live instance behind a handle, or nullptr when stale.
  [[nodiscard]] SpriteInstance* find(SpriteHandle handle);
  [[nodiscard]] const SpriteInstance* find(SpriteHandle handle) const;
  [[nodiscard]] bool attached(SpriteHandle handle) const;

  /// Runtime SetSpriteFrame semantics: accepts only frame_index < the
  /// object's frame count. On failure the instance stores 0xFFFF.
  [[nodiscard]] std::expected<void, std::string> set_frame(SpriteHandle handle,
                                                           std::uint16_t frame_index);

  /// Direct assignment like Runtime's SetSpriteRenderMode (always succeeds).
  void set_render_mode(SpriteHandle handle, SpriteRenderMode mode);
  void set_type(SpriteHandle handle, std::uint16_t type);
  void set_position(SpriteHandle handle, std::array<float, 3> position);
  void set_scale(SpriteHandle handle, float scale_x, float scale_y);
  void set_scale_x(SpriteHandle handle, float scale_x);
  void set_scale_y(SpriteHandle handle, float scale_y);
  void set_rotation(SpriteHandle handle, float rotation);
  void set_tint(SpriteHandle handle, std::array<float, 3> tint);
  void set_texture_offset(SpriteHandle handle, float offset_u, float offset_v);
  /// Assigns Runtime's normalized diffuse alpha field at +0x24.
  void set_diffuse_alpha(SpriteHandle handle, float value);
  /// Restores the Runtime creation defaults (position and attachment are
  /// left untouched).
  void reset_to_defaults(SpriteHandle handle);

  /// Render-list traversal in attachment order (head first).
  [[nodiscard]] std::optional<SpriteHandle> render_list_head() const;
  [[nodiscard]] std::optional<SpriteHandle> render_list_next(SpriteHandle handle) const;

  [[nodiscard]] std::size_t capacity() const;
  [[nodiscard]] std::size_t live_count() const;
  [[nodiscard]] std::size_t attached_count() const;

 private:
  /// One pool slot: the instance value plus its reuse and linkage state.
  struct Slot {
    SpriteInstance instance;
    std::uint32_t generation{0};
    std::uint32_t previous{k_no_link};
    std::uint32_t next{k_no_link};
    bool attached{false};
    std::size_t frame_count{0};
  };

  static constexpr std::uint32_t k_no_link{0xFFFFFFFFU};

  [[nodiscard]] Slot* find_slot(SpriteHandle handle);
  [[nodiscard]] const Slot* find_slot(SpriteHandle handle) const;
  void unlink(Slot& slot);

  std::vector<Slot> m_slots;
  std::vector<std::uint32_t> m_free_indices;
  std::uint32_t m_render_head{k_no_link};
  std::size_t m_attached_count{0};
};

}  // namespace App::Sprite
