#include "Core/Character/PhysicalMotionService.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <string_view>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Character/HorizontalCollisionQuery.hpp"
#include "Core/Character/StaticSupportQuery.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"

namespace App::Character {
namespace {

constexpr std::uint32_t K_SPECIAL_SUPPORT_MASK{0x20000000U};

[[nodiscard]] bool is_serious_fall_stage(const std::uint8_t stage) {
  return stage == 1U || stage == 3U || stage == 4U;
}

void try_select_physical_reaction_move(
    CtlController& controller, const std::uint32_t move_id, const std::string_view reason) {
  if (const auto selected{controller.select_move(move_id)}; !selected) {
    App::Log::warn(LogCategory::Core,
        "Unable to select physical {} reaction move {}: {}",
        reason,
        move_id,
        selected.error());
  }
}

[[nodiscard]] std::optional<std::uint32_t> landing_reaction_move(
    const PhysicalMotionState& motion, const std::optional<std::int16_t> current_move_id) {
  if (motion.accumulated_fall_travel >= PhysicalMotionService::K_FALL_STAGE_4_DISTANCE) {
    return 5U;
  }
  if (motion.accumulated_fall_travel >= PhysicalMotionService::K_FALL_STAGE_3_DISTANCE) {
    return 4U;
  }
  if (motion.accumulated_fall_travel >= PhysicalMotionService::K_FALL_STAGE_1_DISTANCE &&
      motion.maximum_support_gap >= PhysicalMotionService::K_FALL_STAGE_1_DISTANCE) {
    return 4U;
  }
  if (current_move_id == 2) {
    return 100U;
  }
  return std::nullopt;
}

void clear_fall_episode(PhysicalMotionState& motion) {
  motion.fall_stage = 0;
  motion.accumulated_fall_travel = 0.0F;
  motion.maximum_support_gap = 0.0F;
}

void reset_physical_episode_for_reanchor(PhysicalMotionState& motion) {
  motion.vertical_velocity = 0.0F;
  clear_fall_episode(motion);
  motion.horizontal_collision = {};
  motion.support = {};
}

void commit(RuntimeCharacter& character) {
  PhysicalMotionState& motion{character.physical_motion};
  motion.accepted_translation = motion.candidate_translation;
  character.transform.translation = motion.accepted_translation;
  character.suppress_automatic_movement_heading = false;
}

void rollback(RuntimeCharacter& character) {
  PhysicalMotionState& motion{character.physical_motion};
  motion.candidate_translation = motion.accepted_translation;
  character.transform.translation = motion.accepted_translation;
  character.suppress_automatic_movement_heading = false;
}

}  // namespace

void PhysicalMotionService::synchronize(RuntimeCharacter& character) {
  PhysicalMotionState& motion{character.physical_motion};
  motion.candidate_translation = character.transform.translation;
  motion.accepted_translation = character.transform.translation;
  reset_physical_episode_for_reanchor(motion);
  motion.initialized = true;
}

void PhysicalMotionService::synchronize_if_needed(RuntimeCharacter& character) {
  const PhysicalMotionState& motion{character.physical_motion};
  const App::Runtime::Vec3& live{character.transform.translation};
  if (!motion.initialized || live.x != motion.accepted_translation.x ||
      live.y != motion.accepted_translation.y || live.z != motion.accepted_translation.z) {
    synchronize(character);
  }
}

std::optional<std::size_t> PhysicalMotionService::largest_sphere(
    const std::span<const Omikron::CollisionSphere> spheres) {
  if (spheres.empty()) {
    return std::nullopt;
  }
  std::size_t selected{0};
  for (std::size_t index{1}; index < spheres.size(); ++index) {
    if (spheres.subspan(index, 1U).front().radius > spheres.subspan(selected, 1U).front().radius) {
      selected = index;
    }
  }
  return selected;
}

std::optional<std::size_t> PhysicalMotionService::bottom_sphere(
    const std::span<const Omikron::CollisionSphere> spheres) {
  if (spheres.empty()) {
    return std::nullopt;
  }
  std::size_t selected{0};
  for (std::size_t index{1}; index < spheres.size(); ++index) {
    const auto bottom = [&spheres](const std::size_t sphere_index) {
      const Omikron::CollisionSphere& sphere{spheres.subspan(sphere_index, 1U).front()};
      return sphere.center.y + sphere.radius;
    };
    if (bottom(index) > bottom(selected)) {
      selected = index;
    }
  }
  return selected;
}

std::optional<std::size_t> PhysicalMotionService::second_bottom_sphere(
    const std::span<const Omikron::CollisionSphere> spheres) {
  const auto bottom{bottom_sphere(spheres)};
  if (!bottom.has_value()) {
    return std::nullopt;
  }
  const Omikron::CollisionSphere& lowest{spheres.subspan(bottom.value(), 1U).front()};
  const float lowest_extent{lowest.center.y + lowest.radius};
  std::optional<std::size_t> selected;
  float selected_extent{0.0F};
  for (std::size_t index{0}; index < spheres.size(); ++index) {
    const Omikron::CollisionSphere& sphere{spheres.subspan(index, 1U).front()};
    const float extent{sphere.center.y + sphere.radius};
    if (extent < lowest_extent && (!selected.has_value() || extent > selected_extent)) {
      selected = index;
      selected_extent = extent;
    }
  }
  return selected.has_value() ? selected : bottom;
}

std::optional<BodyVerticalExtents> PhysicalMotionService::body_vertical_extents(
    const std::span<const Omikron::CollisionSphere> spheres) {
  if (spheres.empty()) {
    return std::nullopt;
  }
  BodyVerticalExtents result{.top = spheres.front().center.y - spheres.front().radius,
      .bottom = spheres.front().center.y + spheres.front().radius};
  for (const Omikron::CollisionSphere& sphere : spheres.subspan(1U)) {
    result.top = std::min(result.top, sphere.center.y - sphere.radius);
    result.bottom = std::max(result.bottom, sphere.center.y + sphere.radius);
  }
  return result;
}

float PhysicalMotionService::resolve_vertical_displacement(
    const float support_gap, const float desired_delta_y) {
  if (support_gap <= 0.0F && desired_delta_y >= 0.0F) {
    return 0.0F;
  }
  if (desired_delta_y >= support_gap) {
    return support_gap;
  }
  return desired_delta_y;
}

bool PhysicalMotionService::support_is_walkable(const App::Runtime::Vec3& normal) {
  const float threshold{
      std::cos(K_MAX_WALKABLE_SLOPE_DEGREES * std::numbers::pi_v<float> / 180.0F)};
  return -normal.y >= threshold;
}

std::uint8_t PhysicalMotionService::fall_stage_for_gap(const float positive_gap) {
  if (positive_gap >= K_FALL_STAGE_4_DISTANCE) {
    return 4;
  }
  if (positive_gap >= K_FALL_STAGE_3_DISTANCE) {
    return 3;
  }
  if (positive_gap >= K_FALL_STAGE_1_DISTANCE) {
    return 1;
  }
  return positive_gap > 0.0F ? 2 : 0;
}

std::uint8_t PhysicalMotionService::resolve_fall_stage(
    const std::uint8_t current_stage, const float positive_gap) {
  if (current_stage != 0U && current_stage != 2U) {
    return current_stage;
  }
  return fall_stage_for_gap(positive_gap);
}

float PhysicalMotionService::normalize_automatic_heading_delta(float delta_degrees) {
  if (delta_degrees < -180.0F) {
    delta_degrees += 360.0F;
  }
  if (delta_degrees > 180.0F) {
    delta_degrees -= 360.0F;
  }
  return delta_degrees;
}

float PhysicalMotionService::automatic_heading_correction(
    const float intended_heading_degrees, const float resolved_heading_degrees) {
  return normalize_automatic_heading_delta(resolved_heading_degrees - intended_heading_degrees) *
         K_AUTOMATIC_HEADING_CORRECTION_FACTOR;
}

float PhysicalMotionService::wrap_automatic_heading_yaw(float yaw_degrees) {
  if (yaw_degrees > 360.0F) {
    yaw_degrees -= 360.0F;
  }
  if (yaw_degrees < 0.0F) {
    yaw_degrees += 360.0F;
  }
  return yaw_degrees;
}

void PhysicalMotionService::apply_automatic_collision_heading(RuntimeCharacter& character) {
  PhysicalMotionState& motion{character.physical_motion};
  HorizontalCollisionState& horizontal{motion.horizontal_collision};
  horizontal.automatic_heading_applied = false;
  horizontal.automatic_heading_suppression = AutomaticHeadingSuppressionReason::k_none;
  horizontal.intended_heading_degrees = 0.0F;
  horizontal.resolved_heading_degrees = 0.0F;
  horizontal.heading_delta_degrees = 0.0F;
  horizontal.yaw_before_degrees = 0.0F;
  horizontal.yaw_after_degrees = 0.0F;
  horizontal.mdrot_suppression_active = character.suppress_automatic_movement_heading;

  if (!horizontal.forward_collision) {
    horizontal.automatic_heading_suppression =
        AutomaticHeadingSuppressionReason::k_no_forward_collision;
    return;
  }
  if (motion.fall_stage != 0U) {
    horizontal.automatic_heading_suppression = AutomaticHeadingSuppressionReason::k_falling;
    return;
  }
  if (character.suppress_automatic_movement_heading) {
    horizontal.automatic_heading_suppression = AutomaticHeadingSuppressionReason::k_mdrot;
    return;
  }
  if (std::abs(horizontal.intended_displacement.x) <= K_AUTOMATIC_HEADING_X_THRESHOLD) {
    horizontal.automatic_heading_suppression =
        AutomaticHeadingSuppressionReason::k_intended_x_threshold;
    return;
  }
  if (std::abs(horizontal.resolved_displacement.x) <= K_AUTOMATIC_HEADING_X_THRESHOLD) {
    horizontal.automatic_heading_suppression =
        AutomaticHeadingSuppressionReason::k_resolved_x_threshold;
    return;
  }

  horizontal.resolved_heading_degrees =
      std::atan2(horizontal.resolved_displacement.z, horizontal.resolved_displacement.x) * 180.0F /
      std::numbers::pi_v<float>;
  horizontal.intended_heading_degrees =
      std::atan2(horizontal.intended_displacement.z, horizontal.intended_displacement.x) * 180.0F /
      std::numbers::pi_v<float>;
  horizontal.heading_delta_degrees = normalize_automatic_heading_delta(
      horizontal.resolved_heading_degrees - horizontal.intended_heading_degrees);
  horizontal.yaw_before_degrees = character.principal_orientation_degrees.y;
  horizontal.yaw_after_degrees =
      wrap_automatic_heading_yaw(horizontal.yaw_before_degrees +
                                 automatic_heading_correction(horizontal.intended_heading_degrees,
                                     horizontal.resolved_heading_degrees));

  App::Runtime::Vec3 orientation{character.principal_orientation_degrees};
  orientation.y = horizontal.yaw_after_degrees;
  character.set_principal_orientation(orientation);
  horizontal.automatic_heading_applied = true;
}

void PhysicalMotionService::resolve_tick(
    RuntimeCharacter& character, const PhysicalMotionEnvironment& environment) {
  PhysicalMotionState& motion{character.physical_motion};
  motion.horizontal_collision = {};
  motion.support = {};
  motion.vertical_velocity =
      std::min(motion.vertical_velocity + motion.gravity_velocity_delta_per_tick,
          K_TERMINAL_DOWNWARD_VELOCITY);
  motion.candidate_translation.y += motion.vertical_velocity * K_LOGIC_STEP_SECONDS;

  const App::Runtime::Vec3 desired{
      .x = motion.candidate_translation.x - motion.accepted_translation.x,
      .y = motion.candidate_translation.y - motion.accepted_translation.y,
      .z = motion.candidate_translation.z - motion.accepted_translation.z};
  motion.candidate_translation = motion.accepted_translation;

  const auto spheres{character.model_resource == nullptr
                         ? std::span<const Omikron::CollisionSphere>{}
                         : character.model_resource->model.header.collision_spheres()};
  const auto largest{largest_sphere(spheres)};
  const auto second_bottom{second_bottom_sphere(spheres)};
  const auto extents{body_vertical_extents(spheres)};
  HorizontalCollisionState& horizontal_state{motion.horizontal_collision};
  horizontal_state.intended_displacement = {.x = desired.x, .y = 0.0F, .z = desired.z};
  horizontal_state.collision_scale = environment.collision_scale;
  if (largest.has_value() && extents.has_value() && std::isfinite(environment.collision_scale) &&
      environment.collision_scale > 0.0F) {
    horizontal_state.body_radius =
        spheres.subspan(largest.value(), 1U).front().radius * environment.collision_scale;
    horizontal_state.body_top = extents->top;
    horizontal_state.body_bottom = extents->bottom - K_HORIZONTAL_BODY_BOTTOM_TRIM;
    horizontal_state.body_valid =
        std::isfinite(horizontal_state.body_radius) && std::isfinite(horizontal_state.body_top) &&
        std::isfinite(horizontal_state.body_bottom) && horizontal_state.body_radius > 0.0F &&
        horizontal_state.body_bottom >= horizontal_state.body_top;
  }

  HorizontalResolveResult horizontal_result;
  horizontal_result.resolved_displacement = horizontal_state.intended_displacement;
  if (horizontal_state.body_valid && environment.decor_model != nullptr) {
    horizontal_result = HorizontalCollisionQuery::resolve(*environment.decor_model,
        environment.decor_runtime_objects,
        motion.accepted_translation,
        horizontal_state.intended_displacement,
        {.radius = horizontal_state.body_radius,
            .top_y = horizontal_state.body_top,
            .bottom_y = horizontal_state.body_bottom});
  }
  horizontal_state.resolved_displacement = horizontal_result.resolved_displacement;
  horizontal_state.forward_collision = horizontal_result.forward_collision;
  horizontal_state.depenetrated = horizontal_result.depenetrated;
  horizontal_state.depenetration_limit_reached = horizontal_result.depenetration_limit_reached;
  horizontal_state.collision_passes = horizontal_result.collision_passes;
  horizontal_state.depenetration_iterations = horizontal_result.depenetration_iterations;
  if (horizontal_result.last_hit.has_value()) {
    horizontal_state.object_index = horizontal_result.last_hit->object_index;
    horizontal_state.contact_point = horizontal_result.last_hit->world_point;
    horizontal_state.response_normal = {.x = horizontal_result.last_hit->world_normal.x,
        .y = 0.0F,
        .z = horizontal_result.last_hit->world_normal.z};
    horizontal_state.contact_distance = horizontal_result.last_hit->travel_distance;
  }
  motion.candidate_translation.x += horizontal_result.resolved_displacement.x;
  motion.candidate_translation.z += horizontal_result.resolved_displacement.z;
  apply_automatic_collision_heading(character);

  if (!largest.has_value() || !second_bottom.has_value() || !extents.has_value()) {
    if (!motion.missing_body_warning_emitted) {
      App::Log::debug(LogCategory::Core,
          "Physical support unavailable for character '{}': no authored collision spheres",
          character.definition_name);
      motion.missing_body_warning_emitted = true;
    }
    rollback(character);
    return;
  }

  const App::Runtime::Vec3 anchor_local{
      .x = 0.0F, .y = spheres.subspan(second_bottom.value(), 1U).front().center.y, .z = 0.0F};
  const App::Runtime::Vec3 anchor_offset{
      App::Runtime::transform_vector(anchor_local, character.principal_orientation())};
  const StaticSupportQueryInput query{
      .world_probe = {.x = motion.candidate_translation.x + anchor_offset.x,
          .y = motion.accepted_translation.y + anchor_offset.y,
          .z = motion.candidate_translation.z + anchor_offset.z},
      .radius = spheres.subspan(largest.value(), 1U).front().radius};
  const auto hit{environment.decor_model == nullptr
                     ? std::optional<StaticSupportHit>{}
                     : StaticSupportQuery::find(
                           *environment.decor_model, environment.decor_runtime_objects, query)};
  if (!hit.has_value()) {
    rollback(character);
    return;
  }

  motion.support.valid = true;
  motion.support.object_index = hit->object_index;
  motion.support.point = hit->world_point;
  motion.support.normal = hit->world_normal;
  motion.support.clearance = hit->clearance;
  if ((environment.decor_model->meshes.at(hit->object_index).flags & K_SPECIAL_SUPPORT_MASK) !=
      0U) {
    motion.support.special_deferred = true;
    rollback(character);
    return;
  }

  const float old_gap{hit->world_point.y - motion.accepted_translation.y - extents->bottom};
  motion.maximum_support_gap = std::max(motion.maximum_support_gap, old_gap);
  const float resolved_delta_y{resolve_vertical_displacement(old_gap, desired.y)};
  const std::uint8_t previous_fall_stage{motion.fall_stage};
  if (previous_fall_stage != 0U && resolved_delta_y > 0.0F) {
    motion.accumulated_fall_travel += resolved_delta_y;
  }
  motion.candidate_translation.y += resolved_delta_y;
  float new_gap{old_gap - resolved_delta_y};

  motion.support.walkable = support_is_walkable(hit->world_normal);
  if (new_gap > 0.0F && (previous_fall_stage == 0U || previous_fall_stage == 2U) &&
      new_gap < K_SMALL_SUPPORT_SNAP_DISTANCE && !environment.suppress_small_support_snap) {
    motion.candidate_translation.y += new_gap;
    motion.support.gap = 0.0F;
    motion.support.small_step_snapped_this_tick = true;
    commit(character);
    return;
  }

  if (new_gap <= 0.0F) {
    if (new_gap < 0.0F) {
      motion.candidate_translation.y += new_gap;
      new_gap = 0.0F;
    }
    if (motion.support.walkable) {
      motion.support.grounded = true;
      motion.vertical_velocity = K_GROUND_CONTACT_DOWNWARD_VELOCITY;
    } else {
      motion.vertical_velocity = 0.0F;
    }
    if (previous_fall_stage != 0U && character.ctl_controller.has_value()) {
      if (const auto reaction{landing_reaction_move(motion, character.current_move_id())};
          reaction.has_value()) {
        try_select_physical_reaction_move(
            character.ctl_controller.value(), reaction.value(), "landing");
      }
    }
    clear_fall_episode(motion);
  } else {
    motion.fall_stage = resolve_fall_stage(previous_fall_stage, new_gap);
    if (motion.fall_stage != previous_fall_stage && is_serious_fall_stage(motion.fall_stage) &&
        character.ctl_controller.has_value()) {
      try_select_physical_reaction_move(character.ctl_controller.value(), 2U, "serious-fall");
    }
  }
  motion.support.gap = new_gap;
  commit(character);
}

}  // namespace App::Character