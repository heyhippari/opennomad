#include "Core/DecorCulling.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/RuntimePresentation.hpp"
#include "Core/ViewFrustum.hpp"

namespace App {

std::optional<BoundingSphere> DecorCulling::object_sphere(const Omikron::Model3DOData& model,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
    const std::size_t object_index) {
  if (object_index >= model.meshes.size() || object_index >= runtime_objects.size() ||
      object_index >= model.skin_parent_index.size()) {
    return std::nullopt;
  }
  // Skinned meshes may resolve corners against a parent's vertex block, which lies outside this
  // object's own sphere.
  if (model.skin_parent_index.at(object_index) >= 0) {
    return std::nullopt;
  }
  const Omikron::MeshDescriptor& mesh{model.meshes.at(object_index)};
  if (!(mesh.bounding_radius > 0.0F) || !std::isfinite(mesh.bounding_radius)) {
    return std::nullopt;
  }
  const Omikron::Model3DOData::RuntimeObjectState& object{runtime_objects[object_index]};
  const Runtime::Vec3 local_center{.x = (mesh.bounds_min.x + mesh.bounds_max.x) * 0.5F,
      .y = (mesh.bounds_min.y + mesh.bounds_max.y) * 0.5F,
      .z = (mesh.bounds_min.z + mesh.bounds_max.z) * 0.5F};
  const Runtime::Vec3 world_center{Runtime::transform_point(local_center,
      {.matrix = object.world_matrix,
          .translation = object.world_translation,
          .scale = object.scale})};
  const float maximum_scale{
      std::max({std::abs(object.scale.x), std::abs(object.scale.y), std::abs(object.scale.z)})};
  const float radius{mesh.bounding_radius * maximum_scale};
  const std::array<float, 3> center{Runtime::Presentation::to_gl(
      std::array<float, 3>{world_center.x, world_center.y, world_center.z})};
  if (!std::isfinite(radius) || !std::isfinite(center.at(0)) || !std::isfinite(center.at(1)) ||
      !std::isfinite(center.at(2))) {
    return std::nullopt;
  }
  return BoundingSphere{.center = center, .radius = radius};
}

void DecorCulling::rebuild(const Omikron::Model3DOData& model,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects) {
  m_spheres.assign(runtime_objects.size(), std::nullopt);
  for (std::size_t index{0}; index < runtime_objects.size(); ++index) {
    m_spheres.at(index) = object_sphere(model, runtime_objects, index);
  }
}

void DecorCulling::refresh(const Omikron::Model3DOData& model,
    const std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
    const std::span<const std::uint8_t> changed) {
  if (m_spheres.size() != runtime_objects.size() || changed.size() != runtime_objects.size()) {
    rebuild(model, runtime_objects);
    return;
  }
  for (std::size_t index{0}; index < runtime_objects.size(); ++index) {
    if (changed[index] != 0U) {
      m_spheres.at(index) = object_sphere(model, runtime_objects, index);
    }
  }
}

bool DecorCulling::object_visible(
    const std::size_t object_index, const ViewFrustum& frustum) const {
  if (object_index >= m_spheres.size()) {
    return true;
  }
  const std::optional<BoundingSphere>& sphere{m_spheres.at(object_index)};
  return !sphere.has_value() || frustum.intersects(sphere.value());
}

}  // namespace App
