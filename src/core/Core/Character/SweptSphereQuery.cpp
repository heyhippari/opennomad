#include "Core/Character/SweptSphereQuery.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>

namespace App::Character {
namespace {

constexpr float K_GEOMETRY_EPSILON{1.0e-5F};

[[nodiscard]] Runtime::Vec3 add(const Runtime::Vec3& first, const Runtime::Vec3& second) {
  return {.x = first.x + second.x, .y = first.y + second.y, .z = first.z + second.z};
}

[[nodiscard]] Runtime::Vec3 subtract(const Runtime::Vec3& first, const Runtime::Vec3& second) {
  return {.x = first.x - second.x, .y = first.y - second.y, .z = first.z - second.z};
}

[[nodiscard]] Runtime::Vec3 scale(const Runtime::Vec3& vector, const float amount) {
  return {.x = vector.x * amount, .y = vector.y * amount, .z = vector.z * amount};
}

[[nodiscard]] float dot(const Runtime::Vec3& first, const Runtime::Vec3& second) {
  return (first.x * second.x) + (first.y * second.y) + (first.z * second.z);
}

[[nodiscard]] Runtime::Vec3 normalize(const Runtime::Vec3& vector) {
  const float length_squared{dot(vector, vector)};
  if (length_squared <= K_GEOMETRY_EPSILON * K_GEOMETRY_EPSILON) {
    return {};
  }
  return scale(vector, 1.0F / std::sqrt(length_squared));
}

[[nodiscard]] std::optional<Runtime::Vec3> transform_normal(
    const Runtime::Vec3& normal, const Omikron::Model3DOData::RuntimeObjectState& object) {
  if (std::abs(object.scale.x) <= K_GEOMETRY_EPSILON ||
      std::abs(object.scale.y) <= K_GEOMETRY_EPSILON ||
      std::abs(object.scale.z) <= K_GEOMETRY_EPSILON) {
    return std::nullopt;
  }
  const Runtime::Vec3 transformed{
      normalize(Runtime::transform_vector({.x = normal.x / object.scale.x,
                                              .y = normal.y / object.scale.y,
                                              .z = normal.z / object.scale.z},
          object.world_matrix))};
  return dot(transformed, transformed) > K_GEOMETRY_EPSILON ? std::optional{transformed}
                                                            : std::nullopt;
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

void retain(const SweptSphereHit& candidate,
    const SweptSphereQueryInput& query,
    std::optional<SweptSphereHit>& best) {
  if (!std::isfinite(candidate.travel_distance) ||
      candidate.travel_distance < -K_GEOMETRY_EPSILON ||
      candidate.travel_distance > query.max_distance + K_GEOMETRY_EPSILON) {
    return;
  }
  if (!best.has_value() || candidate.travel_distance < best->travel_distance - K_GEOMETRY_EPSILON) {
    SweptSphereHit retained{candidate};
    retained.travel_distance = std::max(retained.travel_distance, 0.0F);
    best = retained;
  }
}

[[nodiscard]] std::optional<float> ray_sphere_root(const Runtime::Vec3& start,
    const Runtime::Vec3& direction,
    const Runtime::Vec3& center,
    const float radius) {
  const Runtime::Vec3 offset{subtract(start, center)};
  const float projection{dot(offset, direction)};
  const float constant{dot(offset, offset) - (radius * radius)};
  if (constant <= K_GEOMETRY_EPSILON) {
    return 0.0F;
  }
  const float discriminant{(projection * projection) - constant};
  if (discriminant < -K_GEOMETRY_EPSILON) {
    return std::nullopt;
  }
  const float root{-projection - std::sqrt(std::max(discriminant, 0.0F))};
  return root >= -K_GEOMETRY_EPSILON ? std::optional{std::max(root, 0.0F)} : std::nullopt;
}

void consider_vertex(const Runtime::Vec3& vertex,
    const Runtime::Vec3& polygon_origin,
    const Runtime::Vec3& polygon_normal,
    const std::size_t object_index,
    const SweptSphereQueryInput& query,
    std::optional<SweptSphereHit>& best) {
  const auto travel{ray_sphere_root(query.start, query.direction, vertex, query.radius)};
  if (!travel.has_value()) {
    return;
  }
  const Runtime::Vec3 center{add(query.start, scale(query.direction, travel.value()))};
  if (dot(polygon_normal, subtract(center, polygon_origin)) < -K_GEOMETRY_EPSILON) {
    return;
  }
  const Runtime::Vec3 response{normalize(subtract(center, vertex))};
  if (dot(response, response) <= K_GEOMETRY_EPSILON ||
      (travel.value() > K_GEOMETRY_EPSILON && dot(response, query.direction) >= 0.0F)) {
    return;
  }
  retain({.object_index = object_index,
             .world_point = vertex,
             .world_normal = response,
             .travel_distance = travel.value()},
      query,
      best);
}

void consider_edge(const Runtime::Vec3& first,
    const Runtime::Vec3& second,
    const Runtime::Vec3& polygon_origin,
    const Runtime::Vec3& polygon_normal,
    const std::size_t object_index,
    const SweptSphereQueryInput& query,
    std::optional<SweptSphereHit>& best) {
  const Runtime::Vec3 edge{
      .x = second.x - first.x, .y = second.y - first.y, .z = second.z - first.z};
  const Runtime::Vec3 origin_offset{subtract(query.start, first)};
  const float edge_squared{dot(edge, edge)};
  if (edge_squared <= K_GEOMETRY_EPSILON) {
    return;
  }

  const float edge_direction{dot(edge, query.direction)};
  const float edge_origin{dot(edge, origin_offset)};
  const float coefficient_a{edge_squared - (edge_direction * edge_direction)};
  const float coefficient_b{
      (edge_squared * dot(origin_offset, query.direction)) - (edge_origin * edge_direction)};
  const float coefficient_c{(edge_squared * dot(origin_offset, origin_offset)) -
                            (edge_origin * edge_origin) -
                            ((query.radius * query.radius) * edge_squared)};

  float travel{0.0F};
  if (coefficient_c > K_GEOMETRY_EPSILON) {
    const float discriminant{(coefficient_b * coefficient_b) - (coefficient_a * coefficient_c)};
    if (coefficient_a <= K_GEOMETRY_EPSILON || discriminant < -K_GEOMETRY_EPSILON) {
      return;
    }
    travel = (-coefficient_b - std::sqrt(std::max(discriminant, 0.0F))) / coefficient_a;
  }
  if (travel < -K_GEOMETRY_EPSILON) {
    return;
  }
  travel = std::max(travel, 0.0F);
  const float edge_parameter{(edge_origin + (travel * edge_direction)) / edge_squared};
  if (edge_parameter <= K_GEOMETRY_EPSILON || edge_parameter >= 1.0F - K_GEOMETRY_EPSILON) {
    return;
  }
  const Runtime::Vec3 point{add(first, scale(edge, edge_parameter))};
  const Runtime::Vec3 center{add(query.start, scale(query.direction, travel))};
  if (dot(polygon_normal, subtract(center, polygon_origin)) < -K_GEOMETRY_EPSILON) {
    return;
  }
  const Runtime::Vec3 response{normalize(subtract(center, point))};
  if (dot(response, response) <= K_GEOMETRY_EPSILON ||
      (travel > K_GEOMETRY_EPSILON && dot(response, query.direction) >= 0.0F)) {
    return;
  }
  retain({.object_index = object_index,
             .world_point = point,
             .world_normal = response,
             .travel_distance = travel},
      query,
      best);
}

template <std::size_t Size>
void consider_polygon(const std::array<Runtime::Vec3, Size>& vertices,
    const Runtime::Vec3& normal,
    const std::size_t object_index,
    const SweptSphereQueryInput& query,
    std::optional<SweptSphereHit>& best) {
  const float start_side{dot(normal, subtract(query.start, vertices.front()))};
  if (start_side < -K_GEOMETRY_EPSILON) {
    return;
  }

  if (start_side <= query.radius + K_GEOMETRY_EPSILON) {
    const Runtime::Vec3 projected{subtract(query.start, scale(normal, start_side))};
    if (point_in_polygon(projected, normal, vertices)) {
      retain({.object_index = object_index,
                 .world_point = projected,
                 .world_normal = normal,
                 .travel_distance = 0.0F},
          query,
          best);
    }
  } else {
    const float normal_speed{dot(normal, query.direction)};
    if (normal_speed < -K_GEOMETRY_EPSILON) {
      const float travel{(query.radius - start_side) / normal_speed};
      const Runtime::Vec3 center{add(query.start, scale(query.direction, travel))};
      const Runtime::Vec3 point{subtract(center, scale(normal, query.radius))};
      if (point_in_polygon(point, normal, vertices)) {
        retain({.object_index = object_index,
                   .world_point = point,
                   .world_normal = normal,
                   .travel_distance = travel},
            query,
            best);
      }
    }
  }

  for (std::size_t index{0}; index < vertices.size(); ++index) {
    consider_edge(vertices[index],
        vertices[(index + 1U) % vertices.size()],
        vertices.front(),
        normal,
        object_index,
        query,
        best);
  }
  for (const Runtime::Vec3& vertex : vertices) {
    consider_vertex(vertex, vertices.front(), normal, object_index, query, best);
  }
}

}  // namespace

std::optional<SweptSphereHit> SweptSphereQuery::find(const Omikron::Model3DOData& model,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
    const SweptSphereQueryInput& query,
    const std::uint32_t excluded_object_flags) {
  std::optional<SweptSphereHit> best;
  const std::size_t object_count{
      std::min({model.meshes.size(), model.polygons.size(), runtime_objects.size()})};
  for (std::size_t object_index{0}; object_index < object_count; ++object_index) {
    if ((model.meshes.at(object_index).flags & excluded_object_flags) != 0U) {
      continue;
    }
    const auto hit{find_in_object(model, runtime_objects, object_index, query)};
    if (hit.has_value() && (!best.has_value() || hit->travel_distance < best->travel_distance)) {
      best = hit;
    }
  }
  return best;
}

std::optional<SweptSphereHit> SweptSphereQuery::find_in_object(const Omikron::Model3DOData& model,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
    const std::size_t object_index,
    const SweptSphereQueryInput& query) {
  if (object_index >= model.meshes.size() || object_index >= model.polygons.size() ||
      object_index >= runtime_objects.size() || !std::isfinite(query.max_distance) ||
      !std::isfinite(query.radius) || query.max_distance < 0.0F || query.radius < 0.0F ||
      std::abs(dot(query.direction, query.direction) - 1.0F) > K_GEOMETRY_EPSILON) {
    return std::nullopt;
  }

  const auto& object{runtime_objects[object_index]};
  std::optional<SweptSphereHit> best;
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
      consider_polygon(vertices, normal.value(), object_index, query, best);
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
      consider_polygon(vertices, normal.value(), object_index, query, best);
    }
  }
  return best;
}

}  // namespace App::Character
