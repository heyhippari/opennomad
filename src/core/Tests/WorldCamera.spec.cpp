#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <array>

#include "Core/WorldCamera.hpp"
#include "Core/WorldPresentation.hpp"

namespace {

using App::WorldCameraCommand;
using App::WorldCameraSystem;

WorldCameraCommand camera_2172() {
  return WorldCameraCommand{.scene_id = 0,
      .scene_generation = 0,
      .camera_id = 2172,
      .runtime_eye = {-3287, -159, -1701},
      .runtime_target = {-3214, -269, -944},
      .duration_units = 0,
      .flags = 2,
      .wait_for_completion = false,
      .camera_type = 12,
      .angle_units = 0,
      .focal_parameter = 853,
      .field_20 = -1,
      .field_22 = -1,
      .tail_fields = {0, 0, 0, 0}};
}

WorldCameraCommand camera_2148() {
  return WorldCameraCommand{.scene_id = 0,
      .scene_generation = 0,
      .camera_id = 2148,
      .runtime_eye = {-3178, -246, -1507},
      .runtime_target = {-3157, -316, -743},
      .duration_units = 130,
      .flags = 2,
      .wait_for_completion = false,
      .camera_type = 12,
      .angle_units = 0,
      .focal_parameter = 853,
      .field_20 = -1,
      .field_22 = -1,
      .tail_fields = {0, 0, 0, 0}};
}

}  // namespace

TEST_SUITE("Core::WorldCameraSystem") {
  TEST_CASE("Runtime camera coordinates use world scale and handedness conversion") {
    const std::array<float, 3> converted{
        WorldCameraSystem::runtime_to_renderer({-3287, -159, -1701})};
    CHECK(converted.at(0) == doctest::Approx(-82.175F));
    CHECK(converted.at(1) == doctest::Approx(3.975F));
    CHECK(converted.at(2) == doctest::Approx(42.525F));
  }

  TEST_CASE("Zero-duration cameras snap") {
    WorldCameraSystem camera;
    camera.apply_command(camera_2172());
    CHECK(camera.has_scripted_pose());
    CHECK_FALSE(camera.transitioning());
    CHECK_EQ(camera.active_camera_id().value_or(0U), 2172U);
    CHECK(camera.pose().eye.at(0) == doctest::Approx(-82.175F));
    CHECK(camera.pose().target.at(2) == doctest::Approx(23.6F));
  }

  TEST_CASE("AREA duration is interpolated at display rate while preserving 30 Hz timing") {
    WorldCameraSystem camera;
    camera.apply_command(camera_2172());
    const auto start{camera.pose()};
    camera.apply_command(camera_2148());
    REQUIRE(camera.transitioning());

    const std::array<float, 3> target_eye{
        WorldCameraSystem::runtime_to_renderer({-3178, -246, -1507})};

    // Runtime's first quarter of the transition has only reached 12.5%
    // because the first half is quadratic ease-in: 2 * 0.25^2 = 0.125.
    const float quarter_duration{(130.0F / 30.0F) * 0.25F};
    camera.update(quarter_duration);
    CHECK(camera.pose().eye.at(0) ==
          doctest::Approx(start.eye.at(0) + ((target_eye.at(0) - start.eye.at(0)) * 0.125F)));

    camera.update(quarter_duration);
    CHECK(camera.pose().eye.at(0) == doctest::Approx((start.eye.at(0) + target_eye.at(0)) * 0.5F));

    const float half_duration{(130.0F / 30.0F) * 0.5F};
    camera.update(half_duration);
    CHECK_FALSE(camera.transitioning());
    CHECK(camera.pose().eye.at(0) == doctest::Approx(target_eye.at(0)));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)