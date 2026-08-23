#include "SpritePool.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include <fmt/format.h>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Sprite/SpriteInstance.hpp"
#include "Core/Sprite/SpriteRenderMode.hpp"

namespace App::Sprite {

namespace {
/// Context prefix shared by every pool error message.
std::string handle_context(const SpriteHandle handle) {
  return fmt::format("sprite handle {}:{}", handle.index, handle.generation);
}
}  // namespace

SpritePool::SpritePool() { m_slots.reserve(k_default_capacity); }

std::expected<SpriteHandle, std::string> SpritePool::create(const std::size_t resource_index,
    const std::size_t object_index,
    const std::size_t frame_count,
    const std::array<float, 3> position) {
  APP_PROFILE_FUNCTION();

  std::uint32_t index{};
  std::uint32_t generation{0};
  if (!m_free_indices.empty()) {
    index = m_free_indices.back();
    m_free_indices.pop_back();
    generation = m_slots.at(static_cast<std::size_t>(index)).generation;
  } else {
    index = static_cast<std::uint32_t>(m_slots.size());
    m_slots.emplace_back();
  }

  Slot& slot{m_slots.at(static_cast<std::size_t>(index))};
  slot.instance = SpriteInstance{};
  slot.instance.handle = SpriteHandle{.index = index, .generation = generation};
  slot.instance.resource_index = resource_index;
  slot.instance.object_index = object_index;
  slot.instance.position = position;
  slot.frame_count = frame_count;
  slot.previous = k_no_link;
  slot.next = k_no_link;
  slot.attached = false;
  return slot.instance.handle;
}

std::expected<void, std::string> SpritePool::attach(const SpriteHandle handle) {
  APP_PROFILE_FUNCTION();

  Slot* slot{find_slot(handle)};
  if (slot == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("attach failed: stale {}", handle_context(handle))};
  }
  if (slot->attached) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("attach failed: {} is already attached", handle_context(handle))};
  }

  const std::uint32_t index{handle.index};
  slot->previous = k_no_link;
  slot->next = m_render_head;
  if (m_render_head != k_no_link) {
    m_slots.at(static_cast<std::size_t>(m_render_head)).previous = index;
  }
  m_render_head = index;
  slot->attached = true;
  slot->instance.render_list_owner = this;
  m_attached_count += 1;
  return {};
}

std::expected<void, std::string> SpritePool::detach(const SpriteHandle handle) {
  APP_PROFILE_FUNCTION();

  Slot* slot{find_slot(handle)};
  if (slot == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("detach failed: stale {}", handle_context(handle))};
  }
  if (!slot->attached) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("detach failed: {} is not attached", handle_context(handle))};
  }

  unlink(*slot);
  return {};
}

std::expected<void, std::string> SpritePool::destroy(const SpriteHandle handle) {
  APP_PROFILE_FUNCTION();

  Slot* slot{find_slot(handle)};
  if (slot == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("destroy failed: stale {}", handle_context(handle))};
  }

  if (slot->attached) {
    unlink(*slot);
  }
  slot->instance.external_association = nullptr;
  slot->instance.render_list_owner = nullptr;
  slot->generation += 1;  // Invalidate every outstanding handle to this slot.
  slot->frame_count = 0;
  slot->previous = k_no_link;
  slot->next = k_no_link;
  slot->attached = false;
  m_free_indices.push_back(handle.index);
  return {};
}

SpriteInstance* SpritePool::find(const SpriteHandle handle) {
  Slot* slot{find_slot(handle)};
  return slot == nullptr ? nullptr : &slot->instance;
}

const SpriteInstance* SpritePool::find(const SpriteHandle handle) const {
  const Slot* slot{find_slot(handle)};
  return slot == nullptr ? nullptr : &slot->instance;
}

bool SpritePool::attached(const SpriteHandle handle) const {
  const Slot* slot{find_slot(handle)};
  return slot != nullptr && slot->attached;
}

std::expected<void, std::string> SpritePool::set_frame(const SpriteHandle handle,
    const std::uint16_t frame_index) {
  APP_PROFILE_FUNCTION();

  Slot* slot{find_slot(handle)};
  if (slot == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("set_frame failed: stale {}", handle_context(handle))};
  }
  if (static_cast<std::size_t>(frame_index) >= slot->frame_count) {
    slot->instance.frame_index = SpriteInstance::k_invalid_frame;
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("set_frame failed: frame {} out of range for {} (object has {} frames)",
            frame_index,
            handle_context(handle),
            slot->frame_count)};
  }
  slot->instance.frame_index = frame_index;
  return {};
}

