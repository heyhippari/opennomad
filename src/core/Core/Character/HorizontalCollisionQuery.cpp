#include "Core/Character/HorizontalCollisionQuery.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace App::Character {
namespace {

constexpr std::uint32_t K_HORIZONTAL_COLLISION_SKIP_MASK{0x00000041U};
constexpr std::uint32_t K_SPECIAL_HORIZONTAL_COLLISION_SKIP_MASK{0x20000000U};
constexpr float K_GEOMETRY_EPSILON{1.0e-5F};

struct Point2 {
  float x{0.0F};
  float z{0.0F};
};

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

[[nodiscard]] float dot(const Point2& first, const Point2& second) {
  return (first.x * second.x) + (first.z * second.z);
}

[[nodiscard]] Runtime::Vec3 normalize(const Runtime::Vec3& vector) {
  const float length{std::sqrt(dot(vector, vector))};
  if (length <= K_GEOMETRY_EPSILON) {
    return {};
  }
  return scale(vector, 1.0F / length);
}

[[nodiscard]] Point2 subtract(const Point2& first, const Point2& second) {
  return {.x = first.x - second.x, .z = first.z - second.z};
}

[[nodiscard]] Point2 add(const Point2& first, const Point2& second) {
  return {.x = first.x + second.x, .z = first.z + second.z};
}

[[nodiscard]] Point2 scale(const Point2& point, const float amount) {
  return {.x = point.x * amount, .z = point.z * amount};
}

[[nodiscard]] Point2 horizontal(const Runtime::Vec3& vector) {
  return {.x = vector.x, .z = vector.z};
}

[[nodiscard]] Runtime::Vec3 horizontal_normal(const Runtime::Vec3& vector) {
  return normalize({.x = vector.x, .y = 0.0F, .z = vector.z});
}

[[nodiscard]] bool valid_body(const HorizontalCollisionBody& body) {
  return std::isfinite(body.radius) && std::isfinite(body.top_y) && std::isfinite(body.bottom_y) &&
         body.radius > 0.0F && body.bottom_y >= body.top_y;
}

[[nodiscard]] std::optional<Runtime::Vec3> transform_normal(
    const Runtime::Vec3& normal, const Omikron::Model3DOData::RuntimeObjectState& object) {
  if (std::abs(object.scale.x) <= K_GEOMETRY_EPSILON ||
      std::abs(object.scale.y) <= K_GEOMETRY_EPSILON ||
      std::abs(object.scale.z) <= K_GEOMETRY_EPSILON) {
    return std::nullopt;
  }
  return normalize(Runtime::transform_vector({.x = normal.x / object.scale.x,
                                                 .y = normal.y / object.scale.y,
                                                 .z = normal.z / object.scale.z},
      object.world_matrix));
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
    const Runtime::Vec3 edge_cross{.x = (edge.y * offset.z) - (edge.z * offset.y),
        .y = (edge.z * offset.x) - (edge.x * offset.z),
        .z = (edge.x * offset.y) - (edge.y * offset.x)};
    if (dot(edge_cross, normal) < -K_GEOMETRY_EPSILON) {
      return false;
    }
  }
  return true;
}

struct Aabb {
  Runtime::Vec3 minimum{};
  Runtime::Vec3 maximum{};
};

[[nodiscard]] bool overlaps(const Aabb& first, const Aabb& second) {
  return first.minimum.x <= second.maximum.x && first.maximum.x >= second.minimum.x &&
         first.minimum.y <= second.maximum.y && first.maximum.y >= second.minimum.y &&
         first.minimum.z <= second.maximum.z && first.maximum.z >= second.minimum.z;
}

[[nodiscard]] Aabb swept_bounds(const Runtime::Vec3& origin,
    const Runtime::Vec3& direction,
    const float query_length,
    const HorizontalCollisionBody& body) {
  const Runtime::Vec3 end{add(origin, scale(direction, query_length))};
  const float horizontal_extent{
      body.radius + HorizontalCollisionQuery::K_HORIZONTAL_COLLISION_SKIN};
  return {.minimum = {.x = std::min(origin.x, end.x) - horizontal_extent,
              .y = origin.y + body.top_y,
              .z = std::min(origin.z, end.z) - horizontal_extent},
      .maximum = {.x = std::max(origin.x, end.x) + horizontal_extent,
          .y = origin.y + body.bottom_y,
          .z = std::max(origin.z, end.z) + horizontal_extent}};
}

