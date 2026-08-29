#include "SpriteFrame.hpp"

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Omikron/Model3DO.hpp"

namespace App::Sprite {

namespace {
constexpr float K_UV_SCALE{1.0F / 256.0F};  // Runtime converts UV bytes exactly as value / 256.
}  // namespace

std::size_t frame_count(const Omikron::Model3DOData& model, const std::size_t object_index) {
  if (object_index >= model.meshes.size()) {
    return 0;
  }
  // Provisional: the serialized root frame count is authoritative when
  // non-zero (it is 0 in every observed file), otherwise each object's
  // rectangle table is its frame descriptor table.
  if (model.header.frame_count != 0U) {
    return static_cast<std::size_t>(model.header.frame_count);
  }
  return model.polygons.at(object_index).rectangles.size();
}

std::expected<SpriteFrame, SpriteFrameError> resolve_frame(const Omikron::Model3DOData& model,
    const std::size_t object_index,
    const std::uint16_t frame_index,
    const float texture_offset_u,
    const float texture_offset_v) {
  APP_PROFILE_FUNCTION();

  if (object_index >= model.meshes.size()) {
    return std::expected<SpriteFrame, SpriteFrameError>{std::unexpect,
        SpriteFrameError{.kind = SpriteFrameError::Kind::k_object_out_of_range,
            .message = fmt::format(
                "object index {} out of range ({} objects)", object_index, model.meshes.size())}};
  }

  const Omikron::MeshDescriptor& mesh{model.meshes.at(object_index)};
  const std::vector<Omikron::Rectangle>& rectangles{model.polygons.at(object_index).rectangles};
  if (static_cast<std::size_t>(frame_index) >= rectangles.size()) {
    return std::expected<SpriteFrame, SpriteFrameError>{std::unexpect,
        SpriteFrameError{.kind = SpriteFrameError::Kind::k_frame_out_of_range,
            .message = fmt::format("frame {} out of range for object '{}' ({} frames)",
                frame_index,
                mesh.name,
                rectangles.size())}};
  }

  // A sprite frame descriptor uses the 16-bit slots at +0x00 and +0x04 of
  // the 0x20 record — the rectangle's first and third vertex indices — and
  // the UV byte pairs at +0x08/+0x09 and +0x0C/+0x0D (uv[0..1] and uv[4..5]).
  // The slots at +0x02/+0x06 and bytes 2-3/6-7 belong to the static-quad
  // interpretation and are ignored here.
  const Omikron::Rectangle& rectangle{rectangles.at(frame_index)};
  const std::uint16_t point_index0{rectangle.vertices.at(0)};
  const std::uint16_t point_index1{rectangle.vertices.at(2)};
  if (point_index0 >= mesh.vertex_count || point_index1 >= mesh.vertex_count) {
    return std::expected<SpriteFrame, SpriteFrameError>{std::unexpect,
        SpriteFrameError{.kind = SpriteFrameError::Kind::k_point_out_of_range,
            .message = fmt::format("frame {} of object '{}' references point {} or {} outside its "
                                   "vertex block ({} vertices)",
                frame_index,
                mesh.name,
                point_index0,
                point_index1,
                mesh.vertex_count)}};
  }

  const std::size_t global0{mesh.vertex_base + static_cast<std::size_t>(point_index0)};
  const std::size_t global1{mesh.vertex_base + static_cast<std::size_t>(point_index1)};
  if (global0 >= model.vertices.size() || global1 >= model.vertices.size()) {
    return std::expected<SpriteFrame, SpriteFrameError>{std::unexpect,
        SpriteFrameError{.kind = SpriteFrameError::Kind::k_point_out_of_range,
            .message = fmt::format("frame {} of object '{}' references vertex {} or {} outside "
                                   "the global vertex list ({} vertices)",
                frame_index,
                mesh.name,
                global0,
                global1,
                model.vertices.size())}};
  }

  const Omikron::RawVertex& point0{model.vertices.at(global0)};
  const Omikron::RawVertex& point1{model.vertices.at(global1)};

  // The first two floats of each point record derive the frame dimensions.
  // The two points are opposite corners of the quad, so a negative delta
  // merely flips the corner order — the absolute values are the width and
  // height (texture mirroring is already encoded in the UV pairs).
  const float width{std::abs(point1.position.x - point0.position.x)};
  const float height{std::abs(point1.position.y - point0.position.y)};
  if (width == 0.0F || height == 0.0F) {
    return std::expected<SpriteFrame, SpriteFrameError>{std::unexpect,
        SpriteFrameError{.kind = SpriteFrameError::Kind::k_degenerate_dimensions,
            .message = fmt::format("frame {} of object '{}' has degenerate dimensions "
                                   "({} x {})",
                frame_index,
                mesh.name,
                static_cast<double>(width),
                static_cast<double>(height))}};
  }

  if (rectangle.material_id < 0 ||
      static_cast<std::size_t>(rectangle.material_id) >= model.materials.size()) {
    return std::expected<SpriteFrame, SpriteFrameError>{std::unexpect,
        SpriteFrameError{.kind = SpriteFrameError::Kind::k_texture_out_of_range,
            .message = fmt::format("frame {} of object '{}' references texture {} outside the "
                                   "material table ({} materials)",
                frame_index,
                mesh.name,
                rectangle.material_id,
                model.materials.size())}};
  }

  SpriteFrame frame{};
  frame.point0 = {point0.position.x, point0.position.y};
  frame.point1 = {point1.position.x, point1.position.y};
  frame.uv0 = {(static_cast<float>(rectangle.uv.at(0)) * K_UV_SCALE) + texture_offset_u,
      (static_cast<float>(rectangle.uv.at(1)) * K_UV_SCALE) + texture_offset_v};
  frame.uv1 = {(static_cast<float>(rectangle.uv.at(4)) * K_UV_SCALE) + texture_offset_u,
      (static_cast<float>(rectangle.uv.at(5)) * K_UV_SCALE) + texture_offset_v};
  frame.texture_index = rectangle.material_id;
  frame.width = width;
  frame.height = height;
  return frame;
}

}  // namespace App::Sprite
