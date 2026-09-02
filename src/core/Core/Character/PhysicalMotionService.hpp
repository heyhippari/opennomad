#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "Core/Character/StaticSupportQuery.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Character {

struct RuntimeCharacter;

struct PhysicalSupportState {
  bool valid{false};
  std::optional<SupportClass> support_class;
  std::optional<std::size_t> object_index;
  App::Runtime::Vec3 point{};
  App::Runtime::Vec3 normal{};
  float clearance{0.0F};
  float gap{0.0F};
  std::optional<std::size_t> alternate_object_index;
  float alternate_clearance{0.0F};
  float previous_primary_relative_y{0.0F};
  float primary_relative_y{0.0F};
  float alternate_relative_y{0.0F};
  float support_delta_term{0.0F};
  float primary_post_movement_gap{0.0F};
  float alternate_gap{0.0F};
  bool history_mode4_condition{false};
  bool walkable{false};
  bool grounded{false};
  bool special_deferred{false};
  bool small_step_snapped_this_tick{false};
  std::uint32_t mover_flags{0};
  bool mover_applied_this_tick{false};
};

struct SupportMode4ResponseState {
  bool attempted{false};
  bool triggered_by_history{false};
  bool triggered_by_steep_slope{false};
  App::Runtime::Vec3 input_displacement{};
  App::Runtime::Vec3 resolved_displacement{};
  bool forward_collision{false};
  bool depenetrated{false};
  std::uint32_t collision_passes{0};
  std::optional<std::size_t> object_index;
  App::Runtime::Vec3 response_normal{};
  bool steep_physical_terms_seeded{false};
};

struct SupportHistoryState {
  float primary_relative_y{0.0F};
};

struct Class2SupportResponseState {
  bool eligible{false};
  bool secondary_query_attempted{false};
  bool secondary_hit{false};
  std::optional<std::size_t> secondary_object_index;
  App::Runtime::Vec3 secondary_normal{};
  float secondary_distance{0.0F};
  float secondary_gap{0.0F};
  bool triggered_by_gap{false};
  bool triggered_by_slope{false};
  bool triggered_by_special_flag{false};
  bool attachment_applied{false};
  App::Runtime::Vec3 primary_support_point{};
  float output_x_per_tick{0.0F};
  float output_z_per_tick{0.0F};
};

struct CeilingCollisionState {
  bool attempted{false};
  bool hit{false};
  bool clamped{false};
  float requested_delta_y{0.0F};
  float resolved_delta_y{0.0F};
  float body_top{0.0F};
  float sphere_radius{0.0F};
  float clearance_adjustment{0.0F};
  std::optional<std::size_t> object_index;
  App::Runtime::Vec3 contact_point{};
  App::Runtime::Vec3 contact_normal{};
  float hit_distance{0.0F};
  float ceiling_limit{0.0F};
};

enum class AutomaticHeadingSuppressionReason : std::uint8_t {
  k_none,
  k_no_forward_collision,
  k_falling,
  k_mdrot,
  k_intended_x_threshold,
  k_resolved_x_threshold,
};

struct HorizontalCollisionState {
  App::Runtime::Vec3 intended_displacement{};
  App::Runtime::Vec3 resolved_displacement{};
  float body_radius{0.0F};
  float body_top{0.0F};
  float body_bottom{0.0F};
  float collision_scale{1.0F};
  bool forward_collision{false};
  bool depenetrated{false};
  bool body_valid{false};
  bool depenetration_limit_reached{false};
  std::uint32_t collision_passes{0};
  std::uint32_t depenetration_iterations{0};
  std::optional<std::size_t> object_index;
  App::Runtime::Vec3 contact_point{};
  App::Runtime::Vec3 response_normal{};
  float contact_distance{0.0F};
  bool automatic_heading_applied{false};
  bool mdrot_suppression_active{false};
  AutomaticHeadingSuppressionReason automatic_heading_suppression{
      AutomaticHeadingSuppressionReason::k_none};
  float intended_heading_degrees{0.0F};
  float resolved_heading_degrees{0.0F};
  float heading_delta_degrees{0.0F};
  float yaw_before_degrees{0.0F};
  float yaw_after_degrees{0.0F};
};

