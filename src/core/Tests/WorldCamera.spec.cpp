#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include "Core/RuntimeMath.hpp"
#include "Core/WorldCamera.hpp"
#include "Core/WorldPresentation.hpp"

namespace {

using App::WorldCameraCommand;
using App::WorldCameraSystem;

WorldCameraCommand camera_2172() {
  return WorldCameraCommand{.scene_id = 0,
      .scene_generation = 0,
      .camera_id = 2172,
      .serialized_eye = {-3287, -159, -1701},
      .serialized_target = {-3214, -269, -944},
      .runtime_eye = App::Runtime::iam_camera_vector_to_runtime({-3287, -159, -1701}),
      .runtime_target = App::Runtime::iam_camera_vector_to_runtime({-3214, -269, -944}),
      .duration_units = 0,
      .flags = 2,
      .wait_for_completion = false,
      .camera_type = 12,
      .roll_units = 0,
      .horizontal_fov_units = 853,
      .roll_degrees = 0,
      .horizontal_fov_degrees = 74,
      .target_attachment_selector = -1,
      .eye_attachment_selector = -1,
      .tail_fields = {0, 0, 0, 0}};
}

WorldCameraCommand camera_2148() {
  return WorldCameraCommand{.scene_id = 0,
      .scene_generation = 0,
      .camera_id = 2148,
      .serialized_eye = {-3178, -246, -1507},
      .serialized_target = {-3157, -316, -743},
      .runtime_eye = App::Runtime::iam_camera_vector_to_runtime({-3178, -246, -1507}),
      .runtime_target = App::Runtime::iam_camera_vector_to_runtime({-3157, -316, -743}),
      .duration_units = 130,
      .flags = 2,
      .wait_for_completion = false,
      .camera_type = 12,
      .roll_units = 0,
      .horizontal_fov_units = 853,
      .roll_degrees = 0,
      .horizontal_fov_degrees = 74,
      .target_attachment_selector = -1,
      .eye_attachment_selector = -1,
      .tail_fields = {0, 0, 0, 0}};
}

}  // namespace

