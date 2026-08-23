#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <span>
#include <vector>

#include "Core/Camera.hpp"
#include "Core/ColorManagement.hpp"
#include "Core/Reflection.hpp"
#include "Core/Texture.hpp"
#include "Core/TextureCube.hpp"
#include "Core/Vertex.hpp"
#include "Core/WorldColorPipeline.hpp"

TEST_SUITE("Core::Rendering") {
  TEST_CASE("Standard sRGB transfer functions preserve their exact boundaries") {
    using App::ColorManagement::linear_to_srgb;
    using App::ColorManagement::srgb_to_linear;

    CHECK_EQ(srgb_to_linear(0.0F), doctest::Approx(0.0F));
    CHECK_EQ(srgb_to_linear(1.0F), doctest::Approx(1.0F));
    CHECK_EQ(srgb_to_linear(0.04045F), doctest::Approx(0.04045F / 12.92F));
    CHECK_EQ(linear_to_srgb(0.0031308F), doctest::Approx(12.92F * 0.0031308F));
    CHECK_EQ(srgb_to_linear(0.5F), doctest::Approx(0.21404114F));

    constexpr std::array<float, 7> k_encoded_values{
        0.0F, 0.01F, 0.04045F, 0.18F, 0.5F, 0.75F, 1.0F};
    for (const float encoded : k_encoded_values) {
      CHECK_EQ(linear_to_srgb(srgb_to_linear(encoded)), doctest::Approx(encoded).epsilon(1.0e-5));
    }
  }

  TEST_CASE("Texture color policies select explicit GL storage without a context") {
    CHECK_EQ(
        App::texture_upload_internal_format(App::TextureColorEncoding::k_srgb), GL_SRGB8_ALPHA8);
    CHECK_EQ(
        App::texture_upload_internal_format(App::TextureColorEncoding::k_legacy_encoded), GL_RGBA8);
    CHECK_EQ(App::texture_upload_internal_format(App::TextureColorEncoding::k_linear), GL_RGBA8);
    CHECK_EQ(App::k_modern_color_texture_policy.encoding, App::TextureColorEncoding::k_srgb);
    CHECK_EQ(
        App::k_legacy_effect_texture_policy.encoding, App::TextureColorEncoding::k_legacy_encoded);
    CHECK_EQ(App::k_linear_data_texture_policy.encoding, App::TextureColorEncoding::k_linear);
    CHECK_EQ(App::k_modern_color_texture_policy.filter, App::TextureFilter::k_linear);
  }

  TEST_CASE("World color targets expose their semantic domains and precision") {
    CHECK_EQ(App::k_legacy_accumulator_target_description.color_encoding,
        App::TextureColorEncoding::k_legacy_encoded);
    CHECK_EQ(App::texture_storage_internal_format(
                 App::k_legacy_accumulator_target_description.color_storage),
        GL_RGBA16);
    CHECK_EQ(App::k_legacy_accumulator_target_description.depth_stencil,
        App::DepthStencilFormat::k_depth24_stencil8);
    CHECK_EQ(
        App::k_linear_scene_target_description.color_encoding, App::TextureColorEncoding::k_linear);
    CHECK_EQ(
        App::texture_storage_internal_format(App::k_linear_scene_target_description.color_storage),
        GL_RGBA16F);
    CHECK_EQ(App::k_linear_scene_target_description.depth_stencil,
        App::DepthStencilFormat::k_depth24_stencil8);
  }

  TEST_CASE("Legacy operators preserve SDR equivalence and defined HDR excess") {
    using App::ColorManagement::legacy_additive;
    using App::ColorManagement::legacy_alpha_over;
    using App::ColorManagement::legacy_darken;
    using App::ColorManagement::legacy_subtractive;
    using App::ColorManagement::linear_to_srgb;
    using App::ColorManagement::split_sdr_base_and_hdr_excess;
    using App::ColorManagement::srgb_to_linear;

    const float destination{srgb_to_linear(0.4F)};
    CHECK_EQ(linear_to_srgb(legacy_alpha_over(destination, 0.4F, 0.5F)), doctest::Approx(0.6F));
    CHECK_EQ(linear_to_srgb(legacy_additive(destination, 0.2F)), doctest::Approx(0.6F));
    CHECK_EQ(linear_to_srgb(legacy_darken(destination, 0.5F)), doctest::Approx(0.2F));
    CHECK_EQ(linear_to_srgb(legacy_subtractive(destination, 0.15F)), doctest::Approx(0.25F));

    const auto negative{split_sdr_base_and_hdr_excess(-2.0F)};
    CHECK_EQ(negative.base, 0.0F);
    CHECK_EQ(negative.excess, 0.0F);

    constexpr float k_hdr_destination{1.75F};
    CHECK_EQ(legacy_additive(k_hdr_destination, 0.2F), doctest::Approx(k_hdr_destination));
    CHECK_EQ(
        legacy_subtractive(k_hdr_destination, 0.2F), doctest::Approx(srgb_to_linear(0.8F) + 0.75F));
    CHECK_EQ(
        legacy_darken(k_hdr_destination, 0.5F), doctest::Approx(srgb_to_linear(0.5F) + 0.375F));
    CHECK_EQ(legacy_alpha_over(k_hdr_destination, 0.25F, 0.5F),
        doctest::Approx(srgb_to_linear(0.75F) + 0.375F));
  }

  TEST_CASE("Aggregated alpha accumulator matches ordered encoded alpha-over") {
    using App::ColorManagement::legacy_alpha_over;
    using App::ColorManagement::linear_to_srgb;
    using App::ColorManagement::srgb_to_linear;
    constexpr float k_first_alpha{0.25F};
    constexpr float k_second_alpha{0.5F};
    constexpr float k_first_color{0.8F};
    constexpr float k_second_color{0.2F};
    const float aggregate_coverage{k_second_alpha + (k_first_alpha * (1.0F - k_second_alpha))};
    const float aggregate_premultiplied{(k_second_color * k_second_alpha) +
                                        (k_first_color * k_first_alpha * (1.0F - k_second_alpha))};
    const float result{
        legacy_alpha_over(srgb_to_linear(0.4F), aggregate_premultiplied, aggregate_coverage)};
    const float expected_encoded{
        (k_second_color * k_second_alpha) +
        (((k_first_color * k_first_alpha) + (0.4F * (1.0F - k_first_alpha))) *
            (1.0F - k_second_alpha))};
    CHECK_EQ(linear_to_srgb(result), doctest::Approx(expected_encoded));
  }

  TEST_CASE("Legacy operator aggregation matches sequential SDR composition") {
    using App::ColorManagement::legacy_additive;
    using App::ColorManagement::legacy_darken;
    using App::ColorManagement::legacy_subtractive;
    using App::ColorManagement::srgb_to_linear;
    const float destination{srgb_to_linear(0.55F)};

    CHECK_EQ(legacy_additive(destination, 0.10F + 0.15F),
        doctest::Approx(legacy_additive(legacy_additive(destination, 0.10F), 0.15F)));
    CHECK_EQ(legacy_subtractive(destination, 0.10F + 0.15F),
        doctest::Approx(legacy_subtractive(legacy_subtractive(destination, 0.10F), 0.15F)));
    const float combined_darken_factor{(1.0F - 0.20F) * (1.0F - 0.35F)};
    CHECK_EQ(legacy_darken(destination, combined_darken_factor),
        doctest::Approx(legacy_darken(legacy_darken(destination, 1.0F - 0.20F), 1.0F - 0.35F)));
  }

  TEST_CASE("Display transform clamps before the exact OETF") {
    using App::ColorManagement::linear_scene_to_sdr;
    CHECK_EQ(linear_scene_to_sdr(-1.0F), 0.0F);
    CHECK_EQ(linear_scene_to_sdr(2.0F), doctest::Approx(1.0F));
  }

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
    const auto pixels{App::Texture2D::generate_checkerboard(16, 16, 4, color_a, color_b)};

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
    const glm::mat4 view{glm::lookAt(
        glm::vec3{0.0F, 2.0F, 4.0F}, glm::vec3{0.0F, 0.0F, 0.0F}, glm::vec3{0.0F, 1.0F, 0.0F})};

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
    const glm::mat4 view{glm::lookAt(
        glm::vec3{0.0F, 2.0F, 4.0F}, glm::vec3{0.0F, 0.0F, 0.0F}, glm::vec3{0.0F, 1.0F, 0.0F})};
    const glm::vec4 floor{0.0F, 1.0F, 0.0F, 0.0F};
    const glm::mat4 reflected{App::reflected_view_matrix(view, floor)};

    // The reflected eye is (0, -2, 4).
    const glm::mat4 inverse{glm::inverse(reflected)};
    CHECK_EQ(glm::vec3{inverse[3]}[1], doctest::Approx(-2.0F));
    // The origin (on the plane) lies sqrt(20) units ahead of the reflected eye.
    const glm::vec4 projected{reflected * glm::vec4{0.0F, 0.0F, 0.0F, 1.0F}};
    CHECK_EQ(projected[2], doctest::Approx(-std::sqrt(20.0F)));
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
    CHECK_GT(up.at(2), down.at(2));         // Zenith blue beats nadir blue.
    CHECK_EQ(up.at(3), std::uint8_t{255});  // Opaque everywhere.
    CHECK_EQ(down.at(3), std::uint8_t{255});
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
