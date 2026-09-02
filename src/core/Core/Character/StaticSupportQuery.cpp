#include "Core/Character/StaticSupportQuery.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace App::Character {
namespace {

constexpr std::uint32_t K_STATIC_SUPPORT_SKIP_MASK{0x00000041U};
constexpr std::uint32_t K_TRANSFORMED_COLLISION_MASK{0x00080000U};
constexpr float K_MINIMUM_FLOOR_NORMAL_Y{-0.0001F};
constexpr float K_GEOMETRY_EPSILON{1.0e-5F};

[[nodiscard]] float dot(const Runtime::Vec3& first, const Runtime::Vec3& second) {
  return (first.x * second.x) + (first.y * second.y) + (first.z * second.z);
}

[[nodiscard]] Runtime::Vec3 normalize(const Runtime::Vec3& vector) {
  const float length{std::sqrt(dot(vector, vector))};
  if (length <= std::numeric_limits<float>::epsilon()) {
    return {};
  }
  return {.x = vector.x / length, .y = vector.y / length, .z = vector.z / length};
}

[[nodiscard]] bool outside_horizontal_bounds(const Omikron::MeshDescriptor& mesh,
    const Omikron::Model3DOData::RuntimeObjectState& object,
    const StaticSupportQueryInput& query) {
  if (mesh.bounding_radius <= 0.0F) {
    return false;
  }
  const Runtime::Vec3 local_center{.x = (mesh.bounds_min.x + mesh.bounds_max.x) * 0.5F,
      .y = (mesh.bounds_min.y + mesh.bounds_max.y) * 0.5F,
      .z = (mesh.bounds_min.z + mesh.bounds_max.z) * 0.5F};
  const Runtime::Transform transform{.matrix = object.world_matrix,
      .translation = object.world_translation,
      .scale = object.scale};
  const Runtime::Vec3 world_center{Runtime::transform_point(local_center, transform)};
  const float maximum_scale{
      std::max({std::abs(object.scale.x), std::abs(object.scale.y), std::abs(object.scale.z)})};
  const float horizontal_extent{query.radius + (mesh.bounding_radius * maximum_scale)};
  return std::abs(query.world_probe.x - world_center.x) > horizontal_extent ||
         std::abs(query.world_probe.z - world_center.z) > horizontal_extent;
}

struct Point2 {
  float x{0.0F};
  float y{0.0F};
};

[[nodiscard]] Point2 project(const Runtime::Vec3& point, const Runtime::Vec3& normal) {
  const float absolute_x{std::abs(normal.x)};
  const float absolute_y{std::abs(normal.y)};
  const float absolute_z{std::abs(normal.z)};
  if (absolute_x >= absolute_y && absolute_x >= absolute_z) {
    return {.x = point.y, .y = point.z};
  }
  if (absolute_y >= absolute_z) {
    return {.x = point.x, .y = point.z};
  }
  return {.x = point.x, .y = point.y};
}

[[nodiscard]] bool point_on_segment(
    const Point2& point, const Point2& first, const Point2& second) {
  const float cross{
      ((point.y - first.y) * (second.x - first.x)) - ((point.x - first.x) * (second.y - first.y))};
  if (std::abs(cross) > K_GEOMETRY_EPSILON) {
    return false;
  }
  return point.x >= std::min(first.x, second.x) - K_GEOMETRY_EPSILON &&
         point.x <= std::max(first.x, second.x) + K_GEOMETRY_EPSILON &&
         point.y >= std::min(first.y, second.y) - K_GEOMETRY_EPSILON &&
         point.y <= std::max(first.y, second.y) + K_GEOMETRY_EPSILON;
}

[[nodiscard]] bool contains_projected(const Runtime::Vec3& point,
    const Runtime::Vec3& normal,
    const std::span<const Runtime::Vec3> vertices) {
  const Point2 projected_point{project(point, normal)};
  bool inside{false};
  for (std::size_t index{0}, previous{vertices.size() - 1U}; index < vertices.size();
      previous = index++) {
    const Point2 first{project(vertices.subspan(previous, 1U).front(), normal)};
    const Point2 second{project(vertices.subspan(index, 1U).front(), normal)};
    if (point_on_segment(projected_point, first, second)) {
      return true;
    }
    const bool crosses{(first.y > projected_point.y) != (second.y > projected_point.y)};
    if (crosses) {
      const float intersection_x{
          first.x + ((projected_point.y - first.y) * (second.x - first.x) / (second.y - first.y))};
      if (projected_point.x < intersection_x) {
        inside = !inside;
      }
    }
  }
  return inside;
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
  const auto& object{runtime_objects.subspan(owner_index, 1U).front()};
  const Runtime::Transform transform{.matrix = object.world_matrix,
      .translation = object.world_translation,
      .scale = object.scale};
  return Runtime::transform_point(
      model.vertices.at(owner.vertex_base + local_index).position, transform);
}

template <std::size_t Size>
void consider_face(const std::array<Runtime::Vec3, Size>& vertices,
    const Runtime::Vec3& authored_normal,
    const Omikron::Model3DOData::RuntimeObjectState& object,
    const std::size_t object_index,
    const StaticSupportQueryInput& query,
    std::optional<StaticSupportHit>& best) {
  const Runtime::Vec3 world_normal{
      normalize(Runtime::transform_vector(authored_normal, object.world_matrix))};
  if (world_normal.y >= K_MINIMUM_FLOOR_NORMAL_Y) {
    return;
  }
  const float plane_d{-dot(world_normal, vertices.front())};
  const float plane_side{dot(world_normal, query.world_probe) + plane_d};
  const float vertical_distance{-plane_side / world_normal.y};
  if (vertical_distance < -K_GEOMETRY_EPSILON) {
    return;
  }
  const Runtime::Vec3 point{.x = query.world_probe.x,
      .y = query.world_probe.y + vertical_distance,
      .z = query.world_probe.z};
  if (!contains_projected(point, world_normal, vertices)) {
    return;
  }
  const float clearance{vertical_distance - query.radius};
  if (!best.has_value() || clearance < best->clearance) {
    best = StaticSupportHit{.object_index = object_index,
        .world_point = point,
        .world_normal = world_normal,
        .clearance = clearance};
  }
}

}  // namespace

std::optional<StaticSupportHit> StaticSupportQuery::find(const Omikron::Model3DOData& model,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
    const StaticSupportQueryInput& query) {
  std::optional<StaticSupportHit> best;
  const std::size_t object_count{
      std::min({model.meshes.size(), model.polygons.size(), runtime_objects.size()})};
  for (std::size_t object_index{0}; object_index < object_count; ++object_index) {
    const Omikron::MeshDescriptor& mesh{model.meshes.at(object_index)};
    if ((mesh.flags & K_STATIC_SUPPORT_SKIP_MASK) != 0U ||
        (mesh.flags & K_TRANSFORMED_COLLISION_MASK) != 0U) {
      continue;
    }
    const auto& object{runtime_objects.subspan(object_index, 1U).front()};
    if (outside_horizontal_bounds(mesh, object, query)) {
      continue;
    }
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
      if (valid) {
        consider_face(vertices, triangle.face_normal, object, object_index, query, best);
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
      if (valid) {
        consider_face(vertices, rectangle.face_normal, object, object_index, query, best);
      }
    }
  }
  return best;
}

}  // namespace App::Character