[[nodiscard]] Aabb polygon_bounds(const std::span<const Runtime::Vec3> vertices) {
  Aabb result{.minimum = vertices.front(), .maximum = vertices.front()};
  for (const Runtime::Vec3& vertex : vertices.subspan(1U)) {
    result.minimum.x = std::min(result.minimum.x, vertex.x);
    result.minimum.y = std::min(result.minimum.y, vertex.y);
    result.minimum.z = std::min(result.minimum.z, vertex.z);
    result.maximum.x = std::max(result.maximum.x, vertex.x);
    result.maximum.y = std::max(result.maximum.y, vertex.y);
    result.maximum.z = std::max(result.maximum.z, vertex.z);
  }
  return result;
}

[[nodiscard]] bool object_outside_sweep(const Omikron::MeshDescriptor& mesh,
    const Omikron::Model3DOData::RuntimeObjectState& object,
    const Aabb& sweep) {
  if (mesh.bounding_radius <= 0.0F) {
    return false;
  }
  const Runtime::Vec3 local_center{.x = (mesh.bounds_min.x + mesh.bounds_max.x) * 0.5F,
      .y = (mesh.bounds_min.y + mesh.bounds_max.y) * 0.5F,
      .z = (mesh.bounds_min.z + mesh.bounds_max.z) * 0.5F};
  const Runtime::Vec3 center{Runtime::transform_point(local_center,
      {.matrix = object.world_matrix,
          .translation = object.world_translation,
          .scale = object.scale})};
  const float maximum_scale{
      std::max({std::abs(object.scale.x), std::abs(object.scale.y), std::abs(object.scale.z)})};
  const float radius{mesh.bounding_radius * maximum_scale};
  return !overlaps(sweep,
      {.minimum = {.x = center.x - radius, .y = center.y - radius, .z = center.z - radius},
          .maximum = {.x = center.x + radius, .y = center.y + radius, .z = center.z + radius}});
}

void retain_hit(const HorizontalCollisionHit& candidate,
    const float query_length,
    std::optional<HorizontalCollisionHit>& best) {
  if (!std::isfinite(candidate.travel_distance) ||
      candidate.travel_distance > query_length + K_GEOMETRY_EPSILON) {
    return;
  }
  if (!best.has_value() || candidate.travel_distance < best->travel_distance - K_GEOMETRY_EPSILON) {
    best = candidate;
  }
}

template <std::size_t Size>
void consider_face(const std::array<Runtime::Vec3, Size>& vertices,
    const Runtime::Vec3& normal,
    const std::size_t object_index,
    const Runtime::Vec3& origin,
    const Runtime::Vec3& direction,
    const float query_length,
    const HorizontalCollisionBody& body,
    std::optional<HorizontalCollisionHit>& best) {
  const Runtime::Vec3 response{horizontal_normal(normal)};
  const float denominator{dot(direction, normal)};
  if (dot(response, response) <= K_GEOMETRY_EPSILON) {
    return;
  }
  Runtime::Vec3 support{.x = origin.x - (response.x * body.radius),
      .y = origin.y + ((normal.y > K_GEOMETRY_EPSILON) ? body.top_y : body.bottom_y),
      .z = origin.z - (response.z * body.radius)};
  if (std::abs(normal.y) <= K_GEOMETRY_EPSILON) {
    support.y = origin.y + ((body.top_y + body.bottom_y) * 0.5F);
  }
  const float plane_distance{dot(normal, subtract(support, vertices.front()))};
  const Runtime::Vec3 current_contact{subtract(support, scale(normal, plane_distance))};
  if (plane_distance >= 0.0F &&
      plane_distance < HorizontalCollisionQuery::K_HORIZONTAL_COLLISION_SKIN &&
      point_in_polygon(current_contact, normal, vertices)) {
    retain_hit({.object_index = object_index,
                   .world_point = current_contact,
                   .world_normal = normal,
                   .travel_distance = plane_distance},
        query_length,
        best);
    return;
  }
  if (denominator >= -K_GEOMETRY_EPSILON) {
    return;
  }
  const float travel{-plane_distance / denominator};
  const Runtime::Vec3 contact{add(support, scale(direction, travel))};
  if (!point_in_polygon(contact, normal, vertices)) {
    return;
  }
  retain_hit({.object_index = object_index,
                 .world_point = contact,
                 .world_normal = normal,
                 .travel_distance = travel},
      query_length,
      best);
}

[[nodiscard]] std::optional<float> first_circle_root(const Point2& center,
    const Point2& direction,
    const Point2& point,
    const float radius,
    const float query_length) {
  const Point2 offset{subtract(center, point)};
  const float projection{dot(offset, direction)};
  const float discriminant{(projection * projection) - (dot(offset, offset) - (radius * radius))};
  if (discriminant < -K_GEOMETRY_EPSILON) {
    return std::nullopt;
  }
  const float root{-projection - std::sqrt(std::max(discriminant, 0.0F))};
  if (root > query_length + K_GEOMETRY_EPSILON) {
    return std::nullopt;
  }
  return root;
}