TEST_SUITE("Core::WorldCameraSystem") {
  TEST_CASE("Camera roll interpolates through the shortest angular arc") {
    WorldCameraSystem camera;

    WorldCameraCommand tilted{.scene_id = 0,
        .scene_generation = 0,
        .camera_id = 2159,
        .runtime_eye = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
        .runtime_target = {.x = 0.0F, .y = 0.0F, .z = 100.0F},
        .duration_units = 0,
        .roll_degrees = 345,
        .horizontal_fov_degrees = 75};

    WorldCameraCommand upright{tilted};
    upright.camera_id = 2165;
    upright.duration_units = 160;
    upright.roll_degrees = 0;

    camera.apply_command(tilted);
    camera.apply_command(upright);

    REQUIRE(camera.transitioning());

    // At halfway through Runtime's quadratic ease-in/out the interpolation
    // amount is exactly 0.5. The camera should therefore have travelled half
    // of the ~15-degree short arc, not half of a 345-degree revolution.
    camera.update((160.0F / 30.0F) * 0.5F);

    const float roll{camera.pose().roll_degrees};

    // Representation may remain in the 0..360 neighbourhood, so compare its
    // equivalent signed angle.
    CHECK(std::remainder(roll, 360.0F) == doctest::Approx(-7.5F).epsilon(0.01));
  }

  TEST_CASE("Runtime dialogue camera pair snaps to A then travels to B") {
    WorldCameraSystem camera;

    WorldCameraCommand first{.camera_id = 2159,
        .runtime_eye =
            App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{-3206, -345, -1006}),
        .runtime_target =
            App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{-3165, -327, -239}),
        .duration_units = 0,
        .camera_type = 12,
        .roll_units = 3928,
        .horizontal_fov_units = 853,
        .roll_degrees = App::Runtime::area_angle_to_degrees(3928),
        .horizontal_fov_degrees = App::Runtime::area_angle_to_degrees(853)};

    WorldCameraCommand second{.camera_id = 2165,
        .runtime_eye =
            App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{-3159, -394, -846}),
        .runtime_target =
            App::Runtime::area_position_to_inches(std::array<std::int32_t, 3>{-3196, -301, -84}),
        .duration_units = 160,
        .camera_type = 12,
        .roll_units = 0,
        .horizontal_fov_units = 853,
        .roll_degrees = 0,
        .horizontal_fov_degrees = App::Runtime::area_angle_to_degrees(853)};

    camera.apply_command(first);

    CHECK_EQ(camera.active_camera_id().value_or(0U), 2159U);
    CHECK_FALSE(camera.transitioning());
    CHECK(camera.pose().roll_degrees == doctest::Approx(345.0F));

    const App::WorldCameraPose first_pose{camera.pose()};

    camera.apply_command(second);

    CHECK_EQ(camera.active_camera_id().value_or(0U), 2165U);
    REQUIRE(camera.transitioning());

    // The second command must begin at exactly camera A, not whatever
    // pre-dialog AREA camera happened to be active.
    CHECK(camera.pose().eye.x == doctest::Approx(first_pose.eye.x));
    CHECK(camera.pose().roll_degrees == doctest::Approx(first_pose.roll_degrees));
    camera.update(160.0F / 30.0F);

    CHECK_FALSE(camera.transitioning());
    CHECK(camera.pose().roll_degrees == doctest::Approx(0.0F));
    CHECK(camera.pose().eye.x == doctest::Approx(second.runtime_eye.x));
  }

  TEST_CASE("Zero-duration cameras snap") {
    WorldCameraSystem camera;
    camera.apply_command(camera_2172());
    CHECK(camera.has_scripted_pose());
    CHECK_FALSE(camera.transitioning());
    CHECK_EQ(camera.active_camera_id().value_or(0U), 2172U);
    CHECK(camera.pose().eye.x ==
          doctest::Approx(static_cast<float>(App::Runtime::area_position_to_inches(-3287))));
    CHECK(camera.pose().target.z ==
          doctest::Approx(static_cast<float>(App::Runtime::area_position_to_inches(-944))));
    CHECK(camera.pose().horizontal_fov_degrees == doctest::Approx(74.0F));
  }

  TEST_CASE("AREA duration is interpolated at display rate while preserving 30 Hz timing") {
    WorldCameraSystem camera;
    camera.apply_command(camera_2172());
    const auto start{camera.pose()};
    camera.apply_command(camera_2148());
    REQUIRE(camera.transitioning());

    const App::Runtime::Vec3 target_eye{
        App::Runtime::iam_camera_vector_to_runtime({-3178, -246, -1507})};

    // Runtime's first quarter of the transition has only reached 12.5%
    // because the first half is quadratic ease-in: 2 * 0.25^2 = 0.125.
    const float quarter_duration{(130.0F / 30.0F) * 0.25F};
    camera.update(quarter_duration);
    CHECK(camera.pose().eye.x ==
          doctest::Approx(start.eye.x + ((target_eye.x - start.eye.x) * 0.125F)));

    camera.update(quarter_duration);
    CHECK(camera.pose().eye.x == doctest::Approx((start.eye.x + target_eye.x) * 0.5F));

    const float half_duration{(130.0F / 30.0F) * 0.5F};
    camera.update(half_duration);
    CHECK_FALSE(camera.transitioning());
    CHECK(camera.pose().eye.x == doctest::Approx(target_eye.x));
  }

  TEST_CASE("Retail camera 2148 materializes in Runtime inches and recovered FOV") {
    WorldCameraSystem camera;
    WorldCameraCommand command{camera_2148()};
    command.duration_units = 0;
    camera.apply_command(command);

    CHECK(camera.pose().eye.x ==
          doctest::Approx(static_cast<float>(App::Runtime::area_position_to_inches(-3178))));
    CHECK(camera.pose().eye.y ==
          doctest::Approx(static_cast<float>(App::Runtime::area_position_to_inches(-246))));
    CHECK(camera.pose().eye.z ==
          doctest::Approx(static_cast<float>(App::Runtime::area_position_to_inches(-1507))));
    CHECK(camera.pose().target.x ==
          doctest::Approx(static_cast<float>(App::Runtime::area_position_to_inches(-3157))));
    CHECK(camera.pose().target.y ==
          doctest::Approx(static_cast<float>(App::Runtime::area_position_to_inches(-316))));
    CHECK(camera.pose().target.z ==
          doctest::Approx(static_cast<float>(App::Runtime::area_position_to_inches(-743))));
    CHECK(camera.pose().roll_degrees == doctest::Approx(0.0F));
    CHECK(camera.pose().horizontal_fov_degrees == doctest::Approx(74.0F));
    CHECK(camera.camera().get_near_plane() == doctest::Approx(2.0F));
    CHECK(camera.camera().get_far_plane() == doctest::Approx(1968.503937F));
  }

  TEST_CASE("Selector-zero cameras resolve against the live current-actor pose") {
    WorldCameraSystem camera;
    App::WorldCameraAttachmentPose attachment{.translation = {.x = 100.0F, .y = 20.0F, .z = 300.0F},
        .principal_orientation = App::Runtime::Matrix3::identity()};
    camera.set_attachment_pose_provider([&attachment]() {
      return std::optional{attachment};
    });
    const std::array<std::int32_t, 3> eye_serialized{256, 0, 0};
    const std::array<std::int32_t, 3> target_serialized{0, 256, 0};
    WorldCameraCommand command{.camera_id = 9,
        .serialized_eye = eye_serialized,
        .serialized_target = target_serialized,
        .runtime_eye = App::Runtime::iam_camera_vector_to_runtime(eye_serialized),
        .runtime_target = App::Runtime::iam_camera_vector_to_runtime(target_serialized),
        .duration_units = 0,
        .target_attachment_selector = 0,
        .eye_attachment_selector = 0};
    camera.apply_command(command);
    const App::Runtime::Vec3 initial_relative_eye{
        App::Runtime::iam_camera_vector_to_runtime(eye_serialized)};
    CHECK(
        camera.pose().eye.x == doctest::Approx(attachment.translation.x - initial_relative_eye.x));

    attachment.translation = {.x = 500.0F, .y = 40.0F, .z = 50.0F};
    attachment.principal_orientation = App::Runtime::rotation_y(1.57079632679F);
    camera.update(0.0F);
    const App::Runtime::Vec3 expected_eye{
        App::Runtime::transform_vector(App::Runtime::iam_camera_vector_to_runtime(eye_serialized),
            attachment.principal_orientation)};
    CHECK(camera.pose().eye.x == doctest::Approx(attachment.translation.x - expected_eye.x));
    CHECK(camera.pose().eye.z == doctest::Approx(attachment.translation.z - expected_eye.z));

    // Selectors other than -1 and 0 remain unresolved and use the authored
    // absolute fallback rather than inventing another actor-orientation basis.
    command.eye_attachment_selector = 1;
    command.target_attachment_selector = -1;
    camera.apply_command(command);
    CHECK(camera.pose().eye.x == doctest::Approx(command.runtime_eye.x));
    CHECK(camera.pose().target.y == doctest::Approx(command.runtime_target.y));

    command.eye_attachment_selector = 3;
    camera.apply_command(command);
    CHECK(camera.pose().eye.x == doctest::Approx(command.runtime_eye.x));
    CHECK(camera.pose().target.y == doctest::Approx(command.runtime_target.y));
  }

  TEST_CASE("Character-script controller 13 follows the live structured-camera source") {
    WorldCameraSystem camera;
    camera.apply_command(camera_2172());
    const App::WorldCameraPose before{camera.pose()};

    std::optional<App::WorldCameraPose> live{App::WorldCameraPose{
        .eye = {.x = 10.0F, .y = 20.0F, .z = 30.0F},
        .target = {.x = 40.0F, .y = 50.0F, .z = 60.0F},
        .roll_degrees = 7.0F,
        .horizontal_fov_degrees = 80.0F}};
    camera.set_controller_pose_provider([&live]() { return live; });

    camera.apply_command(WorldCameraCommand{.kind = App::WorldCameraCommandKind::k_controller_mode,
        .controller_mode = 13U,
        .duration_units = 30});

    CHECK_EQ(camera.active_camera_id().value_or(0U), 2172U);
    CHECK_FALSE(camera.last_command().has_value());
    CHECK_EQ(camera.active_controller_mode().value_or(0U), 13U);
    CHECK(camera.controller_transitioning());
    CHECK(camera.controller_transition_duration_seconds() == doctest::Approx(1.0F));
    CHECK(camera.pose().eye.x == doctest::Approx(before.eye.x));

    camera.update(0.0F);
    CHECK_FALSE(camera.active_camera_id().has_value());
    CHECK(camera.pose().eye.x == doctest::Approx(10.0F));
   CHECK(camera.pose().target.z == doctest::Approx(60.0F));
    CHECK(camera.pose().roll_degrees == doctest::Approx(7.0F));
    CHECK(camera.pose().horizontal_fov_degrees == doctest::Approx(80.0F));

    live->eye.x = 123.0F;
    camera.update(0.5F);
    CHECK(camera.controller_transitioning());
    CHECK(camera.pose().eye.x == doctest::Approx(123.0F));

    // Native mode 13 does nothing when 0x009103D4 is null. It must retain the
    // last copied pose rather than reviving an interrupted AREA transition.
    live.reset();
    camera.update(0.5F);
    CHECK_FALSE(camera.controller_transitioning());
    CHECK(camera.pose().eye.x == doctest::Approx(123.0F));
  }

  TEST_CASE("Controller 13 can establish the first world-camera pose") {
    WorldCameraSystem camera;
    camera.set_controller_pose_provider([]() {
      return std::optional<App::WorldCameraPose>{App::WorldCameraPose{
          .eye = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
          .target = {.x = 4.0F, .y = 5.0F, .z = 6.0F},
          .horizontal_fov_degrees = 75.0F}};
    });
    camera.apply_command(WorldCameraCommand{.kind = App::WorldCameraCommandKind::k_controller_mode,
        .controller_mode = 13U});
    CHECK_FALSE(camera.has_pose());
    camera.update(0.0F);
    CHECK(camera.has_pose());
    CHECK(camera.pose().eye.z == doctest::Approx(3.0F));
   }

  TEST_CASE("Tracked mode-12 transition exposes its exact completion once") {
    WorldCameraSystem camera;
    camera.apply_command(camera_2172());

    WorldCameraCommand tracked{camera_2148()};
    tracked.source_area_id = 222;
    tracked.wait_for_completion = true;
    tracked.operation_generation = 77U;
    camera.apply_command(tracked);

    CHECK_FALSE(camera.take_completed_operation().has_value());
    REQUIRE(camera.transitioning());
    camera.update(130.0F / 30.0F);
    CHECK_FALSE(camera.transitioning());

    const std::optional<App::WorldCameraOperationCompletion> completed{
        camera.take_completed_operation()};
    REQUIRE(completed.has_value());
    const App::WorldCameraOperationCompletion completion{
        completed.value_or(App::WorldCameraOperationCompletion{})};
    CHECK_EQ(completion.operation_generation, 77U);
    CHECK_EQ(completion.source_area_id, 222);
    CHECK_EQ(completion.camera_id, 2148U);
    CHECK_FALSE(camera.take_completed_operation().has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)