void SpritePool::set_render_mode(const SpriteHandle handle, const SpriteRenderMode mode) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->render_mode = mode;
  }
}

void SpritePool::set_type(const SpriteHandle handle, const std::uint16_t type) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->type = type;
  }
}

void SpritePool::set_position(const SpriteHandle handle, const std::array<float, 3> position) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->position = position;
  }
}

void SpritePool::set_scale(const SpriteHandle handle, const float scale_x, const float scale_y) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->scale_x = scale_x;
    instance->scale_y = scale_y;
  }
}

void SpritePool::set_scale_x(const SpriteHandle handle, const float scale_x) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->scale_x = scale_x;
  }
}

void SpritePool::set_scale_y(const SpriteHandle handle, const float scale_y) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->scale_y = scale_y;
  }
}

void SpritePool::set_rotation(const SpriteHandle handle, const float rotation) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->rotation = rotation;
  }
}

void SpritePool::set_tint(const SpriteHandle handle, const std::array<float, 3> tint) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->tint = tint;
  }
}

void SpritePool::set_texture_offset(const SpriteHandle handle,
    const float offset_u,
    const float offset_v) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->texture_offset_u = offset_u;
    instance->texture_offset_v = offset_v;
  }
}

void SpritePool::set_diffuse_alpha(const SpriteHandle handle, const float value) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->diffuse_alpha = value;
  }
}

void SpritePool::reset_to_defaults(const SpriteHandle handle) {
  if (SpriteInstance* instance{find(handle)}; instance != nullptr) {
    instance->scale_x = 1.0F;
    instance->scale_y = 1.0F;
    instance->diffuse_alpha = 0.9F;
    instance->frame_index = SpriteInstance::k_invalid_frame;
    instance->tint = {1.0F, 1.0F, 1.0F};
    instance->render_mode = SpriteRenderMode::k_default;
    instance->rotation = 0.0F;
    instance->texture_offset_u = 0.0F;
    instance->texture_offset_v = 0.0F;
  }
}

std::optional<SpriteHandle> SpritePool::render_list_head() const {
  if (m_render_head == k_no_link) {
    return std::nullopt;
  }
  const Slot& head{m_slots.at(static_cast<std::size_t>(m_render_head))};
  return SpriteHandle{.index = m_render_head, .generation = head.generation};
}

std::optional<SpriteHandle> SpritePool::render_list_next(const SpriteHandle handle) const {
  const Slot* slot{find_slot(handle)};
  if (slot == nullptr || slot->next == k_no_link) {
    return std::nullopt;
  }
  const Slot& next{m_slots.at(static_cast<std::size_t>(slot->next))};
  return SpriteHandle{.index = slot->next, .generation = next.generation};
}

std::size_t SpritePool::capacity() const { return m_slots.size(); }

std::size_t SpritePool::live_count() const { return m_slots.size() - m_free_indices.size(); }

std::size_t SpritePool::attached_count() const { return m_attached_count; }

SpritePool::Slot* SpritePool::find_slot(const SpriteHandle handle) {
  if (handle.index == SpriteHandle::k_invalid_index ||
      static_cast<std::size_t>(handle.index) >= m_slots.size()) {
    return nullptr;
  }
  Slot& slot{m_slots.at(static_cast<std::size_t>(handle.index))};
  if (slot.generation != handle.generation) {
    return nullptr;
  }
  return &slot;
}

const SpritePool::Slot* SpritePool::find_slot(const SpriteHandle handle) const {
  if (handle.index == SpriteHandle::k_invalid_index ||
      static_cast<std::size_t>(handle.index) >= m_slots.size()) {
    return nullptr;
  }
  const Slot& slot{m_slots.at(static_cast<std::size_t>(handle.index))};
  if (slot.generation != handle.generation) {
    return nullptr;
  }
  return &slot;
}

void SpritePool::unlink(Slot& slot) {
  const std::uint32_t previous{slot.previous};
  const std::uint32_t next{slot.next};
  if (previous != k_no_link) {
    m_slots.at(static_cast<std::size_t>(previous)).next = next;
  } else {
    m_render_head = next;
  }
  if (next != k_no_link) {
    m_slots.at(static_cast<std::size_t>(next)).previous = previous;
  }
  slot.previous = k_no_link;
  slot.next = k_no_link;
  slot.attached = false;
  slot.instance.render_list_owner = nullptr;
  m_attached_count -= 1;
}

}  // namespace App::Sprite