void consider_vertex(const Runtime::Vec3& vertex,
    const std::size_t object_index,
    const Runtime::Vec3& origin,
    const Runtime::Vec3& direction,
    const float query_length,
    const HorizontalCollisionBody& body,
    std::optional<HorizontalCollisionHit>& best) {
  if (vertex.y < origin.y + body.top_y - K_GEOMETRY_EPSILON ||
      vertex.y > origin.y + body.bottom_y + K_GEOMETRY_EPSILON) {
    return;
  }
  const auto travel{first_circle_root(
      horizontal(origin), horizontal(direction), horizontal(vertex), body.radius, query_length)};
  if (!travel.has_value()) {
    return;
  }
  const Runtime::Vec3 center{add(origin, scale(direction, travel.value()))};
  const Runtime::Vec3 response{horizontal_normal(subtract(center, vertex))};
  if (dot(response, response) <= K_GEOMETRY_EPSILON || dot(response, direction) >= 0.0F) {
    return;
  }
  retain_hit({.object_index = object_index,
                 .world_point = vertex,
                 .world_normal = response,
                 .travel_distance = travel.value()},
      query_length,
      best);
}

void consider_edge(const Runtime::Vec3& first,
    const Runtime::Vec3& second,
    const std::size_t object_index,
    const Runtime::Vec3& origin,
    const Runtime::Vec3& direction,
    const float query_length,
    const HorizontalCollisionBody& body,
    std::optional<HorizontalCollisionHit>& best) {
  const Point2 edge{subtract(horizontal(second), horizontal(first))};
  const float edge_length_squared{dot(edge, edge)};
  if (edge_length_squared <= K_GEOMETRY_EPSILON) {
    const float minimum_y{std::min(first.y, second.y)};
    const float maximum_y{std::max(first.y, second.y)};
    if (maximum_y < origin.y + body.top_y || minimum_y > origin.y + body.bottom_y) {
      return;
    }
    Runtime::Vec3 representative{first};
    representative.y = std::clamp(origin.y, minimum_y, maximum_y);
    consider_vertex(representative, object_index, origin, direction, query_length, body, best);
    return;
  }

  const float edge_length{std::sqrt(edge_length_squared)};
  const Point2 line_normal{.x = -edge.z / edge_length, .z = edge.x / edge_length};
  const Point2 center_offset{subtract(horizontal(origin), horizontal(first))};
  const float speed{dot(horizontal(direction), line_normal)};
  if (std::abs(speed) <= K_GEOMETRY_EPSILON) {
    return;
  }
  for (const float side : {-1.0F, 1.0F}) {
    const float travel{((side * body.radius) - dot(center_offset, line_normal)) / speed};
    if (travel > query_length + K_GEOMETRY_EPSILON) {
      continue;
    }
    const Point2 center{add(horizontal(origin), scale(horizontal(direction), travel))};
    const float edge_parameter{
        dot(subtract(center, horizontal(first)), edge) / edge_length_squared};
    if (edge_parameter <= K_GEOMETRY_EPSILON || edge_parameter >= 1.0F - K_GEOMETRY_EPSILON) {
      continue;
    }
    const float edge_y{first.y + ((second.y - first.y) * edge_parameter)};
    if (edge_y < origin.y + body.top_y - K_GEOMETRY_EPSILON ||
        edge_y > origin.y + body.bottom_y + K_GEOMETRY_EPSILON) {
      continue;
    }
    const Point2 edge_point{add(horizontal(first), scale(edge, edge_parameter))};
    const Runtime::Vec3 response{
        horizontal_normal({.x = center.x - edge_point.x, .y = 0.0F, .z = center.z - edge_point.z})};
    if (dot(response, direction) >= 0.0F) {
      continue;
    }
    retain_hit({.object_index = object_index,
                   .world_point = {.x = edge_point.x, .y = edge_y, .z = edge_point.z},
                   .world_normal = response,
                   .travel_distance = travel},
        query_length,
        best);
  }
}

template <std::size_t Size>
void consider_polygon(const std::array<Runtime::Vec3, Size>& vertices,
    const Runtime::Vec3& normal,
    const std::size_t object_index,
    const Runtime::Vec3& origin,
    const Runtime::Vec3& direction,
    const float query_length,
    const HorizontalCollisionBody& body,
    const Aabb& sweep,
    std::optional<HorizontalCollisionHit>& best) {
  const Runtime::Vec3 response{horizontal_normal(normal)};
  const bool approaching{dot(direction, normal) < -HorizontalCollisionQuery::K_MOVEMENT_THRESHOLD};
  const float active_side{dot(normal, subtract(origin, vertices.front()))};
  if (dot(response, response) <= K_GEOMETRY_EPSILON || active_side < -K_GEOMETRY_EPSILON ||
      (!approaching &&
          active_side >= body.radius + HorizontalCollisionQuery::K_HORIZONTAL_COLLISION_SKIN) ||
      !overlaps(sweep, polygon_bounds(vertices))) {
    return;
  }
  consider_face(vertices, normal, object_index, origin, direction, query_length, body, best);
  if (!approaching) {
    return;
  }
  for (std::size_t index{0}; index < vertices.size(); ++index) {
    consider_edge(vertices[index],
        vertices[(index + 1U) % vertices.size()],
        object_index,
        origin,
        direction,
        query_length,
        body,
        best);
  }
  for (const Runtime::Vec3& vertex : vertices) {
    consider_vertex(vertex, object_index, origin, direction, query_length, body, best);
  }
}

}  // namespace

