#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// misc-include-cleaner, cppcoreguidelines-pro-type-union-access,
// modernize-use-designated-initializers)

#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>

#include "Core/RuntimeMath.hpp"
#include "Core/RuntimePresentation.hpp"

namespace {

constexpr float K_PI{std::numbers::pi_v<float>};

void check_vec(const App::Runtime::Vec3& actual, const App::Runtime::Vec3& expected) {
  CHECK(actual.x == doctest::Approx(expected.x));
  CHECK(actual.y == doctest::Approx(expected.y));
  CHECK(actual.z == doctest::Approx(expected.z));
}

void check_identity(const App::Runtime::Matrix3& matrix) {
  for (std::size_t row{0}; row < 3U; ++row) {
    for (std::size_t column{0}; column < 3U; ++column) {
      CHECK(matrix.at(row, column) == doctest::Approx(row == column ? 1.0F : 0.0F));
    }
  }
}

}  // namespace

TEST_SUITE("Core::RuntimeMath") {
  TEST_CASE("AREA positional normalization reproduces Runtime truncation") {
    using App::Runtime::area_position_to_inches;
    CHECK_EQ(area_position_to_inches(-2588), -399);
    CHECK_EQ(area_position_to_inches(-271), -42);
    CHECK_EQ(area_position_to_inches(-816), -126);
    CHECK_EQ(area_position_to_inches(-3178), -489);
    CHECK_EQ(area_position_to_inches(-246), -38);
    CHECK_EQ(area_position_to_inches(-1507), -232);

    // Both signs distinguish truncation toward zero from floor and rounding.
    CHECK_EQ(area_position_to_inches(1), 0);
    CHECK_EQ(area_position_to_inches(7), 0);
    CHECK_EQ(area_position_to_inches(8), 0);
    CHECK_EQ(area_position_to_inches(-1), -1);
    CHECK_EQ(area_position_to_inches(-7), -2);
    CHECK_EQ(area_position_to_inches(-8), -2);
  }

  TEST_CASE("AREA angular normalization reproduces signed Runtime degrees") {
    using App::Runtime::area_angle_to_degrees;
    CHECK_EQ(area_angle_to_degrees(853), 74);
    CHECK_EQ(area_angle_to_degrees(4084), 358);
    CHECK_EQ(area_angle_to_degrees(2048), 180);
    CHECK_EQ(area_angle_to_degrees(-2048), -180);
    CHECK_EQ(area_angle_to_degrees(-1), 0);
  }

  TEST_CASE("Runtime matrices multiply in ordinary row-major order") {
    const App::Runtime::Matrix3 first{{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F}};
    const App::Runtime::Matrix3 second{{9.0F, 8.0F, 7.0F, 6.0F, 5.0F, 4.0F, 3.0F, 2.0F, 1.0F}};
    const App::Runtime::Matrix3 result{App::Runtime::multiply(first, second)};
    CHECK_EQ(result.at(0, 0), 30.0F);
    CHECK_EQ(result.at(0, 1), 24.0F);
    CHECK_EQ(result.at(0, 2), 18.0F);
    CHECK_EQ(result.at(2, 0), 138.0F);
    CHECK_EQ(result.at(2, 1), 114.0F);
    CHECK_EQ(result.at(2, 2), 90.0F);

    check_vec(App::Runtime::transform_vector({1.0F, 2.0F, 3.0F}, first), {30.0F, 36.0F, 42.0F});
  }

  TEST_CASE("Runtime Euler composition is Ry times Rx times Rz") {
    const float x{0.31F};
    const float y{-0.72F};
    const float z{1.13F};
    const App::Runtime::Matrix3 expected{App::Runtime::multiply(
        App::Runtime::multiply(App::Runtime::rotation_y(y), App::Runtime::rotation_x(x)),
        App::Runtime::rotation_z(z))};
    const App::Runtime::Matrix3 actual{App::Runtime::euler_rotation(x, y, z)};
    for (std::size_t row{0}; row < 3U; ++row) {
      for (std::size_t column{0}; column < 3U; ++column) {
        CHECK(actual.at(row, column) == doctest::Approx(expected.at(row, column)));
      }
    }
  }

  TEST_CASE("Runtime object transform applies row scale, rotation and translation") {
    const App::Runtime::Transform transform{.matrix = App::Runtime::rotation_z(K_PI * 0.5F),
        .translation = {10.0F, 20.0F, 30.0F},
        .scale = {2.0F, 3.0F, 4.0F}};
    check_vec(App::Runtime::transform_point({1.0F, 2.0F, 3.0F}, transform), {16.0F, 18.0F, 42.0F});
  }

  TEST_CASE("Runtime camera preserves recovered direction and roll") {
    const App::Runtime::Vec3 origin{};
    const auto forward{App::Runtime::camera_view(origin, {0.0F, 0.0F, 1.0F}, 0.0F)};
    check_identity(forward.world_to_camera.matrix);

    const auto right{App::Runtime::camera_view(origin, {1.0F, 0.0F, 0.0F}, 0.0F)};
    check_vec(App::Runtime::transform_point({1.0F, 0.0F, 0.0F}, right.world_to_camera),
        {0.0F, 0.0F, 1.0F});

    const auto down{App::Runtime::camera_view(origin, {0.0F, 1.0F, 0.0F}, 0.0F)};
    check_vec(App::Runtime::transform_point({0.0F, 1.0F, 0.0F}, down.world_to_camera),
        {0.0F, 0.0F, 1.0F});

    const auto rolled{App::Runtime::camera_view(origin, {0.0F, 0.0F, 1.0F}, K_PI * 0.5F)};
    check_vec(App::Runtime::transform_point({1.0F, 0.0F, 0.0F}, rolled.world_to_camera),
        {0.0F, -1.0F, 0.0F});

    const App::Runtime::Vec3 eye{12.0F, -4.0F, 30.0F};
    const auto translated{App::Runtime::camera_view(eye, {12.0F, -4.0F, 40.0F}, 0.0F)};
    check_vec(App::Runtime::transform_point(eye, translated.world_to_camera), {});
    check_vec(App::Runtime::transform_point({12.0F, -4.0F, 40.0F}, translated.world_to_camera),
        {0.0F, 0.0F, 10.0F});
  }

  TEST_CASE("Runtime to GL adapter is an unscaled determinant-positive basis change") {
    check_vec(
        App::Runtime::Presentation::to_gl(App::Runtime::Vec3{.x = 1.0F, .y = 2.0F, .z = 3.0F}),
        {1.0F, -2.0F, -3.0F});
    CHECK_EQ(App::Runtime::Presentation::basis_determinant(), 1.0F);

    const App::Runtime::Transform runtime{.matrix = App::Runtime::rotation_y(K_PI * 0.5F),
        .translation = {4.0F, 5.0F, 6.0F},
        .scale = {2.0F, 3.0F, 4.0F}};
    const glm::mat4 gl{App::Runtime::Presentation::to_gl(runtime)};
    const glm::vec4 result{gl * glm::vec4{1.0F, -2.0F, -3.0F, 1.0F}};
    const App::Runtime::Vec3 runtime_result{
        App::Runtime::transform_point({1.0F, 2.0F, 3.0F}, runtime)};
    const App::Runtime::Vec3 expected{App::Runtime::Presentation::to_gl(runtime_result)};
    CHECK(result.x == doctest::Approx(expected.x));
    CHECK(result.y == doctest::Approx(expected.y));
    CHECK(result.z == doctest::Approx(expected.z));
  }

  TEST_CASE("OpenGL projection matches Runtime at 640 by 480") {
    constexpr float k_width{640.0F};
    constexpr float k_height{480.0F};
    constexpr float k_horizontal_fov{74.0F};
    const float vertical_fov{App::Runtime::horizontal_4_3_to_vertical_fov(k_horizontal_fov)};
    const glm::mat4 projection{glm::perspective(glm::radians(vertical_fov),
        k_width / k_height,
        App::Runtime::k_default_near_inches,
        App::Runtime::metres_to_inches(App::Runtime::k_default_clip_distance_metres))};
    const float tangent{std::tan(glm::radians(k_horizontal_fov) * 0.5F)};
    const float factor_x{(k_width * 0.5F) / tangent};
    const float factor_y{((k_height * 0.5F) * (4.0F / 3.0F)) / tangent};

    for (const App::Runtime::Vec3 point :
        {App::Runtime::Vec3{20.0F, 10.0F, 100.0F}, App::Runtime::Vec3{-35.0F, -15.0F, 250.0F}}) {
      const App::Runtime::Vec3 gl_point{App::Runtime::Presentation::to_gl(point)};
      const glm::vec4 clip{projection * glm::vec4{gl_point.x, gl_point.y, gl_point.z, 1.0F}};
      const float gl_screen_x{(k_width * 0.5F) + ((clip.x / clip.w) * k_width * 0.5F)};
      // Runtime and UI diagnostics use a top-left screen origin.
      const float gl_screen_y{(k_height * 0.5F) - ((clip.y / clip.w) * k_height * 0.5F)};
      const float runtime_screen_x{(k_width * 0.5F) + ((point.x / point.z) * factor_x)};
      const float runtime_screen_y{(k_height * 0.5F) + ((point.y / point.z) * factor_y)};
      CHECK(gl_screen_x == doctest::Approx(runtime_screen_x));
      CHECK(gl_screen_y == doctest::Approx(runtime_screen_y));
    }
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while,
// misc-include-cleaner, cppcoreguidelines-pro-type-union-access,
// modernize-use-designated-initializers)
