#include "Core/Character/SecondarySupportQuery.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace App::Character {
namespace {

constexpr std::uint32_t K_SUPPORT_SKIP_MASK{0x00000041U};
constexpr float K_MINIMUM_FLOOR_NORMAL_Y{-0.0001F};
constexpr float K_GEOMETRY_EPSILON{1.0e-5F};

[[nodiscard]] Runtime::Vec3 subtract(const Runtime::Vec3& first, const Runtime::Vec3& second) {
  return {.x = first.x - second.x, .y = first.y - second.y, .z = first.z - second.z};
}

[[nodiscard]] float dot(const Runtime::Vec3& first, const Runtime::Vec3& second) {
  return (first.x * second.x) + (first.y * second.y) + (first.z * second.z);
}

[[nodiscard]] Runtime::Vec3 normalize(const Runtime::Vec3& vector) {
  const float length_squared{dot(vector, vector)};
  if (length_squared <= K_GEOMETRY_EPSILON * K_GEOMETRY_EPSILON) {
    return {};
  }
  const float inverse_length{1.0F / std::sqrt(length_squared)};
  return {.x = vector.x * inverse_length,
      .y = vector.y * inverse_length,
      .z = vector.z * inverse_length};
}

[[nodiscard]] std::optional<Runtime::Vec3> transform_normal(
    const Runtime::Vec3& normal, const Omikron::Model3DOData::RuntimeObjectState& object) {
  if (std::abs(object.scale.x) <= K_GEOMETRY_EPSILON ||
      std::abs(object.scale.y) <= K_GEOMETRY_EPSILON ||
      std::abs(object.scale.z) <= K_GEOMETRY_EPSILON) {
    return std::nullopt;
  }
  const Runtime::Vec3 result{
      normalize(Runtime::transform_vector({.x = normal.x / object.scale.x,
                                              .y = normal.y / object.scale.y,
                                              .z = normal.z / object.scale.z},
          object.world_matrix))};
  return dot(result, result) > K_GEOMETRY_EPSILON ? std::optional{result} : std::nullopt;
}

[[nodiscard]] std::optional<Runtime::Vec3> resolve_vertex(const Omikron::Model3DOData& model,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
    const std::size_t owner_index,
    const std::size_t local_index) {
  if (owner_index >= model.meshes.size() || owner_index >= runtime_objects.size()) {
    return std::nullopt;
  }
  const Omikron::MeshDescriptor& owner{model.meshes.at(owner_index)};
  if (local_index >= owner.vertex_count ||
      owner.vertex_base + local_index >= model.vertices.size()) {
    return std::nullopt;
  }
  const auto& object{runtime_objects[owner_index]};
  return Runtime::transform_point(model.vertices.at(owner.vertex_base + local_index).position,
      {.matrix = object.world_matrix,
          .translation = object.world_translation,
          .scale = object.scale});
}

[[nodiscard]] bool point_in_polygon(const Runtime::Vec3& point,
    const Runtime::Vec3& normal,
    const std::span<const Runtime::Vec3> vertices) {
  for (std::size_t index{0}; index < vertices.size(); ++index) {
    const Runtime::Vec3 edge{subtract(vertices[(index + 1U) % vertices.size()], vertices[index])};
    const Runtime::Vec3 offset{subtract(point, vertices[index])};
    const Runtime::Vec3 cross{.x = (edge.y * offset.z) - (edge.z * offset.y),
        .y = (edge.z * offset.x) - (edge.x * offset.z),
        .z = (edge.x * offset.y) - (edge.y * offset.x)};
    if (dot(cross, normal) < -K_GEOMETRY_EPSILON) {
      return false;
    }
  }
  return true;
}

template <std::size_t Size>
void consider_polygon(const std::array<Runtime::Vec3, Size>& vertices,
    const Runtime::Vec3& normal,
    const std::size_t object_index,
    const Runtime::Vec3& origin,
    std::optional<SecondarySupportHit>& best) {
  if (normal.y >= K_MINIMUM_FLOOR_NORMAL_Y) {
    return;
  }
  const float distance{-dot(normal, subtract(origin, vertices.front())) / normal.y};
  if (distance < -K_GEOMETRY_EPSILON) {
    return;
  }
  const Runtime::Vec3 point{.x = origin.x, .y = origin.y + distance, .z = origin.z};
  if (!point_in_polygon(point, normal, vertices)) {
    return;
  }
  if (!best.has_value() || distance < best->distance - K_GEOMETRY_EPSILON) {
    best = SecondarySupportHit{
        .object_index = object_index, .world_normal = normal, .distance = std::max(distance, 0.0F)};
  }
}

}  // namespace

std::optional<SecondarySupportHit> SecondarySupportQuery::find(const Omikron::Model3DOData& model,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
    const Runtime::Vec3& world_origin) {
  std::optional<SecondarySupportHit> best;
  const std::size_t object_count{
      std::min({model.meshes.size(), model.polygons.size(), runtime_objects.size()})};
  for (std::size_t object_index{0}; object_index < object_count; ++object_index) {
    const Omikron::MeshDescriptor& mesh{model.meshes.at(object_index)};
    if ((mesh.flags & K_SUPPORT_SKIP_MASK) != 0U) {
      continue;
    }
    const auto& object{runtime_objects[object_index]};
    const Omikron::MeshPolygons& polygons{model.polygons.at(object_index)};
    for (const Omikron::Triangle& triangle : polygons.triangles) {
      std::array<Runtime::Vec3, 3> vertices{};
      bool valid{true};
      for (std::size_t corner{0}; corner < vertices.size(); ++corner) {
        const Omikron::TriangleVertexRef& reference{triangle.vertices.at(corner)};
        std::size_t owner_index{object_index};
        if (reference.parented) {
          if (object_index >= model.skin_parent_index.size() ||
              model.skin_parent_index.at(object_index) < 0) {
            valid = false;
            break;
          }
          owner_index = static_cast<std::size_t>(model.skin_parent_index.at(object_index));
        }
        const auto vertex{resolve_vertex(model, runtime_objects, owner_index, reference.index)};
        if (!vertex.has_value()) {
          valid = false;
          break;
        }
        vertices.at(corner) = vertex.value();
      }
      const auto normal{transform_normal(triangle.face_normal, object)};
      if (valid && normal.has_value()) {
        consider_polygon(vertices, normal.value(), object_index, world_origin, best);
      }
    }
    for (const Omikron::Rectangle& rectangle : polygons.rectangles) {
      std::array<Runtime::Vec3, 4> vertices{};
      bool valid{true};
      for (std::size_t corner{0}; corner < vertices.size(); ++corner) {
        const auto vertex{
            resolve_vertex(model, runtime_objects, object_index, rectangle.vertices.at(corner))};
        if (!vertex.has_value()) {
          valid = false;
          break;
        }
        vertices.at(corner) = vertex.value();
      }
      const auto normal{transform_normal(rectangle.face_normal, object)};
      if (valid && normal.has_value()) {
        consider_polygon(vertices, normal.value(), object_index, world_origin, best);
      }
    }
  }
  return best;
}

}  // namespace App::Character