std::optional<HorizontalCollisionHit> HorizontalCollisionQuery::find(
    const Omikron::Model3DOData& model,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
    const Runtime::Vec3& origin,
    const Runtime::Vec3& direction,
    const float query_length,
    const HorizontalCollisionBody& body) {
  if (!valid_body(body) || query_length < 0.0F) {
    return std::nullopt;
  }
  const Aabb sweep{swept_bounds(origin, direction, query_length, body)};
  std::optional<HorizontalCollisionHit> best;
  const std::size_t object_count{
      std::min({model.meshes.size(), model.polygons.size(), runtime_objects.size()})};
  for (std::size_t object_index{0}; object_index < object_count; ++object_index) {
    const Omikron::MeshDescriptor& mesh{model.meshes.at(object_index)};
    if ((mesh.flags & K_HORIZONTAL_COLLISION_SKIP_MASK) != 0U ||
        (mesh.flags & K_SPECIAL_HORIZONTAL_COLLISION_SKIP_MASK) != 0U) {
      continue;
    }
    const auto& object{runtime_objects.subspan(object_index, 1U).front()};
    if (object_outside_sweep(mesh, object, sweep)) {
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
      const auto normal{transform_normal(triangle.face_normal, object)};
      if (valid && normal.has_value()) {
        consider_polygon(vertices,
            normal.value(),
            object_index,
            origin,
            direction,
            query_length,
            body,
            sweep,
            best);
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
        consider_polygon(vertices,
            normal.value(),
            object_index,
            origin,
            direction,
            query_length,
            body,
            sweep,
            best);
      }
    }
  }
  return best;
}

HorizontalResolveResult HorizontalCollisionQuery::resolve(const Omikron::Model3DOData& model,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
    const Runtime::Vec3& origin,
    const Runtime::Vec3& desired,
    const HorizontalCollisionBody& body) {
  HorizontalResolveResult result;
  Runtime::Vec3 current{origin};
  Runtime::Vec3 remaining{.x = desired.x, .y = 0.0F, .z = desired.z};
  float retry_push{0.0F};

  while (result.collision_passes < K_MAX_HORIZONTAL_COLLISION_PASSES) {
    const float remaining_length{std::sqrt(dot(remaining, remaining))};
    if (remaining_length < K_MOVEMENT_THRESHOLD) {
      break;
    }
    const Runtime::Vec3 direction{scale(remaining, 1.0F / remaining_length)};
    const auto hit{find(model,
        runtime_objects,
        current,
        direction,
        remaining_length + K_HORIZONTAL_COLLISION_LOOKAHEAD,
        body)};
    if (!hit.has_value() || hit->travel_distance > remaining_length) {
      current = add(current, remaining);
      remaining = {};
      break;
    }
    result.last_hit = hit;
    const Runtime::Vec3 response{horizontal_normal(hit->world_normal)};
    if (hit->travel_distance < K_HORIZONTAL_COLLISION_SKIN) {
      if (result.depenetration_iterations >= K_MAX_DEPENETRATION_ITERATIONS) {
        result.depenetration_limit_reached = true;
        break;
      }
      retry_push = retry_push == 0.0F ? K_HORIZONTAL_COLLISION_SKIN - hit->travel_distance
                                      : retry_push * K_DEPENETRATION_RETRY_SCALE;
      current = add(current, scale(response, retry_push));
      result.depenetrated = true;
      ++result.depenetration_iterations;
      continue;
    }

    retry_push = 0.0F;
    const float allowed_travel{hit->travel_distance - K_HORIZONTAL_COLLISION_SKIN};
    current = add(current, scale(direction, allowed_travel));
    const float unresolved_distance{remaining_length - allowed_travel};
    const float inward{std::max(-dot(response, direction), 0.0F)};
    remaining =
        add(scale(direction, unresolved_distance), scale(response, inward * unresolved_distance));
    result.forward_collision = true;
    ++result.collision_passes;
  }

  result.resolved_displacement = subtract(current, origin);
  result.resolved_displacement.y = 0.0F;
  return result;
}

}  // namespace App::Character