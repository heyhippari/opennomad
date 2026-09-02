#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Character {

struct RuntimeCharacter;

struct PhysicalSupportState {
  bool valid{false};
  std::optional<std::size_t> object_index;
  App::Runtime::Vec3 point{};
  App::Runtime::Vec3 normal{};
  float clearance{0.0F};
  float gap{0.0F};
  bool walkable{false};
  bool grounded{false};
  bool special_deferred{false};
};

struct PhysicalMotionEnvironment {
  const Omikron::Model3DOData* decor_model{nullptr};
  std::span<const Omikron::Model3DOData::RuntimeObjectState> decor_runtime_objects{};
  bool suppress_small_support_snap{false};
};

struct BodyVerticalExtents {
  float top{0.0F};
  float bottom{0.0F};
};

/// OpenNomad actor-owned representation of Runtime's distinct candidate and
/// accepted physical positions. This models recovered semantics, not native
/// actor offsets or layout.
struct PhysicalMotionState {
  App::Runtime::Vec3 candidate_translation{};
  App::Runtime::Vec3 accepted_translation{};
  float accumulator_seconds{0.0F};
  float vertical_velocity{0.0F};
  float gravity_velocity_delta_per_tick{12.8608922958F};
  std::uint8_t fall_stage{0};
  float accumulated_fall_travel{0.0F};
  float maximum_support_gap{0.0F};
  PhysicalSupportState support{};
  bool missing_body_warning_emitted{false};
  bool initialized{false};
};

class PhysicalMotionService {
 public:
  static constexpr float K_LOGIC_STEP_SECONDS{1.0F / 30.0F};
  static constexpr float K_DEFAULT_GRAVITY_VELOCITY_DELTA_PER_TICK{12.8608922958F};
  static constexpr float K_TERMINAL_DOWNWARD_VELOCITY{787.40155F};
  static constexpr float K_SMALL_SUPPORT_SNAP_DISTANCE{7.8740158F};
  static constexpr float K_MAX_WALKABLE_SLOPE_DEGREES{30.0F};
  static constexpr float K_GROUND_CONTACT_DOWNWARD_VELOCITY{11.8110237F};
  static constexpr float K_FALL_STAGE_1_DISTANCE{59.0551186F};
  static constexpr float K_FALL_STAGE_3_DISTANCE{118.110237F};
  static constexpr float K_FALL_STAGE_4_DISTANCE{196.850388F};

  static void synchronize(RuntimeCharacter& character);
  static void synchronize_if_needed(RuntimeCharacter& character);
  static void resolve_tick(
      RuntimeCharacter& character, const PhysicalMotionEnvironment& environment = {});

  [[nodiscard]] static std::optional<std::size_t> largest_sphere(
      std::span<const Omikron::CollisionSphere> spheres);
  [[nodiscard]] static std::optional<std::size_t> bottom_sphere(
      std::span<const Omikron::CollisionSphere> spheres);
  [[nodiscard]] static std::optional<std::size_t> second_bottom_sphere(
      std::span<const Omikron::CollisionSphere> spheres);
  [[nodiscard]] static std::optional<BodyVerticalExtents> body_vertical_extents(
      std::span<const Omikron::CollisionSphere> spheres);
  [[nodiscard]] static float resolve_vertical_displacement(
      float support_gap, float desired_delta_y);
  [[nodiscard]] static bool support_is_walkable(const App::Runtime::Vec3& normal);
  [[nodiscard]] static std::uint8_t fall_stage_for_gap(float positive_gap);
};

}  // namespace App::Character