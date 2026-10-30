#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/ViewFrustum.hpp"

namespace App {

/// Per-frame result of culling one decor's material groups.
struct DecorCullStats {
  std::size_t groups_total{0};
  std::size_t groups_submitted{0};
  std::size_t groups_culled{0};
};

/// Conservative, lossless visibility culling for 3DO decor objects.
///
/// Runtime.exe performs no object, portal or occlusion culling for decor: it rejects the whole
/// scene by a root-radius sphere against the view volume (0x0048D7F0), and rejects individual
/// polygons that fall outside the screen during submission. Everything inside the view volume is
/// drawn and resolved by the depth buffer. Culling here must therefore never change the image, so
/// only objects that provably contribute no pixels are skipped: those whose Runtime +0x58
/// broadphase sphere lies completely outside the camera frustum.
class DecorCulling {
 public:
  /// Recomputes the sphere of every object from its live runtime transform. Presentation space is
  /// GL.
  void rebuild(const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects);

  /// Recomputes only the spheres whose `changed` entry is non-zero. Falls back to rebuild() when
  /// the object count differs.
  void refresh(const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
      std::span<const std::uint8_t> changed);

  /// GL-space sphere that contains every vertex of the object, or nullopt when the object cannot be
  /// bounded safely (no radius, skinned, or no runtime state).
  [[nodiscard]] static std::optional<BoundingSphere> object_sphere(
      const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
      std::size_t object_index);

  /// True when the object may contribute pixels. Unbounded objects are always visible.
  [[nodiscard]] bool object_visible(std::size_t object_index, const ViewFrustum& frustum) const;

  [[nodiscard]] std::size_t object_count() const {
    return m_spheres.size();
  }

 private:
  std::vector<std::optional<BoundingSphere>> m_spheres;
};

}  // namespace App
