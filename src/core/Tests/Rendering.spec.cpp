#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c, misc-include-cleaner, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Core/Camera.hpp"
#include "Core/Reflection.hpp"
#include "Core/Texture.hpp"
#include "Core/TextureCube.hpp"
#include "Core/Vertex.hpp"

TEST_SUITE("Core::Rendering") {
  TEST_CASE("Vertex layout is tightly packed") {
    CHECK_EQ(sizeof(App::Vertex), std::size_t{48});
    CHECK_EQ(offsetof(App::Vertex, position), std::size_t{0});
    CHECK_EQ(offsetof(App::Vertex, normal), std::size_t{12});
    CHECK_EQ(offsetof(App::Vertex, uv), std::size_t{24});
    CHECK_EQ(offsetof(App::Vertex, color), std::size_t{32});
  }

  TEST_CASE("Checkerboard pixels alternate between the two colors") {
    const std::array<float, 3> color_a{1.0F, 0.0F, 0.0F};
    const std::array<float, 3> color_b{0.0F, 0.0F, 1.0F};

    // 16x16 image with 4x4 squares (4-pixel cells).
    const auto pixels{
        App::Texture2D::generate_checkerboard(16, 16, 4, color_a, color_b)};

    REQUIRE(pixels.has_value());
    REQUIRE_EQ(pixels.value().size(), std::size_t{16} * 16U * 4U);

    const auto pixel_at{[](const std::vector<std::uint8_t>& data, const int px, const int py) {
      const std::size_t index{
          ((static_cast<std::size_t>(py) * 16U) + static_cast<std::size_t>(px)) * 4U};
      return std::array<std::uint8_t, 4>{
          data.at(index), data.at(index + 1U), data.at(index + 2U), data.at(index + 3U)};
    }};

    const std::array<std::uint8_t, 4> expected_a{255, 0, 0, 255};
    const std::array<std::uint8_t, 4> expected_b{0, 0, 255, 255};

    // Cell (0,0) -> A; (1,0) -> B; (0,1) -> B; (1,1) -> A; (3,3) -> A.
    CHECK_EQ(pixel_at(pixels.value(), 0, 0), expected_a);
    CHECK_EQ(pixel_at(pixels.value(), 4, 0), expected_b);
    CHECK_EQ(pixel_at(pixels.value(), 0, 4), expected_b);
    CHECK_EQ(pixel_at(pixels.value(), 4, 4), expected_a);
    CHECK_EQ(pixel_at(pixels.value(), 15, 15), expected_a);
  }

  TEST_CASE("Checkerboard generation rejects invalid input") {
    const std::array<float, 3> color_a{1.0F, 1.0F, 1.0F};
    const std::array<float, 3> color_b{0.0F, 0.0F, 0.0F};

    CHECK(!App::Texture2D::generate_checkerboard(0, 8, 2, color_a, color_b).has_value());
    CHECK(!App::Texture2D::generate_checkerboard(8, 0, 2, color_a, color_b).has_value());
    CHECK(!App::Texture2D::generate_checkerboard(8, 8, 0, color_a, color_b).has_value());
  }

  TEST_CASE("Camera look_at points the view at the target") {
    App::Camera camera{60.0F, 1.0F, 0.1F, 1000.0F};
    camera.set_position(0.0F, 0.0F, 3.0F);
    camera.look_at(0.0F, 0.0F, 0.0F);

    const std::span<const float, 16> view{camera.get_view_matrix()};
    // The origin must land at view-space z = -3 (in front of the camera).
    CHECK_EQ(view[14], doctest::Approx(-3.0F));
  }

  TEST_CASE("View basis recovers the camera frame from a lookAt view") {
    // Camera at (0, 2, 4) looking at the origin.
    const glm::mat4 view{glm::lookAt(glm::vec3{0.0F, 2.0F, 4.0F},
                                     glm::vec3{0.0F, 0.0F, 0.0F},
                                     glm::vec3{0.0F, 1.0F, 0.0F})};

    const App::ViewBasis basis{App::view_basis(view)};

    // front = normalize(0, -2, -4): from the eye toward the target.
    CHECK_EQ(basis.front[0], doctest::Approx(0.0F));
    CHECK_EQ(basis.front[1], doctest::Approx(-0.4472136F));
    CHECK_EQ(basis.front[2], doctest::Approx(-0.8944272F));
    // right = normalize(cross(front, world up)) is screen-right.
    CHECK_EQ(basis.right[0], doctest::Approx(1.0F));
    CHECK_EQ(basis.right[1], doctest::Approx(0.0F));
    CHECK_EQ(basis.right[2], doctest::Approx(0.0F));
    // up completes the right-handed frame.
    CHECK_EQ(basis.up[0], doctest::Approx(0.0F));
    CHECK_EQ(basis.up[1], doctest::Approx(0.8944272F));
    CHECK_EQ(basis.up[2], doctest::Approx(-0.4472136F));
    CHECK_EQ(glm::dot(basis.front, basis.right), doctest::Approx(0.0F));
    CHECK_EQ(glm::dot(basis.front, basis.up), doctest::Approx(0.0F));
  }

  TEST_CASE("Reflection plane passes through the defining points") {
    const glm::vec3 first{0.0F, 1.0F, 0.0F};
    const glm::vec3 second{1.0F, 1.0F, 0.0F};
    const glm::vec3 third{0.0F, 1.0F, 1.0F};

    const glm::vec4 plane{App::plane_from_points(first, second, third)};
    const glm::vec3 normal{glm::vec3{plane}};
    CHECK_EQ(glm::length(normal), doctest::Approx(1.0F));
    CHECK_EQ(glm::dot(normal, first) + plane[3], doctest::Approx(0.0F));
    CHECK_EQ(glm::dot(normal, second) + plane[3], doctest::Approx(0.0F));
    CHECK_EQ(glm::dot(normal, third) + plane[3], doctest::Approx(0.0F));
    // (second - first) x (third - first) = (1, 0, 0) x (0, 0, 1) = (0, -1, 0).
    CHECK_EQ(normal[1], doctest::Approx(-1.0F));
  }

  TEST_CASE("Points reflect symmetrically through a plane") {
    const glm::vec4 floor{0.0F, 1.0F, 0.0F, 0.0F};  // y = 0.
    const glm::vec3 above{0.5F, 2.0F, -0.5F};

    const glm::vec3 mirrored{App::reflect_point(above, floor)};
    CHECK_EQ(mirrored[0], doctest::Approx(0.5F));
    CHECK_EQ(mirrored[1], doctest::Approx(-2.0F));
    CHECK_EQ(mirrored[2], doctest::Approx(-0.5F));

    const glm::vec3 restored{App::reflect_point(mirrored, floor)};
    CHECK_EQ(glm::distance(restored, above), doctest::Approx(0.0F));
  }

  TEST_CASE("Reflected view mirrors the eye through the plane") {
    // Camera at (0, 2, 4) looking at the origin, mirrored through y = 0.
    const glm::mat4 view{glm::lookAt(glm::vec3{0.0F, 2.0F, 4.0F},
                                     glm::vec3{0.0F, 0.0F, 0.0F},
                                     glm::vec3{0.0F, 1.0F, 0.0F})};
    const glm::vec4 floor{0.0F, 1.0F, 0.0F, 0.0F};
    const glm::mat4 reflected{App::reflected_view_matrix(view, floor)};

    // The reflected eye is (0, -2, 4).
    const glm::mat4 inverse{glm::inverse(reflected)};
    CHECK_EQ(glm::vec3{inverse[3]}[1], doctest::Approx(-2.0F));
    // The origin (on the plane) lies sqrt(20) units ahead of the reflected eye.
    const glm::vec4 projected{reflected * glm::vec4{0.0F, 0.0F, 0.0F, 1.0F}};
    CHECK_EQ(projected[2], doctest::Approx(-std::sqrt(20.0F)));
  }

  TEST_CASE("Skybox view keeps rotation and drops translation") {
    const glm::mat4 view{glm::lookAt(glm::vec3{3.0F, 2.0F, 4.0F},
                                     glm::vec3{0.0F, 1.0F, 0.0F},
                                     glm::vec3{0.0F, 1.0F, 0.0F})};

    const glm::mat4 sky_view{App::skybox_view_matrix(view)};

    // The rotation block is preserved...
    const glm::mat3 rotation{glm::mat3{view}};
    const glm::mat3 sky_rotation{glm::mat3{sky_view}};
    for (int column{0}; column < 3; ++column) {
      for (int row{0}; row < 3; ++row) {
        CHECK_EQ(sky_rotation[column][row], doctest::Approx(rotation[column][row]));
      }
    }

    // ...while the translation column is zeroed, so the skybox camera sits
    // at the world origin.
    CHECK_EQ(sky_view[3][0], doctest::Approx(0.0F));
    CHECK_EQ(sky_view[3][1], doctest::Approx(0.0F));
    CHECK_EQ(sky_view[3][2], doctest::Approx(0.0F));
    const glm::mat4 inverse{glm::inverse(sky_view)};
    CHECK_EQ(glm::length(glm::vec3{inverse[3]}), doctest::Approx(0.0F));
  }

  TEST_CASE("Sky cubemap fades from bright zenith to dark nadir") {
    const auto faces{App::generate_sky_cubemap(8)};
    REQUIRE_EQ(faces.size(), std::size_t{6});
    const std::size_t bytes{std::size_t{8} * 8U * 4U};
    for (const auto& face : faces) {
      REQUIRE_EQ(face.size(), bytes);
    }

    // Face order +X, -X, +Y, -Y, +Z, -Z; the centre texel of the +Y face
    // points straight up and must be brighter than the -Y centre.
    const auto pixel_at{[](const std::vector<std::uint8_t>& face, const int px, const int py) {
      const std::size_t index{
          ((static_cast<std::size_t>(py) * 8U) + static_cast<std::size_t>(px)) * 4U};
      return std::array<std::uint8_t, 4>{
          face.at(index), face.at(index + 1U), face.at(index + 2U), face.at(index + 3U)};
    }};

    const auto up{pixel_at(faces.at(2), 4, 4)};
    const auto down{pixel_at(faces.at(3), 4, 4)};
    CHECK_GT(up.at(2), down.at(2));          // Zenith blue beats nadir blue.
    CHECK_EQ(up.at(3), std::uint8_t{255});   // Opaque everywhere.
    CHECK_EQ(down.at(3), std::uint8_t{255});
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c, misc-include-cleaner, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
