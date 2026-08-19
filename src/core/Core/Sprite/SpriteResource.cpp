#include "SpriteResource.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/Sprite/SpriteFrame.hpp"

namespace App::Sprite {

std::expected<SpriteResource, std::string> SpriteResource::create(
    const std::span<const std::byte> scx_bytes,
    const Omikron::ScxModelResource& resource,
    const Omikron::ScxSpriteEntry& sprite) {
  APP_PROFILE_FUNCTION();

  auto model{Omikron::Model3DO::load(scx_bytes.subspan(resource.core_offset, resource.core_size))};
  if (!model) {
    return std::expected<SpriteResource, std::string>{std::unexpect,
        fmt::format("Failed to decode embedded model '{}': {}", sprite.name, model.error())};
  }

  // Cross-check the declared auxiliary size against what the material
  // descriptors consume (palette + payload per material, back to back).
  const auto expected_auxiliary_size{Omikron::Texture3DT::encoded_size(model->materials)};
  if (!expected_auxiliary_size) {
    return std::expected<SpriteResource, std::string>{std::unexpect,
        fmt::format("Failed to compute the auxiliary size of model '{}': {}",
            sprite.name,
            expected_auxiliary_size.error())};
  }
  if (expected_auxiliary_size.value() != resource.auxiliary_size) {
    return std::expected<SpriteResource, std::string>{std::unexpect,
        fmt::format("Model '{}' auxiliary size mismatch: the header declares {:#x} but the "
                    "materials consume {:#x}",
            sprite.name,
            resource.auxiliary_size,
            expected_auxiliary_size.value())};
  }

  auto images{Omikron::Texture3DT::load(
      scx_bytes.subspan(resource.auxiliary_offset, resource.auxiliary_size), model->materials)};
  if (!images) {
    return std::expected<SpriteResource, std::string>{std::unexpect,
        fmt::format("Failed to decode textures of model '{}': {}", sprite.name, images.error())};
  }

  SpriteResource decoded{};
  decoded.name = sprite.name;
  decoded.sprite_id = sprite.sprite_id;
  decoded.model = std::move(model).value();
  decoded.images = std::move(images).value();
  return decoded;
}

std::size_t SpriteResource::object_count() const { return model.meshes.size(); }

std::size_t SpriteResource::frame_count(const std::size_t object_index) const {
  return Sprite::frame_count(model, object_index);
}

std::expected<SpriteFrame, SpriteFrameError> SpriteResource::resolve_frame(
    const std::size_t object_index,
    const std::uint16_t frame_index,
    const float texture_offset_u,
    const float texture_offset_v) const {
  return Sprite::resolve_frame(model, object_index, frame_index, texture_offset_u, texture_offset_v);
}

std::size_t SpriteResource::default_object_index() const {
  for (std::size_t index{0}; index < model.meshes.size(); ++index) {
    if (!model.polygons.at(index).rectangles.empty()) {
      return index;
    }
  }
  return 0;
}

}  // namespace App::Sprite