struct PhysicalMotionEnvironment {
  const Omikron::Model3DOData* decor_model{nullptr};
  std::span<const Omikron::Model3DOData::RuntimeObjectState> decor_runtime_objects{};
  bool suppress_small_support_snap{false};
  bool mdslidou_support_override_active{false};
  float collision_scale{1.0F};
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
  float horizontal_physical_x_per_tick{0.0F};
  float vertical_velocity{0.0F};
  float horizontal_physical_z_per_tick{0.0F};
  float gravity_velocity_delta_per_tick{12.8608922958F};
  std::uint8_t fall_stage{0};
  float accumulated_fall_travel{0.0F};
  float maximum_support_gap{0.0F};
  SupportHistoryState support_history{};
  HorizontalCollisionState horizontal_collision{};
  SupportMode4ResponseState support_mode4_response{};
  Class2SupportResponseState class2_support_response{};
  CeilingCollisionState ceiling_collision{};
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
  static constexpr float K_STEEP_SUPPORT_DOWNWARD_VELOCITY{11.8110237F};
  static constexpr float K_MOVER_HORIZONTAL_STEP{2.0F};
  static constexpr float K_SUPPORT_SECONDARY_PENETRATION_THRESHOLD{11.8110237F};
  static constexpr float K_CLASS2_SECONDARY_GAP_THRESHOLD{11.8110237F};
  static constexpr float K_CLASS2_ATTACHMENT_FACTOR{0.125F};
  static constexpr float K_CEILING_CLEARANCE_ADJUSTMENT{19.6850395F};
  /// Runtime mode-1's independent 30 cm lower-cylinder step-over allowance.
  static constexpr float K_HORIZONTAL_BODY_BOTTOM_TRIM{11.8110237F};
  static constexpr float K_FALL_STAGE_1_DISTANCE{59.0551186F};
  static constexpr float K_FALL_STAGE_3_DISTANCE{118.110237F};
  static constexpr float K_FALL_STAGE_4_DISTANCE{196.850388F};
  static constexpr float K_AUTOMATIC_HEADING_X_THRESHOLD{0.0001F};
  static constexpr float K_AUTOMATIC_HEADING_CORRECTION_FACTOR{0.125F};

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
  [[nodiscard]] static float ceiling_displacement_limit(
      float body_top, float hit_distance, float sphere_radius);
  [[nodiscard]] static float clamp_upward_displacement(float desired_delta_y, float ceiling_limit);
  [[nodiscard]] static float support_delta_term(
      float primary_clearance, float anchor_y, float previous_primary_relative_y);
  [[nodiscard]] static float primary_relative_y(
      float accepted_y, float candidate_y, float primary_clearance, float anchor_y, float radius);
  [[nodiscard]] static float alternate_relative_y(
      float alternate_clearance, float primary_relative_y, float primary_clearance);
  [[nodiscard]] static bool history_mode4_required(std::uint8_t previous_fall_stage,
      float support_delta,
      float primary_gap_after,
      std::optional<float> alternate_gap_after,
      bool mdslidou_override_active);
  [[nodiscard]] static bool support_is_walkable(const App::Runtime::Vec3& normal);
  [[nodiscard]] static std::uint8_t fall_stage_for_gap(float positive_gap);
  [[nodiscard]] static std::uint8_t resolve_fall_stage(
      std::uint8_t current_stage, float positive_gap);
  [[nodiscard]] static float normalize_automatic_heading_delta(float delta_degrees);
  [[nodiscard]] static float automatic_heading_correction(
      float intended_heading_degrees, float resolved_heading_degrees);
  [[nodiscard]] static float wrap_automatic_heading_yaw(float yaw_degrees);
  static void apply_automatic_collision_heading(RuntimeCharacter& character);
};

}  // namespace App::Character