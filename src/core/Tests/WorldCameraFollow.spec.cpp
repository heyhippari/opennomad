#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <optional>

// NOLINTBEGIN(misc-use-anonymous-namespace, bugprone-unchecked-optional-access)

#include "Core/RuntimeMath.hpp"
#include "Core/WorldCamera.hpp"
#include "Core/WorldCameraFollow.hpp"
#include "Core/WorldPresentation.hpp"

namespace {

using App::WorldCameraEndpointSource;
using App::WorldCameraFollow;
using App::WorldCameraFollowInput;
using App::WorldCameraFollowSettings;
using App::Runtime::Vec3;

constexpr float k_degrees_to_radians{std::numbers::pi_v<float> / 180.0F};

/// Eye trailing 100 in behind the actor on +Z, target at the actor origin.
WorldCameraFollowSettings trailing_settings() {
  return WorldCameraFollowSettings{.eye_offset = {.x = 0.0F, .y = 0.0F, .z = 100.0F}};
}

WorldCameraFollowInput attached_at(const Vec3& position, const float delta_units) {
  const WorldCameraEndpointSource source{.position = position};
  return WorldCameraFollowInput{.target = source, .eye = source, .delta_units = delta_units};
}

/// A wall plane at Z = `wall_z` that blocks every crossing segment.
WorldCameraFollow::SegmentQuery wall_at(const float wall_z) {
  return [wall_z](const Vec3& start, const Vec3& end) -> std::optional<Vec3> {
    if ((start.z - wall_z) * (end.z - wall_z) >= 0.0F) {
      return std::nullopt;
    }
    const float amount{(wall_z - start.z) / (end.z - start.z)};
    return Vec3{.x = start.x + ((end.x - start.x) * amount),
        .y = start.y + ((end.y - start.y) * amount),
        .z = wall_z};
  };
}

}  // namespace

