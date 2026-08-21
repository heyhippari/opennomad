#include <doctest/doctest.h>

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
      .runtime_eye = App::Runtime::area_position_to_inches({-3287, -159, -1701}),
      .runtime_target = App::Runtime::area_position_to_inches({-3214, -269, -944}),
      .duration_units = 0,
      .flags = 2,
      .wait_for_completion = false,
      .camera_type = 12,
      .roll_units = 0,
      .horizontal_fov_units = 853,
      .roll_degrees = 0,
      .horizontal_fov_degrees = 74,
      .field_20 = -1,
      .field_22 = -1,
      .tail_fields = {0, 0, 0, 0}};
}

WorldCameraCommand camera_2148() {
  return WorldCameraCommand{.scene_id = 0,
      .scene_generation = 0,
      .camera_id = 2148,
      .serialized_eye = {-3178, -246, -1507},
      .serialized_target = {-3157, -316, -743},
      .runtime_eye = App::Runtime::area_position_to_inches({-3178, -246, -1507}),
      .runtime_target = App::Runtime::area_position_to_inches({-3157, -316, -743}),
      .duration_units = 130,
      .flags = 2,
      .wait_for_completion = false,
      .camera_type = 12,
      .roll_units = 0,
      .horizontal_fov_units = 853,
      .roll_degrees = 0,
      .horizontal_fov_degrees = 74,
      .field_20 = -1,
      .field_22 = -1,
      .tail_fields = {0, 0, 0, 0}};
}

}  // namespace

TEST_SUITE("Core::WorldCameraSystem") {
  TEST_CASE("Zero-duration cameras snap") {
    WorldCameraSystem camera;
    camera.apply_command(camera_2172());
    CHECK(camera.has_scripted_pose());
    CHECK_FALSE(camera.transitioning());
    CHECK_EQ(camera.active_camera_id().value_or(0U), 2172U);
    CHECK(camera.pose().eye.x == doctest::Approx(-506.0F));
    CHECK(camera.pose().target.z == doctest::Approx(-146.0F));
    CHECK(camera.pose().horizontal_fov_degrees == doctest::Approx(74.0F));
  }

  TEST_CASE("AREA duration is interpolated at display rate while preserving 30 Hz timing") {
    WorldCameraSystem camera;
    camera.apply_command(camera_2172());
    const auto start{camera.pose()};
    camera.apply_command(camera_2148());
    REQUIRE(camera.transitioning());

    const App::Runtime::Vec3 target_eye{
        App::Runtime::area_position_to_inches({-3178, -246, -1507})};

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

    CHECK(camera.pose().eye.x == doctest::Approx(-489.0F));
    CHECK(camera.pose().eye.y == doctest::Approx(-38.0F));
    CHECK(camera.pose().eye.z == doctest::Approx(-232.0F));
    CHECK(camera.pose().target.x == doctest::Approx(-486.0F));
    CHECK(camera.pose().target.y == doctest::Approx(-49.0F));
    CHECK(camera.pose().target.z == doctest::Approx(-115.0F));
    CHECK(camera.pose().roll_degrees == doctest::Approx(0.0F));
    CHECK(camera.pose().horizontal_fov_degrees == doctest::Approx(74.0F));
    CHECK(camera.camera().get_near_plane() == doctest::Approx(2.0F));
    CHECK(camera.camera().get_far_plane() == doctest::Approx(1968.503937F));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)
