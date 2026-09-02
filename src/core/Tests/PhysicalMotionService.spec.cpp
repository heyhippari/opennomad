#include "Core/Character/PhysicalMotionService.hpp"

#include <doctest/doctest.h>

#include "Core/Character/CharacterRuntime.hpp"

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
                                       .face_normal = {0.0F, -1.0F, 0.0F}}}});
    decor.vertices = {{.position = {-1000.0F, floor_y, -1000.0F}},
        {.position = {1000.0F, floor_y, -1000.0F}},
        {.position = {0.0F, floor_y, 1000.0F}}};
    decor.runtime_objects.push_back(App::Omikron::Model3DOData::RuntimeObjectState{});
  }

  [[nodiscard]] App::Character::PhysicalMotionEnvironment environment(
      const bool suppress_snap = false) const {
    return {.decor_model = &decor,
        .decor_runtime_objects = decor.runtime_objects,
        .suppress_small_support_snap = suppress_snap};
  }
};

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

  TEST_CASE("grounded resolution keeps exact contact and applies the downward bias") {
    PhysicalFixture fixture;
    fixture.character.transform.translation = {.x = 1.0F, .y = 10.0F, .z = 3.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);
    fixture.character.suppress_automatic_movement_heading = true;

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK_EQ(fixture.character.physical_motion.accepted_translation.y, 10.0F);
    CHECK_EQ(fixture.character.transform.translation.y, 10.0F);
    CHECK(fixture.character.physical_motion.support.valid);
    CHECK(fixture.character.physical_motion.support.walkable);
    CHECK(fixture.character.physical_motion.support.grounded);
    CHECK_EQ(fixture.character.physical_motion.support.gap, 0.0F);
    CHECK(
        fixture.character.physical_motion.vertical_velocity ==
        doctest::Approx(App::Character::PhysicalMotionService::K_GROUND_CONTACT_DOWNWARD_VELOCITY));
    CHECK_FALSE(fixture.character.suppress_automatic_movement_heading);
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
    CHECK_EQ(extents->top, -4.0F);
    CHECK_EQ(extents->bottom, 10.0F);

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
    CHECK(below.support.grounded);
    CHECK_EQ(below.support.gap, 0.0F);
    const auto exact{resolve_gap(Service::K_SMALL_SUPPORT_SNAP_DISTANCE, false)};
    CHECK_FALSE(exact.support.grounded);
    CHECK(exact.support.gap == doctest::Approx(Service::K_SMALL_SUPPORT_SNAP_DISTANCE));
    const auto above{resolve_gap(Service::K_SMALL_SUPPORT_SNAP_DISTANCE + 0.001F, false)};
    CHECK_FALSE(above.support.grounded);
    const auto suppressed{resolve_gap(Service::K_SMALL_SUPPORT_SNAP_DISTANCE - 0.001F, true)};
    CHECK_FALSE(suppressed.support.grounded);
    CHECK(suppressed.support.gap > 0.0F);
  }

  TEST_CASE("walkability and native fall stages retain exact boundaries") {
    using Service = App::Character::PhysicalMotionService;
    constexpr float pi{3.14159265358979323846F};
    const auto normal = [pi](const float degrees) {
      const float radians{degrees * pi / 180.0F};
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
    fixture.decor.polygons.front().triangles.front().face_normal = {1.0F, -1.0F, 0.0F};
    fixture.decor.vertices.at(0).position = {0.0F, 5.0F, -1000.0F};
    fixture.decor.vertices.at(1).position = {1000.0F, 1005.0F, -1000.0F};
    fixture.decor.vertices.at(2).position = {0.0F, 5.0F, 1000.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.support.valid);
    CHECK_FALSE(fixture.character.physical_motion.support.walkable);
    CHECK_FALSE(fixture.character.physical_motion.support.grounded);
    CHECK_EQ(fixture.character.physical_motion.support.gap, 0.0F);
    CHECK_EQ(fixture.character.physical_motion.vertical_velocity, 0.0F);
    CHECK_EQ(fixture.character.transform.translation.x, 0.0F);
    CHECK_EQ(fixture.character.transform.translation.z, 0.0F);
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

  TEST_CASE("fall travel accumulates downward and maximum gap never decreases") {
    PhysicalFixture fixture{105.0F};
    App::Character::PhysicalMotionService::synchronize(fixture.character);

    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());
    const float first_travel{fixture.character.physical_motion.accumulated_fall_travel};
    const float first_maximum{fixture.character.physical_motion.maximum_support_gap};
    CHECK(first_travel > 0.0F);
    CHECK(first_maximum > 0.0F);

    fixture.decor.vertices.at(0).position.y = 55.0F;
    fixture.decor.vertices.at(1).position.y = 55.0F;
    fixture.decor.vertices.at(2).position.y = 55.0F;
    App::Character::PhysicalMotionService::resolve_tick(fixture.character, fixture.environment());

    CHECK(fixture.character.physical_motion.accumulated_fall_travel > first_travel);
    CHECK_EQ(fixture.character.physical_motion.maximum_support_gap, first_maximum);
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
    character.transform.translation = {1.0F, 2.0F, 3.0F};
    character.physical_motion.accumulator_seconds = 0.012F;
    character.physical_motion.gravity_velocity_delta_per_tick = 7.0F;
    character.physical_motion.vertical_velocity = 99.0F;
    character.physical_motion.fall_stage = 4;
    character.physical_motion.accumulated_fall_travel = 20.0F;
    character.physical_motion.maximum_support_gap = 30.0F;
    character.physical_motion.support.valid = true;

    App::Character::PhysicalMotionService::synchronize(character);

    CHECK_EQ(character.physical_motion.vertical_velocity, 0.0F);
    CHECK_EQ(character.physical_motion.fall_stage, 0U);
    CHECK_EQ(character.physical_motion.accumulated_fall_travel, 0.0F);
    CHECK_EQ(character.physical_motion.maximum_support_gap, 0.0F);
    CHECK_FALSE(character.physical_motion.support.valid);
    CHECK_EQ(character.physical_motion.gravity_velocity_delta_per_tick, 7.0F);
    CHECK_EQ(character.physical_motion.accumulator_seconds, 0.012F);
  }
}
