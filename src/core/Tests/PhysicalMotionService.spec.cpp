#include "Core/Character/PhysicalMotionService.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
#include <memory>
#include <numbers>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Omikron/CtlControlSet.hpp"
#include "OmikronTestBuffer.hpp"

namespace {

struct PhysicalFixture {
  App::Character::RuntimeCharacter character;
  App::Omikron::Model3DOData decor;
  std::shared_ptr<App::Character::ModelResource> resource;

  explicit PhysicalFixture(const float floor_y = 15.0F) {
    resource = std::make_shared<App::Character::ModelResource>();
    resource->model.header.collision_sphere_count = 1;
    resource->model.header.collision_sphere_slots.at(0) = {.center = {}, .radius = 5.0F};
    character.model_resource = resource;

    decor.meshes.push_back(App::Omikron::MeshDescriptor{.vertex_count = 3});
    decor.polygons.push_back(
        App::Omikron::MeshPolygons{.triangles = {App::Omikron::Triangle{
                                       .vertices = {{{.index = 0}, {.index = 1}, {.index = 2}}},
                                       .face_normal = {.x = 0.0F, .y = -1.0F, .z = 0.0F}}}});
    decor.vertices = {{.position = {.x = -1000.0F, .y = floor_y, .z = -1000.0F}},
        {.position = {.x = 1000.0F, .y = floor_y, .z = -1000.0F}},
        {.position = {.x = 0.0F, .y = floor_y, .z = 1000.0F}}};
    decor.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});
  }

  void add_wall() {
    const std::uint32_t vertex_base{static_cast<std::uint32_t>(decor.vertices.size())};
    decor.meshes.push_back(
        App::Omikron::MeshDescriptor{.vertex_count = 4, .vertex_base = vertex_base});
    decor.polygons.push_back(
        App::Omikron::MeshPolygons{.rectangles = {App::Omikron::Rectangle{
                                       .vertices = {0, 1, 2, 3}, .face_normal = {.x = -1.0F}}}});
    decor.vertices.insert(decor.vertices.end(),
        {{.position = {.x = 10.0F, .y = -20.0F, .z = -100.0F}},
            {.position = {.x = 10.0F, .y = -20.0F, .z = 100.0F}},
            {.position = {.x = 10.0F, .y = 20.0F, .z = 100.0F}},
            {.position = {.x = 10.0F, .y = 20.0F, .z = -100.0F}}});
    decor.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});
  }

  [[nodiscard]] App::Character::PhysicalMotionEnvironment environment(
      const bool suppress_snap = false) const {
    return {.decor_model = &decor,
        .decor_runtime_objects = decor.runtime_objects,
        .suppress_small_support_snap = suppress_snap};
  }
};

std::shared_ptr<const App::Omikron::CtlControlSet> make_ctl_bank(
    const std::initializer_list<std::uint32_t> move_ids) {
  Buffer bytes;
  bytes.u32(0x30374543U)
      .u32(0x101U)
      .u32(0)
      .u32(static_cast<std::uint32_t>(move_ids.size()))
      .zeros(0x48U);
  std::size_t move_index{0};
  for (const std::uint32_t move_id : move_ids) {
    bytes.u32(move_id).u32(1).u32(move_index == 0U ? 1U : 0U).u32(0).u32(0).chars("Move", 12);
    ++move_index;
  }
  for (const std::uint32_t move_id : move_ids) {
    bytes.u32(1000U + move_id).u32(0).u32(0x8020U).zeros(0x58U - 12U);
  }
  auto parsed{App::Omikron::CtlControlSet::load(bytes.data())};
  REQUIRE(parsed.has_value());
  return std::make_shared<const App::Omikron::CtlControlSet>(std::move(parsed).value());
}

void attach_controller(App::Character::RuntimeCharacter& character,
    const std::initializer_list<std::uint32_t> move_ids) {
  auto controller{App::Character::CtlController::create(make_ctl_bank(move_ids), "PHYSICS_TEST")};
  REQUIRE(controller.has_value());
  character.ctl_controller = std::move(controller).value();
}

[[nodiscard]] std::optional<std::uint32_t> controller_restart_count(
    const App::Character::RuntimeCharacter& character) {
  if (!character.ctl_controller.has_value()) {
    return std::nullopt;
  }
  return character.ctl_controller->same_state_restart_count();
}

[[nodiscard]] bool select_controller_move(
    App::Character::RuntimeCharacter& character, const std::uint32_t move_id) {
  if (!character.ctl_controller.has_value()) {
    return false;
  }
  return character.ctl_controller->select_move(move_id).has_value();
}

}  // namespace

