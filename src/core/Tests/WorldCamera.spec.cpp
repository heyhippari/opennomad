#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <unordered_map>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include "Core/RuntimeMath.hpp"
#include "Core/WorldCamera.hpp"
#include "Core/WorldPresentation.hpp"

namespace {

using App::WorldCameraCommand;
using App::WorldCameraSystem;

using AttachmentMap =
    std::unordered_map<App::Character::BodyIdentity, App::WorldCameraAttachmentPose>;

void set_attachment_provider(WorldCameraSystem& camera, const AttachmentMap& attachments) {
  camera.set_attachment_pose_provider(
      [&attachments](const App::Character::BodyIdentity body_identity)
          -> std::optional<App::WorldCameraAttachmentPose> {
        const auto found{attachments.find(body_identity)};
        if (found == attachments.end()) {
          return std::nullopt;
        }
        return found->second;
      });
}

WorldCameraCommand attached_camera(const std::int16_t selector,
    const std::array<std::int32_t, 3>& eye,
    const std::array<std::int32_t, 3>& target,
    const std::optional<App::Character::BodyIdentity> participant_a = 10,
    const std::optional<App::Character::BodyIdentity> participant_b = 20) {
  return WorldCameraCommand{.camera_id = 9,
      .attachment_participants = {.participant_a_body_identity = participant_a,
          .participant_b_body_identity = participant_b},
      .serialized_eye = eye,
      .serialized_target = target,
      .runtime_eye = App::Runtime::iam_camera_vector_to_runtime(eye),
      .runtime_target = App::Runtime::iam_camera_vector_to_runtime(target),
      .duration_units = 0,
      .target_attachment_selector = selector,
      .eye_attachment_selector = selector};
}

App::Runtime::Vec3 midpoint_attached_point(const App::Runtime::Vec3& midpoint,
    const std::array<std::int32_t, 3>& serialized,
    const float yaw_degrees) {
  const App::Runtime::Matrix3 orientation{App::Runtime::euler_rotation_degrees(
      App::Runtime::Vec3{.x = 0.0F, .y = yaw_degrees, .z = 0.0F})};
  const App::Runtime::Vec3 rotated{App::Runtime::transform_vector(
      App::Runtime::iam_camera_vector_to_runtime(serialized), orientation)};
  return App::Runtime::Vec3{
      .x = midpoint.x - rotated.x, .y = midpoint.y - rotated.y, .z = midpoint.z - rotated.z};
}

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

