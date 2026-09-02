#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Character {

struct HorizontalCollisionBody {
  float radius{0.0F};
  float top_y{0.0F};
  float bottom_y{0.0F};
};

struct HorizontalCollisionHit {
  std::size_t object_index{0};
  App::Runtime::Vec3 world_point{};
  App::Runtime::Vec3 world_normal{};
  float travel_distance{0.0F};
};

struct HorizontalResolveResult {
  App::Runtime::Vec3 resolved_displacement{};
  bool forward_collision{false};
  bool depenetrated{false};
  bool depenetration_limit_reached{false};
  std::uint32_t collision_passes{0};
  std::uint32_t depenetration_iterations{0};
  std::optional<HorizontalCollisionHit> last_hit;
};

class HorizontalCollisionQuery {
 public:
  static constexpr float K_MOVEMENT_THRESHOLD{0.0001F};
  static constexpr float K_HORIZONTAL_COLLISION_LOOKAHEAD{2.0F};
  static constexpr float K_HORIZONTAL_COLLISION_SKIN{1.0F};
  static constexpr float K_DEPENETRATION_RETRY_SCALE{1.1F};
  static constexpr std::uint32_t K_MAX_HORIZONTAL_COLLISION_PASSES{3U};
  /// OpenNomad malformed-data hardening; Runtime's cooked-data loop has no equivalent cap.
  static constexpr std::uint32_t K_MAX_DEPENETRATION_ITERATIONS{16U};

  [[nodiscard]] static std::optional<HorizontalCollisionHit> find(
      const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
      const App::Runtime::Vec3& origin,
      const App::Runtime::Vec3& direction,
      float query_length,
      const HorizontalCollisionBody& body);

  [[nodiscard]] static HorizontalResolveResult resolve(const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
      const App::Runtime::Vec3& origin,
      const App::Runtime::Vec3& desired,
      const HorizontalCollisionBody& body);
};

}  // namespace App::Character