TEST_SUITE("Core::Character::PhysicalMotionService") {
  TEST_CASE("synchronization initializes both actor-owned positions from the live transform") {
    App::Character::RuntimeCharacter character;
    character.transform.translation = {.x = 11.0F, .y = 22.0F, .z = 33.0F};

    App::Character::PhysicalMotionService::synchronize_if_needed(character);

    CHECK(character.physical_motion.initialized);
    CHECK_EQ(character.physical_motion.candidate_translation.x, 11.0F);
    CHECK_EQ(character.physical_motion.candidate_translation.y, 22.0F);
    CHECK_EQ(character.physical_motion.candidate_translation.z, 33.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.x, 11.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.y, 22.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.z, 33.0F);
  }

  TEST_CASE("grounded resolution keeps exact contact and clears physical motion terms") {
    PhysicalFixture fixture;
    fixture.character.transform.translation = {.x = 1.0F, .y = 10.0F, .z = 3.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.horizontal_physical_x_per_tick = 2.0F;
    fixture.character.physical_motion.horizontal_physical_z_per_tick = -2.0F;
    fixture.character.suppress_automatic_movement_heading = true;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK_EQ(fixture.character.physical_motion.accepted_translation.y, 10.0F);
    CHECK_EQ(fixture.character.transform.translation.y, 10.0F);
    CHECK(fixture.character.physical_motion.support.valid);
    CHECK(fixture.character.physical_motion.support.walkable);
    CHECK(fixture.character.physical_motion.support.grounded);
    CHECK_EQ(fixture.character.physical_motion.support.gap, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_x_per_tick, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.vertical_velocity, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_z_per_tick, 0.0F);
    CHECK_FALSE(fixture.character.suppress_automatic_movement_heading);
  }

  TEST_CASE("horizontal physical terms compose with authored movement before collision") {
    PhysicalFixture fixture;
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.candidate_translation = {.x = 5.0F, .z = 4.0F};
    fixture.character.physical_motion.horizontal_physical_x_per_tick = 2.0F;
    fixture.character.physical_motion.horizontal_physical_z_per_tick = -2.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK_EQ(fixture.character.physical_motion.horizontal_collision.intended_displacement.x, 7.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_collision.intended_displacement.z, 2.0F);
  }

  TEST_CASE("grounded actors integrate gravity and return to zero velocity each tick") {
    PhysicalFixture fixture;
    fixture.character.transform.translation.y = 10.0F;
    App::Character::PhysicalMotionService::synchronize(fixture.character);

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK_EQ(fixture.character.physical_motion.accepted_translation.y, 10.0F);
    CHECK_EQ(fixture.character.physical_motion.vertical_velocity, 0.0F);
    CHECK(fixture.character.physical_motion.support.grounded);
  }

  TEST_CASE("horizontal physical terms run while an attached controller is disabled") {
    PhysicalFixture fixture;
    attach_controller(fixture.character, {1U});
    fixture.character.controller_enabled = false;
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.horizontal_physical_x_per_tick = 2.0F;
    fixture.character.physical_motion.horizontal_physical_z_per_tick = -2.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character);

    CHECK_EQ(fixture.character.physical_motion.horizontal_collision.intended_displacement.x, 2.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_collision.intended_displacement.z, -2.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_x_per_tick, 2.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_z_per_tick, -2.0F);
  }

  TEST_CASE("external live-transform changes re-anchor both positions") {
    App::Character::RuntimeCharacter character;
    character.transform.translation = {.x = 1.0F, .y = 2.0F, .z = 3.0F};
    App::Character::PhysicalMotionService::synchronize(character);
    character.physical_motion.candidate_translation = {.x = 50.0F, .y = 60.0F, .z = 70.0F};
    character.transform.translation = {.x = 8.0F, .y = 9.0F, .z = 10.0F};

    App::Character::PhysicalMotionService::synchronize_if_needed(character);

    CHECK_EQ(character.physical_motion.candidate_translation.x, 8.0F);
    CHECK_EQ(character.physical_motion.candidate_translation.y, 9.0F);
    CHECK_EQ(character.physical_motion.candidate_translation.z, 10.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.x, 8.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.y, 9.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.z, 10.0F);
  }

  TEST_CASE("authored sphere selectors and extents preserve authored tie order") {
    const std::array<App::Omikron::CollisionSphere, 4> spheres{{
        {.center = {.y = 2.0F}, .radius = 4.0F},
        {.center = {.y = 8.0F}, .radius = 2.0F},
        {.center = {.y = 5.0F}, .radius = 5.0F},
        {.center = {.y = -3.0F}, .radius = 1.0F},
    }};

    CHECK_EQ(App::Character::PhysicalMotionService::largest_sphere(spheres), 2U);
    CHECK_EQ(App::Character::PhysicalMotionService::bottom_sphere(spheres), 1U);
    CHECK_EQ(App::Character::PhysicalMotionService::second_bottom_sphere(spheres), 0U);
    const auto extents{App::Character::PhysicalMotionService::body_vertical_extents(spheres)};
    REQUIRE(extents.has_value());
    const auto resolved_extents{extents.value_or(App::Character::BodyVerticalExtents{})};
    CHECK_EQ(resolved_extents.top, -4.0F);
    CHECK_EQ(resolved_extents.bottom, 10.0F);

    const std::array one{App::Omikron::CollisionSphere{.center = {.y = 3.0F}, .radius = 2.0F}};
    CHECK_EQ(App::Character::PhysicalMotionService::second_bottom_sphere(one), 0U);
    CHECK_FALSE(App::Character::PhysicalMotionService::largest_sphere({}).has_value());
  }

  TEST_CASE("vertical displacement uses the recovered exact clamp algebra") {
    using Service = App::Character::PhysicalMotionService;
    CHECK_EQ(Service::resolve_vertical_displacement(10.0F, 4.0F), 4.0F);
    CHECK_EQ(Service::resolve_vertical_displacement(10.0F, 15.0F), 10.0F);
    CHECK_EQ(Service::resolve_vertical_displacement(-5.0F, 1.0F), 0.0F);
    CHECK_EQ(Service::resolve_vertical_displacement(-5.0F, -2.0F), -5.0F);
    CHECK_EQ(Service::resolve_vertical_displacement(-5.0F, -10.0F), -10.0F);
  }

  TEST_CASE("automatic heading math preserves native shortest-turn and wrapping boundaries") {
    using Service = App::Character::PhysicalMotionService;

    CHECK_EQ(Service::normalize_automatic_heading_delta(180.0F), 180.0F);
    CHECK_EQ(Service::normalize_automatic_heading_delta(-180.0F), -180.0F);
    CHECK_EQ(Service::normalize_automatic_heading_delta(-340.0F), 20.0F);
    CHECK_EQ(Service::normalize_automatic_heading_delta(340.0F), -20.0F);
    CHECK_EQ(Service::automatic_heading_correction(0.0F, 90.0F), 11.25F);
    CHECK_EQ(Service::automatic_heading_correction(0.0F, -90.0F), -11.25F);
    CHECK_EQ(Service::automatic_heading_correction(170.0F, -170.0F), 2.5F);
    CHECK_EQ(Service::automatic_heading_correction(-170.0F, 170.0F), -2.5F);
    CHECK_EQ(Service::automatic_heading_correction(0.0F, 180.0F), 22.5F);
    CHECK_EQ(Service::automatic_heading_correction(0.0F, -180.0F), -22.5F);
    CHECK_EQ(Service::wrap_automatic_heading_yaw(365.0F), 5.0F);
    CHECK_EQ(Service::wrap_automatic_heading_yaw(-5.0F), 355.0F);
    CHECK_EQ(Service::wrap_automatic_heading_yaw(360.0F), 360.0F);
    CHECK_EQ(Service::wrap_automatic_heading_yaw(0.0F), 0.0F);
  }

  TEST_CASE("automatic heading guards preserve native precedence and X-only thresholds") {
    using Reason = App::Character::AutomaticHeadingSuppressionReason;
    using Service = App::Character::PhysicalMotionService;

    App::Character::RuntimeCharacter character;
    auto& motion{character.physical_motion};
    auto& horizontal{motion.horizontal_collision};
    horizontal.intended_displacement = {.x = 1.0F};
    horizontal.resolved_displacement = {.x = 1.0F, .z = 1.0F};

    Service::apply_automatic_collision_heading(character);
    CHECK_EQ(horizontal.automatic_heading_suppression, Reason::k_no_forward_collision);
    CHECK_FALSE(horizontal.automatic_heading_applied);

    horizontal.forward_collision = true;
    horizontal.depenetrated = true;
    motion.fall_stage = 2U;
    character.suppress_automatic_movement_heading = true;
    Service::apply_automatic_collision_heading(character);
    CHECK_EQ(horizontal.automatic_heading_suppression, Reason::k_falling);

    motion.fall_stage = 0U;
    Service::apply_automatic_collision_heading(character);
    CHECK_EQ(horizontal.automatic_heading_suppression, Reason::k_mdrot);
    CHECK(horizontal.mdrot_suppression_active);

    character.suppress_automatic_movement_heading = false;
    horizontal.intended_displacement = {.z = 100.0F};
    Service::apply_automatic_collision_heading(character);
    CHECK_EQ(horizontal.automatic_heading_suppression, Reason::k_intended_x_threshold);

    horizontal.intended_displacement = {.x = 0.0001F, .z = 100.0F};
    Service::apply_automatic_collision_heading(character);
    CHECK_EQ(horizontal.automatic_heading_suppression, Reason::k_intended_x_threshold);

    horizontal.intended_displacement.x = -0.000099F;
    Service::apply_automatic_collision_heading(character);
    CHECK_EQ(horizontal.automatic_heading_suppression, Reason::k_intended_x_threshold);

    horizontal.intended_displacement.x = 0.000101F;
    horizontal.resolved_displacement.x = -0.0001F;
    Service::apply_automatic_collision_heading(character);
    CHECK_EQ(horizontal.automatic_heading_suppression, Reason::k_resolved_x_threshold);

    horizontal.resolved_displacement.x = -0.000099F;
    Service::apply_automatic_collision_heading(character);
    CHECK_EQ(horizontal.automatic_heading_suppression, Reason::k_resolved_x_threshold);

    horizontal.resolved_displacement.x = -0.000101F;
    Service::apply_automatic_collision_heading(character);
    CHECK(horizontal.automatic_heading_applied);
  }

  TEST_CASE("automatic heading updates only principal yaw and synchronizes its matrix") {
    App::Character::RuntimeCharacter character;
    character.controller_enabled = false;
    character.set_principal_orientation({.x = 15.0F, .y = 355.0F, .z = 25.0F});
    auto& horizontal{character.physical_motion.horizontal_collision};
    horizontal.forward_collision = true;
    horizontal.intended_displacement = {.x = 1.0F};
    horizontal.resolved_displacement = {.x = std::cos(80.0F * std::numbers::pi_v<float> / 180.0F),
        .z = std::sin(80.0F * std::numbers::pi_v<float> / 180.0F)};

    App::Character::PhysicalMotionService::apply_automatic_collision_heading(character);

    CHECK_EQ(character.principal_orientation_degrees.x, 15.0F);
    CHECK(character.principal_orientation_degrees.y == doctest::Approx(5.0F));
    CHECK_EQ(character.principal_orientation_degrees.z, 25.0F);
    const auto expected{character.principal_orientation()};
    for (std::size_t index{0}; index < expected.values.size(); ++index) {
      CHECK(character.transform.matrix.values.at(index) ==
            doctest::Approx(expected.values.at(index)));
    }
  }

  TEST_CASE("gravity composes with authored candidate Y once") {
    PhysicalFixture fixture{105.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.candidate_translation.y = -2.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.vertical_velocity == doctest::Approx(12.8608923F));
    CHECK(fixture.character.transform.translation.y ==
          doctest::Approx(-2.0F + (12.8608923F / 30.0F)));
    CHECK_EQ(fixture.character.physical_motion.accumulated_fall_travel, 0.0F);
  }

  TEST_CASE("gravity clamps to terminal speed") {
    PhysicalFixture fixture{1000.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.vertical_velocity = 780.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.vertical_velocity ==
          doctest::Approx(App::Character::PhysicalMotionService::K_TERMINAL_DOWNWARD_VELOCITY));
  }

  TEST_CASE("small support snap has strict boundary and per-tick suppression") {
    using Service = App::Character::PhysicalMotionService;
    const auto resolve_gap = [](const float gap, const bool suppress) {
      PhysicalFixture fixture{5.0F + gap};
      Service::synchronize(fixture.character);
      fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
      Service::resolve_tick(fixture.character, fixture.environment(suppress));
      return fixture.character.physical_motion;
    };

    const auto below{resolve_gap(Service::K_SMALL_SUPPORT_SNAP_DISTANCE - 0.001F, false)};
    CHECK_FALSE(below.support.grounded);
    CHECK_EQ(below.support.gap, 0.0F);
    CHECK(below.support.small_step_snapped_this_tick);
    CHECK_EQ(below.vertical_velocity, 0.0F);
    const auto exact{resolve_gap(Service::K_SMALL_SUPPORT_SNAP_DISTANCE, false)};
    CHECK_FALSE(exact.support.grounded);
    CHECK_FALSE(exact.support.small_step_snapped_this_tick);
    CHECK(exact.support.gap == doctest::Approx(Service::K_SMALL_SUPPORT_SNAP_DISTANCE));
    const auto above{resolve_gap(Service::K_SMALL_SUPPORT_SNAP_DISTANCE + 0.001F, false)};
    CHECK_FALSE(above.support.grounded);
    const auto suppressed{resolve_gap(Service::K_SMALL_SUPPORT_SNAP_DISTANCE - 0.001F, true)};
    CHECK_FALSE(suppressed.support.grounded);
    CHECK(suppressed.support.gap > 0.0F);
  }

  TEST_CASE("small support snap defers grounded contact and episode reset by one tick") {
    using Service = App::Character::PhysicalMotionService;
    PhysicalFixture fixture{5.0F + Service::K_SMALL_SUPPORT_SNAP_DISTANCE - 1.0F};
    Service::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.fall_stage = 2U;
    fixture.character.physical_motion.accumulated_fall_travel = 10.0F;
    fixture.character.physical_motion.maximum_support_gap = 20.0F;

    Service::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.support.small_step_snapped_this_tick);
    CHECK_FALSE(fixture.character.physical_motion.support.grounded);
    CHECK_EQ(fixture.character.physical_motion.fall_stage, 2U);
    CHECK_EQ(fixture.character.physical_motion.accumulated_fall_travel, 10.0F);
    CHECK_EQ(fixture.character.physical_motion.maximum_support_gap, 20.0F);

    Service::resolve_tick(fixture.character, fixture.environment());

    CHECK_FALSE(fixture.character.physical_motion.support.small_step_snapped_this_tick);
    CHECK(fixture.character.physical_motion.support.grounded);
    CHECK_EQ(fixture.character.physical_motion.fall_stage, 0U);
    CHECK_EQ(fixture.character.physical_motion.vertical_velocity, 0.0F);
  }

  TEST_CASE("small support snap is limited to stages zero and two") {
    using Service = App::Character::PhysicalMotionService;
    for (const std::uint8_t stage : {0U, 2U}) {
      PhysicalFixture fixture{5.0F + Service::K_SMALL_SUPPORT_SNAP_DISTANCE - 1.0F};
      Service::synchronize(fixture.character);
      fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
      fixture.character.physical_motion.fall_stage = stage;
      Service::resolve_tick(fixture.character, fixture.environment());
      CHECK(fixture.character.physical_motion.support.small_step_snapped_this_tick);
      CHECK_EQ(fixture.character.physical_motion.fall_stage, stage);
    }
    for (const std::uint8_t stage : {1U, 3U, 4U}) {
      PhysicalFixture fixture{5.0F + Service::K_SMALL_SUPPORT_SNAP_DISTANCE};
      Service::synchronize(fixture.character);
      fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
      fixture.character.physical_motion.vertical_velocity = 30.0F;
      fixture.character.physical_motion.fall_stage = stage;
      Service::resolve_tick(fixture.character, fixture.environment());
      CHECK_FALSE(fixture.character.physical_motion.support.small_step_snapped_this_tick);
      CHECK_EQ(fixture.character.physical_motion.fall_stage, stage);
      CHECK(fixture.character.physical_motion.support.gap > 0.0F);
    }
  }

  TEST_CASE("walkability and native fall stages retain exact boundaries") {
    using Service = App::Character::PhysicalMotionService;
    const auto normal = [](const float degrees) {
      const float radians{degrees * std::numbers::pi_v<float> / 180.0F};
      return App::Runtime::Vec3{.x = std::sin(radians), .y = -std::cos(radians)};
    };
    CHECK(Service::support_is_walkable(normal(0.0F)));
    CHECK(Service::support_is_walkable(normal(29.0F)));
    CHECK(Service::support_is_walkable(normal(30.0F)));
    CHECK_FALSE(Service::support_is_walkable(normal(31.0F)));

    CHECK_EQ(Service::fall_stage_for_gap(Service::K_FALL_STAGE_1_DISTANCE - 0.001F), 2U);
    CHECK_EQ(Service::fall_stage_for_gap(Service::K_FALL_STAGE_1_DISTANCE), 1U);
    CHECK_EQ(Service::fall_stage_for_gap(Service::K_FALL_STAGE_3_DISTANCE - 0.001F), 1U);
    CHECK_EQ(Service::fall_stage_for_gap(Service::K_FALL_STAGE_3_DISTANCE), 3U);
    CHECK_EQ(Service::fall_stage_for_gap(Service::K_FALL_STAGE_4_DISTANCE - 0.001F), 3U);
    CHECK_EQ(Service::fall_stage_for_gap(Service::K_FALL_STAGE_4_DISTANCE), 4U);

    CHECK_EQ(Service::resolve_fall_stage(4U, 1.0F), 4U);
    CHECK_EQ(Service::resolve_fall_stage(3U, 1.0F), 3U);
    CHECK_EQ(Service::resolve_fall_stage(1U, 1.0F), 1U);
    CHECK_EQ(Service::resolve_fall_stage(2U, Service::K_FALL_STAGE_3_DISTANCE), 3U);
  }

  TEST_CASE("serious fall stages latch while stage two can promote") {
    using Service = App::Character::PhysicalMotionService;
    for (const std::uint8_t stage : {1U, 3U, 4U}) {
      CHECK_EQ(Service::resolve_fall_stage(stage, Service::K_FALL_STAGE_1_DISTANCE - 1.0F), stage);
      CHECK_EQ(Service::resolve_fall_stage(stage, 1.0F), stage);
    }
    CHECK_EQ(Service::resolve_fall_stage(2U, Service::K_FALL_STAGE_1_DISTANCE - 1.0F), 2U);
    CHECK_EQ(Service::resolve_fall_stage(2U, Service::K_FALL_STAGE_4_DISTANCE), 4U);
  }

  TEST_CASE("no support rolls position back but retains integrated velocity and clears transient") {
    PhysicalFixture fixture;
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.candidate_translation = {.x = 4.0F, .y = 5.0F, .z = 6.0F};
    fixture.character.suppress_automatic_movement_heading = true;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character);

    CHECK_EQ(fixture.character.transform.translation.x, 0.0F);
    CHECK_EQ(fixture.character.transform.translation.y, 0.0F);
    CHECK_EQ(fixture.character.transform.translation.z, 0.0F);
    CHECK(fixture.character.physical_motion.vertical_velocity == doctest::Approx(12.8608923F));
    CHECK_FALSE(fixture.character.physical_motion.support.valid);
    CHECK_FALSE(fixture.character.suppress_automatic_movement_heading);
  }

  TEST_CASE("no-support rollback preserves horizontal physical terms without a collision") {
    PhysicalFixture fixture;
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.horizontal_physical_x_per_tick = 2.0F;
    fixture.character.physical_motion.horizontal_physical_z_per_tick = -2.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character);

    CHECK_EQ(fixture.character.physical_motion.horizontal_collision.intended_displacement.x, 2.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_collision.intended_displacement.z, -2.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_x_per_tick, 2.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_z_per_tick, -2.0F);
    CHECK_EQ(fixture.character.transform.translation.x, 0.0F);
    CHECK_EQ(fixture.character.transform.translation.z, 0.0F);
  }

  TEST_CASE("walkable penetration is corrected to exact contact") {
    PhysicalFixture fixture{5.0F};
    fixture.character.transform.translation.y = 5.0F;
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK_EQ(fixture.character.transform.translation.y, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.support.gap, 0.0F);
    CHECK(fixture.character.physical_motion.support.grounded);
  }

  TEST_CASE("steep contact blocks vertical penetration without claiming grounded sliding") {
    PhysicalFixture fixture{5.0F};
    fixture.decor.polygons.front().triangles.front().face_normal = {
        .x = 1.0F, .y = -1.0F, .z = 0.0F};
    fixture.decor.vertices.at(0).position = {.x = 0.0F, .y = 5.0F, .z = -1000.0F};
    fixture.decor.vertices.at(1).position = {.x = 1000.0F, .y = 1005.0F, .z = -1000.0F};
    fixture.decor.vertices.at(2).position = {.x = 0.0F, .y = 5.0F, .z = 1000.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.fall_stage = 3U;
    fixture.character.physical_motion.accumulated_fall_travel = 20.0F;
    fixture.character.physical_motion.maximum_support_gap = 30.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.support.valid);
    CHECK_FALSE(fixture.character.physical_motion.support.walkable);
    CHECK_FALSE(fixture.character.physical_motion.support.grounded);
    CHECK_EQ(fixture.character.physical_motion.support.gap, 0.0F);
    CHECK(fixture.character.physical_motion.steep_support_response.attempted);
    CHECK_EQ(fixture.character.physical_motion.steep_support_response.input_displacement.y, 0.0F);
    CHECK(fixture.character.physical_motion.steep_support_response.physical_terms_seeded);
    CHECK(fixture.character.physical_motion.horizontal_physical_x_per_tick ==
          doctest::Approx(fixture.character.physical_motion.support.normal.x));
    CHECK(fixture.character.physical_motion.horizontal_physical_z_per_tick ==
          doctest::Approx(fixture.character.physical_motion.support.normal.z));
    CHECK(
        fixture.character.physical_motion.vertical_velocity ==
        doctest::Approx(App::Character::PhysicalMotionService::K_STEEP_SUPPORT_DOWNWARD_VELOCITY));
    CHECK_EQ(fixture.character.physical_motion.fall_stage, 0U);
    CHECK_EQ(fixture.character.physical_motion.accumulated_fall_travel, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.maximum_support_gap, 0.0F);
    CHECK_EQ(fixture.character.transform.translation.x, 0.0F);
    CHECK_EQ(fixture.character.transform.translation.y, 0.0F);
    CHECK_EQ(fixture.character.transform.translation.z, 0.0F);
  }

  TEST_CASE("support at exactly 30 degrees remains walkable without mode 4") {
    PhysicalFixture fixture{5.0F};
    constexpr float angle{30.0F * std::numbers::pi_v<float> / 180.0F};
    fixture.decor.polygons.front().triangles.front().face_normal = {
        .x = std::sin(angle), .y = -std::cos(angle)};
    fixture.decor.vertices.at(0).position = {.x = 0.0F, .y = 5.0F, .z = -1000.0F};
    fixture.decor.vertices.at(1).position = {
        .x = 1000.0F, .y = 5.0F + (std::tan(angle) * 1000.0F), .z = -1000.0F};
    fixture.decor.vertices.at(2).position = {.x = 0.0F, .y = 5.0F, .z = 1000.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.support.walkable);
    CHECK(fixture.character.physical_motion.support.grounded);
    CHECK_FALSE(fixture.character.physical_motion.steep_support_response.attempted);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_x_per_tick, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.vertical_velocity, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_z_per_tick, 0.0F);
  }

  TEST_CASE("upward steep contact retries mode 4 without seeding slide terms") {
    PhysicalFixture fixture{5.0F};
    fixture.decor.polygons.front().triangles.front().face_normal = {
        .x = 1.0F, .y = -1.0F, .z = 0.0F};
    fixture.decor.vertices.at(0).position = {.x = 0.0F, .y = 5.0F, .z = -1000.0F};
    fixture.decor.vertices.at(1).position = {.x = 1000.0F, .y = 1005.0F, .z = -1000.0F};
    fixture.decor.vertices.at(2).position = {.x = 0.0F, .y = 5.0F, .z = 1000.0F};
    fixture.character.transform.translation.y = 5.0F;
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.vertical_velocity = -20.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.steep_support_response.attempted);
    CHECK_FALSE(fixture.character.physical_motion.steep_support_response.physical_terms_seeded);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_x_per_tick, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.vertical_velocity, -20.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_z_per_tick, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.accepted_translation.y, 5.0F);
  }

  TEST_CASE("walkable mover flags seed exact next-tick terms with negative priority") {
    PhysicalFixture fixture;
    fixture.decor.meshes.front().mover_flags = 0x10U | 0x20U | 0x40U | 0x80U;
    fixture.character.transform.translation = {.x = 1.0F, .y = 10.0F, .z = 3.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    const auto& motion{fixture.character.physical_motion};
    CHECK(motion.support.grounded);
    CHECK_EQ(motion.support.mover_flags, 0xF0U);
    CHECK(motion.support.mover_applied_this_tick);
    CHECK_EQ(motion.horizontal_physical_x_per_tick, -2.0F);
    CHECK_EQ(motion.vertical_velocity, 0.0F);
    CHECK_EQ(motion.horizontal_physical_z_per_tick, -2.0F);
    CHECK_EQ(motion.accepted_translation.x, 1.0F);
    CHECK_EQ(motion.accepted_translation.z, 3.0F);
  }

  TEST_CASE("individual mover bits seed their exact horizontal axis terms") {
    struct Expectation {
      std::uint32_t flags;
      float x;
      float z;
    };
    for (const Expectation expected : {Expectation{.flags = 0x10U, .x = 2.0F, .z = 0.0F},
             Expectation{.flags = 0x20U, .x = -2.0F, .z = 0.0F},
             Expectation{.flags = 0x40U, .x = 0.0F, .z = 2.0F},
             Expectation{.flags = 0x80U, .x = 0.0F, .z = -2.0F},
             Expectation{.flags = 0x50U, .x = 2.0F, .z = 2.0F}}) {
      CAPTURE(expected.flags);
      PhysicalFixture fixture;
      fixture.decor.meshes.front().mover_flags = expected.flags;
      fixture.character.transform.translation.y = 10.0F;
      App::Character::PhysicalMotionService::synchronize(fixture.character);

      App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

      CHECK_EQ(fixture.character.physical_motion.horizontal_physical_x_per_tick, expected.x);
      CHECK_EQ(fixture.character.physical_motion.horizontal_physical_z_per_tick, expected.z);
      CHECK_EQ(fixture.character.physical_motion.vertical_velocity, 0.0F);
    }
  }

  TEST_CASE("mover displacement starts on the tick after contact") {
    PhysicalFixture fixture;
    fixture.decor.meshes.front().mover_flags = 0x10U;
    fixture.character.transform.translation = {.x = 1.0F, .y = 10.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    CHECK_EQ(fixture.character.physical_motion.accepted_translation.x, 1.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_x_per_tick, 2.0F);

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    CHECK_EQ(fixture.character.physical_motion.horizontal_collision.intended_displacement.x, 2.0F);
    CHECK_EQ(fixture.character.physical_motion.accepted_translation.x, 3.0F);
  }

  TEST_CASE("wall collision clears mover-generated physical terms on the next tick") {
    PhysicalFixture fixture{12.0F};
    fixture.resource->model.header.collision_sphere_count = 2;
    fixture.resource->model.header.collision_sphere_slots.at(0) = {
        .center = {.y = -10.0F}, .radius = 2.0F};
    fixture.resource->model.header.collision_sphere_slots.at(1) = {
        .center = {.y = 10.0F}, .radius = 2.0F};
    fixture.decor.meshes.front().mover_flags = 0x10U;
    fixture.add_wall();
    fixture.character.transform.translation.x = 7.0F;
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    REQUIRE_EQ(fixture.character.physical_motion.horizontal_physical_x_per_tick, 2.0F);

    fixture.decor.meshes.front().mover_flags = 0U;
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.horizontal_collision.forward_collision);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_x_per_tick, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_z_per_tick, 0.0F);
  }

  TEST_CASE("special support is diagnosed and rolls the complete attempt back") {
    PhysicalFixture fixture{5.0F};
    fixture.decor.meshes.front().flags = 0x20000000U;
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.candidate_translation.x = 8.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.support.valid);
    CHECK(fixture.character.physical_motion.support.special_deferred);
    CHECK_FALSE(fixture.character.physical_motion.support.walkable);
    CHECK_FALSE(fixture.character.physical_motion.support.grounded);
    CHECK_EQ(fixture.character.transform.translation.x, 0.0F);
  }

  TEST_CASE("current support diagnostics are reset before each result") {
    PhysicalFixture fixture{5.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    REQUIRE(fixture.character.physical_motion.support.grounded);

    fixture.decor.vertices.at(0).position.y = 105.0F;
    fixture.decor.vertices.at(1).position.y = 105.0F;
    fixture.decor.vertices.at(2).position.y = 105.0F;
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.vertical_velocity = 0.0F;
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    CHECK_FALSE(fixture.character.physical_motion.support.grounded);

    fixture.decor.meshes.front().flags = 0x20000000U;
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    REQUIRE(fixture.character.physical_motion.support.special_deferred);
    fixture.decor.meshes.front().flags = 0U;
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    CHECK_FALSE(fixture.character.physical_motion.support.special_deferred);
  }

  TEST_CASE("fall travel accumulates downward and maximum gap never decreases") {
    PhysicalFixture fixture{105.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    const float first_travel{fixture.character.physical_motion.accumulated_fall_travel};
    const float first_maximum{fixture.character.physical_motion.maximum_support_gap};
    CHECK_EQ(first_travel, 0.0F);
    CHECK(first_maximum == doctest::Approx(100.0F));

    fixture.decor.vertices.at(0).position.y = 55.0F;
    fixture.decor.vertices.at(1).position.y = 55.0F;
    fixture.decor.vertices.at(2).position.y = 55.0F;
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.accumulated_fall_travel > first_travel);
    CHECK_EQ(fixture.character.physical_motion.maximum_support_gap, first_maximum);
  }

  TEST_CASE("pre-movement gap and previous stage control episode accounting") {
    PhysicalFixture fixture{105.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.vertical_velocity = 600.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.maximum_support_gap == doctest::Approx(100.0F));
    CHECK(fixture.character.physical_motion.support.gap == doctest::Approx(80.0F));
    CHECK_EQ(fixture.character.physical_motion.fall_stage, 1U);
    CHECK_EQ(fixture.character.physical_motion.accumulated_fall_travel, 0.0F);

    fixture.character.physical_motion.vertical_velocity = 300.0F;
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    CHECK(fixture.character.physical_motion.accumulated_fall_travel == doctest::Approx(10.0F));
    CHECK(fixture.character.physical_motion.maximum_support_gap == doctest::Approx(100.0F));
  }

  TEST_CASE("serious fall entry selects move two once even when servicing is disabled") {
    using Service = App::Character::PhysicalMotionService;
    PhysicalFixture fixture{5.0F + Service::K_FALL_STAGE_3_DISTANCE + 10.0F};
    attach_controller(fixture.character, {1U, 2U});
    fixture.character.controller_enabled = false;
    Service::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;

    Service::resolve_tick(fixture.character, fixture.environment(true));
    CHECK_EQ(fixture.character.current_move_id(), std::optional<std::int16_t>{2});
    const auto restart_count{controller_restart_count(fixture.character)};
    REQUIRE(restart_count.has_value());

    Service::resolve_tick(fixture.character, fixture.environment(true));
    CHECK_EQ(fixture.character.current_move_id(), std::optional<std::int16_t>{2});
    CHECK_EQ(controller_restart_count(fixture.character), restart_count);
  }

  TEST_CASE("serious fall classification is independent of controller availability and move data") {
    using Service = App::Character::PhysicalMotionService;
    for (const bool attach_missing_move_controller : {false, true}) {
      PhysicalFixture fixture{5.0F + Service::K_FALL_STAGE_3_DISTANCE + 10.0F};
      if (attach_missing_move_controller) {
        attach_controller(fixture.character, {1U});
      }
      Service::synchronize(fixture.character);
      fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
      Service::resolve_tick(fixture.character, fixture.environment(true));
      CHECK_EQ(fixture.character.physical_motion.fall_stage, 3U);
      CHECK(fixture.character.physical_motion.accepted_translation.y == doctest::Approx(0.0F));
    }
  }

  TEST_CASE("final landing displacement counts before move four reaction") {
    PhysicalFixture fixture{7.0F};
    attach_controller(fixture.character, {1U, 4U});
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.vertical_velocity = 60.0F;
    fixture.character.physical_motion.fall_stage = 3U;
    fixture.character.physical_motion.accumulated_fall_travel = 117.0F;
    fixture.character.physical_motion.maximum_support_gap = 120.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK_EQ(fixture.character.current_move_id(), std::optional<std::int16_t>{4});
    CHECK(fixture.character.physical_motion.support.grounded);
    CHECK_EQ(fixture.character.physical_motion.fall_stage, 0U);
    CHECK_EQ(fixture.character.physical_motion.accumulated_fall_travel, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.maximum_support_gap, 0.0F);
  }

  TEST_CASE("landing reaction thresholds preserve native priority and paired 1.5 metre gate") {
    using Service = App::Character::PhysicalMotionService;
    struct Case {
      float travel;
      float gap;
      std::int16_t expected_move;
    };
    const std::array cases{Case{.travel = Service::K_FALL_STAGE_4_DISTANCE,
                               .gap = Service::K_FALL_STAGE_4_DISTANCE,
                               .expected_move = 5},
        Case{.travel = Service::K_FALL_STAGE_3_DISTANCE,
            .gap = Service::K_FALL_STAGE_3_DISTANCE,
            .expected_move = 4},
        Case{.travel = Service::K_FALL_STAGE_1_DISTANCE,
            .gap = Service::K_FALL_STAGE_1_DISTANCE,
            .expected_move = 4},
        Case{.travel = Service::K_FALL_STAGE_1_DISTANCE,
            .gap = Service::K_FALL_STAGE_1_DISTANCE - 1.0F,
            .expected_move = 1},
        Case{.travel = Service::K_FALL_STAGE_1_DISTANCE - 1.0F,
            .gap = Service::K_FALL_STAGE_1_DISTANCE,
            .expected_move = 1}};
    for (const Case& test_case : cases) {
      PhysicalFixture fixture{5.0F};
      attach_controller(fixture.character, {1U, 4U, 5U});
      Service::synchronize(fixture.character);
      fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
      fixture.character.physical_motion.fall_stage = 3U;
      fixture.character.physical_motion.accumulated_fall_travel = test_case.travel;
      fixture.character.physical_motion.maximum_support_gap = test_case.gap;
      Service::resolve_tick(fixture.character, fixture.environment());
      CHECK_EQ(fixture.character.current_move_id(),
          std::optional<std::int16_t>{test_case.expected_move});
    }
  }

  TEST_CASE("small snap delays low-severity move-two recovery until contact") {
    using Service = App::Character::PhysicalMotionService;
    PhysicalFixture fixture{5.0F + Service::K_SMALL_SUPPORT_SNAP_DISTANCE - 1.0F};
    attach_controller(fixture.character, {1U, 2U, 100U});
    REQUIRE(select_controller_move(fixture.character, 2U));
    Service::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.fall_stage = 2U;

    Service::resolve_tick(fixture.character, fixture.environment());
    CHECK_EQ(fixture.character.current_move_id(), std::optional<std::int16_t>{2});
    CHECK_EQ(fixture.character.physical_motion.fall_stage, 2U);
    CHECK(fixture.character.physical_motion.support.small_step_snapped_this_tick);

    Service::resolve_tick(fixture.character, fixture.environment());
    CHECK_EQ(fixture.character.current_move_id(), std::optional<std::int16_t>{100});
    CHECK_EQ(fixture.character.physical_motion.fall_stage, 0U);
  }

  TEST_CASE("low landing does not force recovery unless current move is two") {
    PhysicalFixture fixture{5.0F};
    attach_controller(fixture.character, {1U, 100U});
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.fall_stage = 2U;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK_EQ(fixture.character.current_move_id(), std::optional<std::int16_t>{1});
  }

  TEST_CASE("missing landing move is nonfatal and disabled controllers still react") {
    using Service = App::Character::PhysicalMotionService;
    for (const bool include_reaction : {false, true}) {
      PhysicalFixture fixture{5.0F};
      if (include_reaction) {
        attach_controller(fixture.character, {1U, 5U});
      } else {
        attach_controller(fixture.character, {1U});
      }
      fixture.character.controller_enabled = false;
      Service::synchronize(fixture.character);
      fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
      fixture.character.physical_motion.fall_stage = 4U;
      fixture.character.physical_motion.accumulated_fall_travel = Service::K_FALL_STAGE_4_DISTANCE;
      fixture.character.physical_motion.maximum_support_gap = Service::K_FALL_STAGE_4_DISTANCE;
      Service::resolve_tick(fixture.character, fixture.environment());
      CHECK(fixture.character.physical_motion.support.grounded);
      CHECK_EQ(fixture.character.physical_motion.fall_stage, 0U);
      CHECK_EQ(fixture.character.current_move_id(),
          std::optional<std::int16_t>{include_reaction ? 5 : 1});
    }
  }

  TEST_CASE("landing clears the active fall episode") {
    PhysicalFixture fixture{5.1F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.fall_stage = 3;
    fixture.character.physical_motion.accumulated_fall_travel = 130.0F;
    fixture.character.physical_motion.maximum_support_gap = 140.0F;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.support.grounded);
    CHECK_EQ(fixture.character.physical_motion.fall_stage, 0U);
    CHECK_EQ(fixture.character.physical_motion.accumulated_fall_travel, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.maximum_support_gap, 0.0F);
  }

  TEST_CASE(
      "authoritative synchronization resets episodes but preserves parameters and remainder") {
    App::Character::RuntimeCharacter character;
    character.transform.translation = {.x = 1.0F, .y = 2.0F, .z = 3.0F};
    character.physical_motion.accumulator_seconds = 0.012F;
    character.physical_motion.gravity_velocity_delta_per_tick = 7.0F;
    character.physical_motion.horizontal_physical_x_per_tick = 2.0F;
    character.physical_motion.vertical_velocity = 99.0F;
    character.physical_motion.horizontal_physical_z_per_tick = -2.0F;
    character.physical_motion.fall_stage = 4;
    character.physical_motion.accumulated_fall_travel = 20.0F;
    character.physical_motion.maximum_support_gap = 30.0F;
    character.physical_motion.support.valid = true;

    App::Character::PhysicalMotionService::synchronize(character);

    CHECK_EQ(character.physical_motion.horizontal_physical_x_per_tick, 0.0F);
    CHECK_EQ(character.physical_motion.vertical_velocity, 0.0F);
    CHECK_EQ(character.physical_motion.horizontal_physical_z_per_tick, 0.0F);
    CHECK_EQ(character.physical_motion.fall_stage, 0U);
    CHECK_EQ(character.physical_motion.accumulated_fall_travel, 0.0F);
    CHECK_EQ(character.physical_motion.maximum_support_gap, 0.0F);
    CHECK_FALSE(character.physical_motion.support.valid);
    CHECK_EQ(character.physical_motion.gravity_velocity_delta_per_tick, 7.0F);
    CHECK_EQ(character.physical_motion.accumulator_seconds, 0.012F);
  }

  TEST_CASE("horizontal collision resolves before support while preserving desired Y") {
    PhysicalFixture fixture{12.0F};
    fixture.resource->model.header.collision_sphere_count = 2;
    fixture.resource->model.header.collision_sphere_slots.at(0) = {
        .center = {.y = -10.0F}, .radius = 2.0F};
    fixture.resource->model.header.collision_sphere_slots.at(1) = {
        .center = {.y = 10.0F}, .radius = 2.0F};
    fixture.decor.vertices.at(1).position.x = 10.0F;
    fixture.decor.vertices.at(2).position.x = 10.0F;
    fixture.add_wall();
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.candidate_translation = {.x = 20.0F, .y = -1.0F, .z = 20.0F};

    App::Character::PhysicalMotionService::resolve_tick(
        fixture.character, fixture.environment(true));

    const auto& motion{fixture.character.physical_motion};
    CHECK(motion.horizontal_collision.body_valid);
    CHECK(motion.horizontal_collision.forward_collision);
    CHECK(motion.horizontal_collision.intended_displacement.x == doctest::Approx(20.0F));
    CHECK(motion.horizontal_collision.resolved_displacement.x == doctest::Approx(7.0F));
    CHECK(motion.horizontal_collision.resolved_displacement.z == doctest::Approx(20.0F));
    CHECK(motion.horizontal_collision.automatic_heading_applied);
    CHECK(motion.horizontal_collision.heading_delta_degrees ==
          doctest::Approx(std::atan2(20.0F, 7.0F) * 180.0F / std::numbers::pi_v<float> - 45.0F));
    CHECK(fixture.character.principal_orientation_degrees.y ==
          doctest::Approx(motion.horizontal_collision.heading_delta_degrees * 0.125F));
    CHECK(motion.horizontal_collision.body_radius == doctest::Approx(2.0F));
    CHECK(motion.horizontal_collision.body_top == doctest::Approx(-12.0F));
    CHECK(motion.horizontal_collision.body_bottom ==
          doctest::Approx(
              12.0F - App::Character::PhysicalMotionService::K_HORIZONTAL_BODY_BOTTOM_TRIM));
    CHECK(motion.support.valid);
    CHECK(motion.accepted_translation.x == doctest::Approx(7.0F));
    CHECK(motion.accepted_translation.y == doctest::Approx(-1.0F));
    CHECK(motion.accepted_translation.z == doctest::Approx(20.0F));
  }

  TEST_CASE("starting-overlap depenetration alone does not steer") {
    PhysicalFixture fixture{12.0F};
    fixture.resource->model.header.collision_sphere_count = 2;
    fixture.resource->model.header.collision_sphere_slots.at(0) = {
        .center = {.y = -10.0F}, .radius = 2.0F};
    fixture.resource->model.header.collision_sphere_slots.at(1) = {
        .center = {.y = 10.0F}, .radius = 2.0F};
    fixture.add_wall();
    fixture.character.transform.translation.x = 7.5F;
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.candidate_translation.x = 5.5F;
    fixture.character.physical_motion.horizontal_physical_x_per_tick = -2.0F;
    fixture.decor = {};
    fixture.add_wall();

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    const auto& horizontal{fixture.character.physical_motion.horizontal_collision};
    CHECK(horizontal.depenetrated);
    CHECK_FALSE(horizontal.forward_collision);
    CHECK_FALSE(horizontal.automatic_heading_applied);
    CHECK_EQ(fixture.character.physical_motion.horizontal_physical_x_per_tick, -2.0F);
    CHECK_EQ(fixture.character.principal_orientation_degrees.y, 0.0F);
  }

  TEST_CASE("automatic heading observes fall stage before support starts a fall") {
    PhysicalFixture fixture{105.0F};
    fixture.resource->model.header.collision_sphere_count = 2;
    fixture.resource->model.header.collision_sphere_slots.at(0) = {
        .center = {.y = -10.0F}, .radius = 2.0F};
    fixture.resource->model.header.collision_sphere_slots.at(1) = {
        .center = {.y = 10.0F}, .radius = 2.0F};
    fixture.add_wall();
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.candidate_translation = {.x = 20.0F, .z = 20.0F};

    App::Character::PhysicalMotionService::resolve_tick(
        fixture.character, fixture.environment(true));

    CHECK(fixture.character.physical_motion.horizontal_collision.automatic_heading_applied);
    CHECK_NE(fixture.character.physical_motion.fall_stage, 0U);
    CHECK_NE(fixture.character.principal_orientation_degrees.y, 0.0F);
  }

  TEST_CASE("MDROT suppresses only steering for one tick and rollback preserves later yaw") {
    PhysicalFixture fixture;
    fixture.resource->model.header.collision_sphere_count = 2;
    fixture.resource->model.header.collision_sphere_slots.at(0) = {
        .center = {.y = -10.0F}, .radius = 2.0F};
    fixture.resource->model.header.collision_sphere_slots.at(1) = {
        .center = {.y = 10.0F}, .radius = 2.0F};
    fixture.decor = {};
    fixture.add_wall();
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.candidate_translation = {.x = 20.0F, .z = 20.0F};
    fixture.character.suppress_automatic_movement_heading = true;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    auto& motion{fixture.character.physical_motion};
    CHECK(motion.horizontal_collision.forward_collision);
    CHECK(motion.horizontal_collision.mdrot_suppression_active);
    CHECK_FALSE(motion.horizontal_collision.automatic_heading_applied);
    CHECK_EQ(motion.candidate_translation.x, 0.0F);
    CHECK_EQ(motion.accepted_translation.x, 0.0F);
    CHECK_EQ(fixture.character.transform.translation.x, 0.0F);
    CHECK_EQ(fixture.character.principal_orientation_degrees.y, 0.0F);
    CHECK_FALSE(fixture.character.suppress_automatic_movement_heading);
    const auto suppressed_resolved{motion.horizontal_collision.resolved_displacement};

    motion.candidate_translation = {.x = 20.0F, .z = 20.0F};
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(motion.horizontal_collision.forward_collision);
    CHECK(motion.horizontal_collision.automatic_heading_applied);
    CHECK_EQ(motion.horizontal_collision.resolved_displacement.x,
        doctest::Approx(suppressed_resolved.x));
    CHECK_EQ(motion.horizontal_collision.resolved_displacement.z,
        doctest::Approx(suppressed_resolved.z));
    CHECK_EQ(motion.horizontal_collision.resolved_displacement.x, doctest::Approx(7.0F));
    CHECK_EQ(motion.horizontal_collision.resolved_displacement.z, doctest::Approx(20.0F));
    CHECK_EQ(motion.candidate_translation.x, 0.0F);
    CHECK_EQ(motion.accepted_translation.x, 0.0F);
    CHECK_EQ(fixture.character.transform.translation.x, 0.0F);
    CHECK_NE(fixture.character.principal_orientation_degrees.y, 0.0F);
    CHECK_FALSE(fixture.character.suppress_automatic_movement_heading);
    const float first_correction{fixture.character.principal_orientation_degrees.y};

    motion.candidate_translation = {.x = 20.0F, .z = 20.0F};
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(motion.horizontal_collision.automatic_heading_applied);
    CHECK(fixture.character.principal_orientation_degrees.y ==
          doctest::Approx(first_correction * 2.0F));
    CHECK_EQ(motion.accepted_translation.x, 0.0F);
  }

  TEST_CASE("MDROT suppresses steering but a real collision still clears physical terms") {
    PhysicalFixture fixture;
    fixture.resource->model.header.collision_sphere_count = 2;
    fixture.resource->model.header.collision_sphere_slots.at(0) = {
        .center = {.y = -10.0F}, .radius = 2.0F};
    fixture.resource->model.header.collision_sphere_slots.at(1) = {
        .center = {.y = 10.0F}, .radius = 2.0F};
    fixture.decor = {};
    fixture.add_wall();
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.candidate_translation = {.x = 19.0F, .z = 19.0F};
    fixture.character.physical_motion.horizontal_physical_x_per_tick = 1.0F;
    fixture.character.physical_motion.horizontal_physical_z_per_tick = 1.0F;
    fixture.character.suppress_automatic_movement_heading = true;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    const auto& motion{fixture.character.physical_motion};
    CHECK(motion.horizontal_collision.forward_collision);
    CHECK_FALSE(motion.horizontal_collision.automatic_heading_applied);
    CHECK_EQ(motion.horizontal_physical_x_per_tick, 0.0F);
    CHECK_EQ(motion.horizontal_physical_z_per_tick, 0.0F);
    CHECK_EQ(motion.accepted_translation.x, 0.0F);
  }

  TEST_CASE("MDROT clears after a successful collision and support commit") {
    PhysicalFixture fixture{12.0F};
    fixture.resource->model.header.collision_sphere_count = 2;
    fixture.resource->model.header.collision_sphere_slots.at(0) = {
        .center = {.y = -10.0F}, .radius = 2.0F};
    fixture.resource->model.header.collision_sphere_slots.at(1) = {
        .center = {.y = 10.0F}, .radius = 2.0F};
    fixture.add_wall();
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;
    fixture.character.physical_motion.candidate_translation = {.x = 20.0F, .z = 20.0F};
    fixture.character.suppress_automatic_movement_heading = true;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.horizontal_collision.forward_collision);
    CHECK_FALSE(fixture.character.physical_motion.horizontal_collision.automatic_heading_applied);
    CHECK(fixture.character.physical_motion.support.valid);
    CHECK(fixture.character.physical_motion.accepted_translation.x == doctest::Approx(7.0F));
    CHECK(fixture.character.physical_motion.accepted_translation.z == doctest::Approx(20.0F));
    CHECK_EQ(fixture.character.principal_orientation_degrees.y, 0.0F);
    CHECK_FALSE(fixture.character.suppress_automatic_movement_heading);
  }

  TEST_CASE("collision scale is configurable and invalid horizontal bodies resolve as identity") {
    PhysicalFixture fixture{12.0F};
    fixture.resource->model.header.collision_sphere_count = 2;
    fixture.resource->model.header.collision_sphere_slots.at(0) = {
        .center = {.y = -10.0F}, .radius = 2.0F};
    fixture.resource->model.header.collision_sphere_slots.at(1) = {
        .center = {.y = 10.0F}, .radius = 2.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.physical_motion.gravity_velocity_delta_per_tick = 0.0F;

    auto environment{fixture.environment()};
    environment.collision_scale = 1.5F;
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, environment);
    CHECK(fixture.character.physical_motion.horizontal_collision.body_radius ==
          doctest::Approx(3.0F));

    fixture.character.physical_motion.candidate_translation.x = 2.0F;
    environment.collision_scale = 0.0F;
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, environment);
    CHECK_FALSE(fixture.character.physical_motion.horizontal_collision.body_valid);
    CHECK(fixture.character.physical_motion.horizontal_collision.resolved_displacement.x ==
          doctest::Approx(2.0F));
  }
}