TEST_SUITE("Core::WorldCameraFollow") {
  TEST_CASE("Runtime camera Euler builder reproduces 0x00441EB0") {
    const Vec3 degrees{.x = 20.0F, .y = -35.0F, .z = 50.0F};
    const float sx{std::sin(degrees.x * k_degrees_to_radians)};
    const float cx{std::cos(degrees.x * k_degrees_to_radians)};
    const float sy{std::sin(degrees.y * k_degrees_to_radians)};
    const float cy{std::cos(degrees.y * k_degrees_to_radians)};
    const float sz{std::sin(degrees.z * k_degrees_to_radians)};
    const float cz{std::cos(degrees.z * k_degrees_to_radians)};
    const App::Runtime::Matrix3 matrix{App::Runtime::camera_euler_rotation_degrees(degrees)};
    CHECK(matrix.values.at(0) == doctest::Approx(cz * cy));
    CHECK(matrix.values.at(1) == doctest::Approx(-(sz * cy)));
    CHECK(matrix.values.at(2) == doctest::Approx(sy));
    CHECK(matrix.values.at(3) == doctest::Approx((sz * cx) + (sy * sx * cz)));
    CHECK(matrix.values.at(4) == doctest::Approx((cz * cx) - (sx * sz * sy)));
    CHECK(matrix.values.at(5) == doctest::Approx(-(sx * cy)));
    CHECK(matrix.values.at(6) == doctest::Approx((sz * sx) - (sy * cz * cx)));
    CHECK(matrix.values.at(7) == doctest::Approx((sx * cz) + (cx * sz * sy)));
    CHECK(matrix.values.at(8) == doctest::Approx(cy * cx));

    const Vec3 yaw_only{.x = 0.0F, .y = 73.0F, .z = 0.0F};
    const App::Runtime::Matrix3 camera{App::Runtime::camera_euler_rotation_degrees(yaw_only)};
    const App::Runtime::Matrix3 actor{App::Runtime::euler_rotation_degrees(yaw_only)};
    for (std::size_t index{0}; index < camera.values.size(); ++index) {
      CHECK(camera.values.at(index) == doctest::Approx(actor.values.at(index)));
    }
  }

  TEST_CASE("Applied cameras snap once, then ease positions by delta over frames") {
    WorldCameraFollow follow;
    WorldCameraFollowSettings settings{trailing_settings()};
    settings.target_position_frames = 1;  // Runtime treats 1 as 2.
    settings.eye_position_frames = 4;
    follow.reset({}, {}, settings);
    follow.update(attached_at({.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0F), {});
    CHECK(follow.target().x == doctest::Approx(0.0F));
    CHECK(follow.eye().z == doctest::Approx(-100.0F));

    follow.update(attached_at({.x = 40.0F, .y = 0.0F, .z = 0.0F}, 1.0F), {});
    CHECK(follow.target().x == doctest::Approx(20.0F));
    CHECK(follow.eye().x == doctest::Approx(10.0F));

    // Unattached endpoints keep their previous value.
    follow.update(WorldCameraFollowInput{.delta_units = 1.0F}, {});
    CHECK(follow.target().x == doctest::Approx(20.0F));
  }

  TEST_CASE("Eye source angles ease along the shortest arc and settle within 0.1 degree") {
    WorldCameraFollow follow;
    WorldCameraFollowSettings settings{trailing_settings()};
    settings.eye_angle_frames = 4;
    follow.reset({}, {}, settings);
    WorldCameraFollowInput input{attached_at({}, 1.0F)};
    input.eye->angles_degrees.y = 350.0F;
    follow.update(input, {});

    // 350 -> 10 travels +20 degrees, a quarter of it per 30 Hz unit.
    input.eye->angles_degrees.y = 10.0F;
    follow.update(input, {});
    const Vec3 expected{WorldCameraFollow::attached_point(
        {}, {.x = 0.0F, .y = 355.0F, .z = 0.0F}, settings.eye_offset)};
    CHECK(follow.eye().x == doctest::Approx(expected.x));
    CHECK(follow.eye().z == doctest::Approx(expected.z));

    input.eye->angles_degrees.y = 355.05F;
    follow.update(input, {});
    const Vec3 settled{WorldCameraFollow::attached_point(
        {}, {.x = 0.0F, .y = 355.05F, .z = 0.0F}, settings.eye_offset)};
    CHECK(follow.eye().x == doctest::Approx(settled.x));
  }

  TEST_CASE("Adventure line of sight pulls the eye in at once and eases back out") {
    WorldCameraFollow follow;
    follow.reset({}, {}, trailing_settings());
    follow.enable_adventure(0.0F, 40.0F);
    const WorldCameraFollow::SegmentQuery clear{};
    follow.update(attached_at({}, 1.0F), clear);
    REQUIRE(follow.eye().z == doctest::Approx(-100.0F));
    CHECK(follow.occlusion() == WorldCameraFollow::Occlusion::k_clear);

    // A wall 90 in behind the actor: the 1.2x ray hits at 90, so the held
    // distance is 75 and the eye is placed there without easing.
    follow.update(attached_at({}, 1.0F), wall_at(-90.0F));
    CHECK(follow.occlusion() == WorldCameraFollow::Occlusion::k_pulled_in);
    CHECK(follow.occluded_distance() == doctest::Approx(75.0F));
    CHECK(follow.eye().z == doctest::Approx(-75.0F));
    // Above half the free distance the height blend keeps part of the eye Y:
    // weight (75 - 50) / 50 = 0.5 toward head height -28 in, over 4 frames.
    CHECK(follow.eye().y == doctest::Approx(-3.5F));

    // Wall removed: recovery eases the distance back toward the free view.
    follow.update(attached_at({}, 1.0F), clear);
    CHECK(follow.occlusion() == WorldCameraFollow::Occlusion::k_recovering);
    const float recovering{follow.occluded_distance()};
    CHECK(recovering > 75.0F);
    CHECK(recovering < 100.0F);

    // The 1/8 ease converges on the free distance (Runtime keeps the float
    // held distance too, so it may stay "recovering" just below it).
    for (int frame{0}; frame < 200; ++frame) {
      follow.update(attached_at({}, 1.0F), clear);
    }
    CHECK(follow.occluded_distance() == doctest::Approx(100.0F));
    CHECK(follow.eye().z == doctest::Approx(-100.0F));
    CHECK(follow.eye().y == doctest::Approx(0.0F).epsilon(0.001));
  }

  TEST_CASE("Recovery ends once the free view is no longer than the held distance") {
    WorldCameraFollow follow;
    follow.reset({}, {}, trailing_settings());
    follow.enable_adventure(0.0F, 40.0F);
    follow.update(attached_at({}, 1.0F), {});
    follow.update(attached_at({}, 1.0F), wall_at(-90.0F));
    REQUIRE(follow.occluded_distance() == doctest::Approx(75.0F));

    // The actor walks back toward the camera: the free view shrinks to 60 in.
    WorldCameraFollowInput closer{attached_at({}, 1.0F)};
    closer.eye->position.z = 40.0F;
    follow.update(closer, {});
    CHECK(follow.occlusion() == WorldCameraFollow::Occlusion::k_clear);
  }

  TEST_CASE("Close occlusion lifts eye and target to head height") {
    WorldCameraFollow follow;
    follow.reset({}, {}, trailing_settings());
    follow.enable_adventure(0.0F, 40.0F);
    follow.update(attached_at({}, 1.0F), {});
    // Wall 24 in behind: distance 20 is under half of 100, so both endpoints
    // aim at the head (-0.7 * 40 = -28 in, Y down) over four frames.
    follow.update(attached_at({}, 1.0F), wall_at(-24.0F));
    CHECK(follow.eye().z == doctest::Approx(-20.0F));
    CHECK(follow.eye().y == doctest::Approx(-7.0F));
    CHECK(follow.target().y == doctest::Approx(-7.0F));
  }

  TEST_CASE("Without the adventure setup occluders are ignored") {
    WorldCameraFollow follow;
    follow.reset({}, {}, trailing_settings());
    follow.update(attached_at({}, 1.0F), wall_at(-24.0F));
    follow.update(attached_at({}, 1.0F), wall_at(-24.0F));
    CHECK(follow.eye().z == doctest::Approx(-100.0F));
    CHECK(follow.occlusion() == WorldCameraFollow::Occlusion::k_clear);
  }

  TEST_CASE("Mode-0 player camera trails the controlled actor with Runtime's preset") {
    App::WorldCameraSystem camera;
    App::WorldCameraAttachmentPose player{
        .translation = {.x = 10.0F, .y = -5.0F, .z = 20.0F}, .body_bottom = 40.0F};
    camera.set_attachment_pose_provider([&player](const App::Character::BodyIdentity body)
                                            -> std::optional<App::WorldCameraAttachmentPose> {
      return body == 7U ? std::optional{player} : std::nullopt;
    });
    const App::WorldCameraCommand command{
        App::WorldCameraSystem::player_camera_command(1U, 1U, {.participant_a_body_identity = 7U})};
    CHECK(command.runtime_eye.z == doctest::Approx(-118.11F).epsilon(0.001));
    camera.apply_command(command);

    CHECK_EQ(camera.active_controller_mode().value_or(0xFFFFU), 0U);
    CHECK(camera.follow().adventure());
    CHECK(camera.pose().horizontal_fov_degrees == doctest::Approx(75.0F));
    CHECK(camera.pose().target.x == doctest::Approx(10.0F));
    CHECK(camera.pose().eye.z == doctest::Approx(20.0F + 118.11F).epsilon(0.001));

    // Target eases over 3 frames: one 30 Hz unit covers a third of a move.
    player.translation.x = 40.0F;
    camera.update(1.0F / 30.0F);
    CHECK(camera.pose().target.x == doctest::Approx(20.0F));
  }

  TEST_CASE("Fight dispatcher states skip the adventure follow setup") {
    App::WorldCameraSystem camera;
    const App::WorldCameraAttachmentPose fighter{.body_bottom = 40.0F, .adventure_mode = 13};
    camera.set_attachment_pose_provider(
        [&fighter](
            const App::Character::BodyIdentity) -> std::optional<App::WorldCameraAttachmentPose> {
          return fighter;
        });
    camera.apply_command(
        App::WorldCameraSystem::player_camera_command(1U, 1U, {.participant_a_body_identity = 7U}));
    CHECK_FALSE(camera.follow().adventure());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, bugprone-unchecked-optional-access)