  TEST_CASE("runtime clip distance updates far plane without resetting pose or FOV") {
    WorldCameraSystem camera;
    WorldCameraCommand command{.camera_id = 2172,
        .runtime_eye = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
        .runtime_target = {.x = 0.0F, .y = 0.0F, .z = 100.0F},
        .duration_units = 0,
        .horizontal_fov_degrees = static_cast<std::int32_t>(70.0F)};

    camera.apply_command(command);
    CHECK(camera.camera().get_far_plane() == doctest::Approx(App::Runtime::metres_to_inches(
                                                 App::Runtime::k_default_clip_distance_metres)));

    camera.set_clip_distance_metres(150.0F);
    CHECK(
        camera.camera().get_far_plane() == doctest::Approx(App::Runtime::metres_to_inches(150.0F)));
    CHECK(camera.pose().horizontal_fov_degrees == doctest::Approx(70.0F));
    CHECK(camera.pose().eye.z == doctest::Approx(0.0F));

    camera.apply_command(command);
    CHECK(
        camera.camera().get_far_plane() == doctest::Approx(App::Runtime::metres_to_inches(150.0F)));
    CHECK(camera.pose().horizontal_fov_degrees == doctest::Approx(70.0F));
    CHECK(camera.pose().target.z == doctest::Approx(100.0F));
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

  TEST_CASE("Selector zero uses explicit participant A and follows that live identity") {
    WorldCameraSystem camera;
    AttachmentMap attachments{
        {10,
            App::WorldCameraAttachmentPose{.translation = {.x = 100.0F, .y = 20.0F, .z = 300.0F},
                .principal_orientation = App::Runtime::Matrix3::identity()}},
        {20,
            App::WorldCameraAttachmentPose{
                .translation = {.x = -500.0F, .y = -400.0F, .z = -300.0F},
                .principal_orientation = App::Runtime::rotation_y(std::numbers::pi_v<float>)}}};
    set_attachment_provider(camera, attachments);
    const std::array<std::int32_t, 3> eye_serialized{256, 0, 0};
    const std::array<std::int32_t, 3> target_serialized{0, 256, 0};
    WorldCameraCommand command{attached_camera(0, eye_serialized, target_serialized)};
    camera.apply_command(command);
    const App::Runtime::Vec3 initial_relative_eye{
        App::Runtime::iam_camera_vector_to_runtime(eye_serialized)};
    CHECK(camera.pose().eye.x ==
          doctest::Approx(attachments.at(10).translation.x - initial_relative_eye.x));

    attachments.at(10).translation = {.x = 500.0F, .y = 40.0F, .z = 50.0F};
    attachments.at(10).principal_orientation =
        App::Runtime::rotation_y(std::numbers::pi_v<float> * 0.5F);
    camera.update(0.0F);
    const App::Runtime::Vec3 expected_eye{
        App::Runtime::transform_vector(App::Runtime::iam_camera_vector_to_runtime(eye_serialized),
            attachments.at(10).principal_orientation)};
    CHECK(
        camera.pose().eye.x == doctest::Approx(attachments.at(10).translation.x - expected_eye.x));
    CHECK(
        camera.pose().eye.z == doctest::Approx(attachments.at(10).translation.z - expected_eye.z));
  }

  TEST_CASE("Selector zero missing participant uses the absolute fallback") {
    WorldCameraSystem camera;
    const AttachmentMap attachments;
    set_attachment_provider(camera, attachments);
    const std::array<std::int32_t, 3> eye{256, 0, 0};
    const std::array<std::int32_t, 3> target{0, 256, 0};
    const WorldCameraCommand command{attached_camera(0, eye, target, std::nullopt, 20)};
    camera.apply_command(command);
    CHECK(camera.pose().eye.x == doctest::Approx(command.runtime_eye.x));
    CHECK(camera.pose().target.y == doctest::Approx(command.runtime_target.y));
  }

  TEST_CASE("Attachment resolution retains captured body identity across world transfer") {
    WorldCameraSystem camera;
    constexpr App::Character::BodyIdentity k_captured_body{1010U};
    constexpr App::Character::BodyIdentity k_duplicate_character_id_body{2020U};
    App::Character::BodyIdentity observed_body{0};
    camera.set_attachment_pose_provider([&](const App::Character::BodyIdentity body_identity)
                                            -> std::optional<App::WorldCameraAttachmentPose> {
      observed_body = body_identity;
      if (body_identity == k_captured_body) {
        return App::WorldCameraAttachmentPose{.translation = {.x = 400.0F, .y = 0.0F, .z = 0.0F}};
      }
      if (body_identity == k_duplicate_character_id_body) {
        return App::WorldCameraAttachmentPose{.translation = {.x = -400.0F, .y = 0.0F, .z = 0.0F}};
      }
      return std::nullopt;
    });
    WorldCameraCommand command{attached_camera(0, {0, 0, 0}, {0, 0, 0}, k_captured_body)};
    command.scene_id = 222U;
    command.scene_generation = 17U;
    command.attachment_participants.participant_a_character_id = 10;

    camera.apply_command(command);
    camera.update(1.0F / 60.0F);

    CHECK_EQ(observed_body, k_captured_body);
    CHECK(camera.pose().eye.x > 300.0F);
  }

  TEST_CASE("Selector six uses the A-B midpoint and recovered relationship yaw") {
    const std::array<std::int32_t, 3> eye{256, 0, 0};
    const std::array<std::int32_t, 3> target{0, 0, 256};

    SUBCASE("X-axis pair derives 90 degrees and keeps target and eye independent") {
      WorldCameraSystem camera;
      const AttachmentMap attachments{
          {10, App::WorldCameraAttachmentPose{.translation = {.x = 10.0F, .y = 0.0F, .z = 0.0F}}},
          {20, App::WorldCameraAttachmentPose{.translation = {.x = -10.0F, .y = 0.0F, .z = 0.0F}}}};
      set_attachment_provider(camera, attachments);
      camera.apply_command(attached_camera(6, eye, target));
      const App::Runtime::Vec3 expected_eye{midpoint_attached_point({}, eye, 90.0F)};
      const App::Runtime::Vec3 expected_target{midpoint_attached_point({}, target, 90.0F)};
      CHECK(camera.pose().eye.x == doctest::Approx(expected_eye.x));
      CHECK(camera.pose().eye.z == doctest::Approx(expected_eye.z));
      CHECK(camera.pose().target.x == doctest::Approx(expected_target.x));
      CHECK(camera.pose().target.z == doctest::Approx(expected_target.z));
      CHECK(camera.pose().eye.x != doctest::Approx(camera.pose().target.x));
    }

    SUBCASE("Z-axis pair derives 180 degrees") {
      WorldCameraSystem camera;
      const AttachmentMap attachments{
          {10, App::WorldCameraAttachmentPose{.translation = {.x = 0.0F, .y = 0.0F, .z = 10.0F}}},
          {20, App::WorldCameraAttachmentPose{.translation = {.x = 0.0F, .y = 0.0F, .z = -10.0F}}}};
      set_attachment_provider(camera, attachments);
      camera.apply_command(attached_camera(6, eye, target));
      const App::Runtime::Vec3 expected_eye{midpoint_attached_point({}, eye, 180.0F)};
      const App::Runtime::Vec3 expected_target{midpoint_attached_point({}, target, 180.0F)};
      CHECK(camera.pose().eye.x == doctest::Approx(expected_eye.x));
      CHECK(camera.pose().eye.z == doctest::Approx(expected_eye.z));
      CHECK(camera.pose().target.x == doctest::Approx(expected_target.x));
      CHECK(camera.pose().target.z == doctest::Approx(expected_target.z));
    }
  }

  TEST_CASE("Selector six remains live after placement and during a transition") {
    const std::array<std::int32_t, 3> eye{256, 0, 0};
    const std::array<std::int32_t, 3> target{0, 0, 256};
    AttachmentMap attachments{
        {10, App::WorldCameraAttachmentPose{.translation = {.x = 10.0F, .y = 0.0F, .z = 0.0F}}},
        {20, App::WorldCameraAttachmentPose{.translation = {.x = -10.0F, .y = 0.0F, .z = 0.0F}}}};

    SUBCASE("zero-duration placement follows both identities") {
      WorldCameraSystem camera;
      set_attachment_provider(camera, attachments);
      camera.apply_command(attached_camera(6, eye, target));
      const App::Runtime::Vec3 initial_eye{camera.pose().eye};
      attachments.at(10).translation.x += 100.0F;
      attachments.at(20).translation.x += 100.0F;
      camera.update(0.0F);
      CHECK(camera.pose().eye.x == doctest::Approx(initial_eye.x + 100.0F));
    }

    SUBCASE("transition destination is re-resolved from moved participants") {
      WorldCameraSystem camera;
      set_attachment_provider(camera, attachments);
      camera.apply_command(camera_2172());
      const App::WorldCameraPose start{camera.pose()};
      WorldCameraCommand command{attached_camera(6, eye, target)};
      command.duration_units = 30;
      camera.apply_command(command);
      REQUIRE(camera.transitioning());
      attachments.at(10).translation.x += 100.0F;
      attachments.at(20).translation.x += 100.0F;
      const App::Runtime::Vec3 destination{
          midpoint_attached_point({.x = 100.0F, .y = 0.0F, .z = 0.0F}, eye, 90.0F)};
      camera.update(0.5F);
      CHECK(camera.pose().eye.x == doctest::Approx((start.eye.x + destination.x) * 0.5F));
      CHECK(camera.pose().eye.z == doctest::Approx((start.eye.z + destination.z) * 0.5F));
    }
  }

  TEST_CASE("Selector six safely falls back for missing or coincident participants") {
    const std::array<std::int32_t, 3> eye{256, 0, 0};
    const std::array<std::int32_t, 3> target{0, 0, 256};

    const auto check_fallback = [&eye, &target](const AttachmentMap& attachments) {
      WorldCameraSystem camera;
      set_attachment_provider(camera, attachments);
      const WorldCameraCommand command{attached_camera(6, eye, target)};
      camera.apply_command(command);
      CHECK(camera.pose().eye.x == doctest::Approx(command.runtime_eye.x));
      CHECK(camera.pose().target.z == doctest::Approx(command.runtime_target.z));
      CHECK(std::isfinite(camera.pose().eye.x));
      CHECK(std::isfinite(camera.pose().eye.y));
      CHECK(std::isfinite(camera.pose().eye.z));
    };

    SUBCASE("missing A") {
      check_fallback(AttachmentMap{{20,
          App::WorldCameraAttachmentPose{.translation = {.x = -10.0F, .y = 0.0F, .z = 0.0F}}}});
    }
    SUBCASE("missing B") {
      check_fallback(AttachmentMap{
          {10, App::WorldCameraAttachmentPose{.translation = {.x = 10.0F, .y = 0.0F, .z = 0.0F}}}});
    }
    SUBCASE("coincident A and B") {
      check_fallback(AttachmentMap{
          {10, App::WorldCameraAttachmentPose{.translation = {.x = 3.0F, .y = 4.0F, .z = 5.0F}}},
          {20, App::WorldCameraAttachmentPose{.translation = {.x = 3.0F, .y = 4.0F, .z = 5.0F}}}});
    }
  }

  TEST_CASE("Unsupported attachment selectors retain absolute fallback") {
    WorldCameraSystem camera;
    const AttachmentMap attachments{
        {10, App::WorldCameraAttachmentPose{.translation = {.x = 100.0F, .y = 20.0F, .z = 300.0F}}},
        {20,
            App::WorldCameraAttachmentPose{
                .translation = {.x = -100.0F, .y = -20.0F, .z = -300.0F}}}};
    set_attachment_provider(camera, attachments);
    const std::array<std::int32_t, 3> eye{256, 0, 0};
    const std::array<std::int32_t, 3> target{0, 256, 0};

    for (const std::int16_t selector :
        {std::int16_t{1}, std::int16_t{3}, std::int16_t{5}, std::int16_t{9}}) {
      const WorldCameraCommand command{attached_camera(selector, eye, target)};
      camera.apply_command(command);
      CHECK(camera.pose().eye.x == doctest::Approx(command.runtime_eye.x));
      CHECK(camera.pose().target.y == doctest::Approx(command.runtime_target.y));
    }
  }

  TEST_CASE("Character-script controller 13 follows the live structured-camera source") {
    WorldCameraSystem camera;
    camera.apply_command(camera_2172());
    const App::WorldCameraPose before{camera.pose()};

    std::optional<App::WorldCameraPose> live{
        App::WorldCameraPose{.eye = {.x = 10.0F, .y = 20.0F, .z = 30.0F},
            .target = {.x = 40.0F, .y = 50.0F, .z = 60.0F},
            .roll_degrees = 7.0F,
            .horizontal_fov_degrees = 80.0F}};
    camera.set_controller_pose_provider([&live]() {
      return live;
    });

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
      return std::optional<App::WorldCameraPose>{
          App::WorldCameraPose{.eye = {.x = 1.0F, .y = 2.0F, .z = 3.0F},
              .target = {.x = 4.0F, .y = 5.0F, .z = 6.0F},
              .horizontal_fov_degrees = 75.0F}};
    });
    camera.apply_command(WorldCameraCommand{
        .kind = App::WorldCameraCommandKind::k_controller_mode, .controller_mode = 13U});
    CHECK_FALSE(camera.has_pose());
    camera.update(0.0F);
    CHECK(camera.has_pose());
    CHECK(camera.pose().eye.z == doctest::Approx(3.0F));
  }

  TEST_CASE("Mode 13 logically releases to mode 0 when the structured source ends") {
    WorldCameraSystem camera;
    std::optional<App::WorldCameraPose> live{
        App::WorldCameraPose{.eye = {.x = 10.0F, .y = 20.0F, .z = 30.0F},
            .target = {.x = 40.0F, .y = 50.0F, .z = 60.0F},
            .horizontal_fov_degrees = 80.0F}};
    camera.set_controller_pose_provider([&live]() {
      return live;
    });
    camera.apply_command(WorldCameraCommand{
        .kind = App::WorldCameraCommandKind::k_controller_mode, .controller_mode = 13U});
    camera.update(0.0F);
    REQUIRE_EQ(camera.active_controller_mode().value_or(0U), 13U);
    CHECK(camera.pose().eye.x == doctest::Approx(10.0F));

    // The structured script stopped publishing: ownership releases to the
    // automatic player camera (mode 0). The last pose stays as a
    // presentation fallback, but the source is no longer scripted.
    camera.release_structured_controller();
    CHECK_EQ(camera.active_controller_mode().value_or(0xFFFFU), 0U);
    CHECK(camera.has_pose());
    CHECK_FALSE(camera.has_scripted_pose());
    CHECK(camera.pose().eye.x == doctest::Approx(10.0F));

    // The released controller no longer consumes the structured source.
    live->eye.x = 999.0F;
    camera.update(0.5F);
    CHECK(camera.pose().eye.x == doctest::Approx(10.0F));

    // Releasing again without mode 13 is a no-op.
    camera.release_structured_controller();
    CHECK_EQ(camera.active_controller_mode().value_or(0xFFFFU), 0U);
  }

  TEST_CASE("autocameraplayer gates the mode-13 release to mode 0") {
    WorldCameraSystem camera;
    camera.apply_command(WorldCameraCommand{
        .kind = App::WorldCameraCommandKind::k_controller_mode, .controller_mode = 13U});
    REQUIRE_EQ(camera.active_controller_mode().value_or(0U), 13U);

    // Legacy default "0": no release even with the source ended and a player
    // character present.
    CHECK_FALSE(camera.autocameraplayer());
    CHECK_FALSE(camera.should_release_structured_controller(false, true));

    // Enabled: the release still requires the structured source to have ended
    // and a current player character to exist.
    camera.set_autocameraplayer(true);
    CHECK(camera.should_release_structured_controller(false, true));
    CHECK_FALSE(camera.should_release_structured_controller(true, true));
    CHECK_FALSE(camera.should_release_structured_controller(false, false));

    // Without mode 13 active there is nothing to release.
    WorldCameraSystem plain;
    plain.set_autocameraplayer(true);
    CHECK_FALSE(plain.should_release_structured_controller(false, true));
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
