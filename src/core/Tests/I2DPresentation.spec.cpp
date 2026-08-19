#include <doctest/doctest.h>

#include <utility>

#include "Core/Interface/FontManager.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/I2DPresentation.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace)

namespace {

TEST_SUITE("Core::Interface::I2DPresentation") {
  TEST_CASE("640x480 maps 1:1 to the recovered reference canvas") {
    const auto transform{App::Interface::make_presentation_transform(640, 480)};
    CHECK_EQ(transform.pixel_width, 640);
    CHECK_EQ(transform.pixel_height, 480);
    CHECK(transform.pixels_per_reference_unit == doctest::Approx(1.0F));
    CHECK(transform.logical_left == doctest::Approx(0.0F));
    CHECK(transform.logical_right == doctest::Approx(640.0F));
    CHECK(transform.logical_top == doctest::Approx(0.0F));
    CHECK(transform.logical_bottom == doctest::Approx(480.0F));
  }

  TEST_CASE("Vertical range is always 0..480") {
    for (const auto [width, height] :
        {std::pair{800, 600},
            std::pair{1280, 720},
            std::pair{1920, 1080},
            std::pair{2560, 1440},
            std::pair{3440, 1440},
            std::pair{3840, 2160}}) {
      const auto transform{App::Interface::make_presentation_transform(width, height)};
      CHECK(transform.logical_top == doctest::Approx(0.0F));
      CHECK(transform.logical_bottom == doctest::Approx(480.0F));
      CHECK(transform.pixels_per_reference_unit ==
            doctest::Approx(static_cast<float>(height) / 480.0F));
    }
  }

  TEST_CASE("16:9 exposes ~-106.667..746.667 logical width") {
    const auto full_hd{App::Interface::make_presentation_transform(1920, 1080)};
    CHECK(full_hd.pixels_per_reference_unit == doctest::Approx(2.25F));
    CHECK(full_hd.logical_left == doctest::Approx(-106.6667F).epsilon(1e-3F));
    CHECK(full_hd.logical_right == doctest::Approx(746.6667F).epsilon(1e-3F));

    const auto quad_hd{App::Interface::make_presentation_transform(2560, 1440)};
    CHECK(quad_hd.logical_left == doctest::Approx(-106.6667F).epsilon(1e-3F));
    CHECK(quad_hd.logical_right == doctest::Approx(746.6667F).epsilon(1e-3F));
  }

  TEST_CASE("Ultrawide extends the logical range symmetrically") {
    const auto transform{App::Interface::make_presentation_transform(3440, 1440)};
    CHECK(transform.pixels_per_reference_unit == doctest::Approx(3.0F));
    CHECK(transform.logical_left == doctest::Approx(-253.3333F).epsilon(1e-3F));
    CHECK(transform.logical_right == doctest::Approx(893.3333F).epsilon(1e-3F));
  }

  TEST_CASE("Reference x=320 maps to the physical screen centre") {
    for (const auto [width, height] :
        {std::pair{640, 480}, std::pair{1920, 1080}, std::pair{3440, 1440}}) {
      const auto transform{App::Interface::make_presentation_transform(width, height)};
      // physical_x = (logical_x - logical_left) * scale; at logical_x=320 it
      // must equal half the physical width.
      const float physical_x{(320.0F - transform.logical_left) *
                             transform.pixels_per_reference_unit};
      CHECK(physical_x == doctest::Approx(static_cast<float>(width) / 2.0F));
    }
  }
}

TEST_SUITE("Core::Interface::I2DTopCenterPlacement") {
  using App::Interface::I2DRect;
  using App::Interface::compute_top_center_placement;

  TEST_CASE("Landscape keeps the recovered logo rectangle + top margin") {
    const I2DRect destination{.x = 0, .y = 0, .width = 640, .height = 150};
    const auto placement{
        compute_top_center_placement(destination, 8.0F, true, 1920, 1080)};

    // Horizontally centred on the reference canvas (x=320).
    CHECK((placement.x0 + placement.x1) / 2.0F == doctest::Approx(320.0F));
    CHECK(placement.x0 == doctest::Approx(0.0F));
    CHECK(placement.x1 == doctest::Approx(640.0F));
    // 8 reference units of top margin, height preserved.
    CHECK(placement.y0 == doctest::Approx(8.0F));
    CHECK(placement.y1 == doctest::Approx(158.0F));
  }

  TEST_CASE("Presentation scale shrinks the logo around its horizontal centre") {
    const I2DRect destination{.x = 0, .y = 0, .width = 640, .height = 150};
    const auto placement{
        compute_top_center_placement(
            destination, 12.0F, true, 1920, 1080, 0.84F)};

    CHECK((placement.x0 + placement.x1) / 2.0F ==
          doctest::Approx(320.0F));
    CHECK(placement.x1 - placement.x0 ==
          doctest::Approx(537.6F));
    CHECK(placement.y0 == doctest::Approx(12.0F));
    CHECK(placement.y1 - placement.y0 ==
          doctest::Approx(126.0F));
  }

  TEST_CASE("Margin is a constant fraction of screen height") {
    const I2DRect destination{.x = 0, .y = 0, .width = 640, .height = 150};
    const float top_margin{8.0F};
    for (const auto [width, height] :
        {std::pair{1920, 1080}, std::pair{2560, 1440}, std::pair{3840, 2160}}) {
      const auto placement{
          compute_top_center_placement(destination, top_margin, true, width, height)};
      // The reference-space margin is constant; the physical margin is
      // margin * scale = margin * height/480, so margin/height = 8/480.
      CHECK(placement.y0 == doctest::Approx(top_margin));
      const float scale{static_cast<float>(height) / 480.0F};
      const float physical_margin{
          (placement.y0 - static_cast<float>(destination.y)) * scale};
      CHECK(physical_margin / static_cast<float>(height) ==
            doctest::Approx(top_margin / 480.0F));
    }
  }

  TEST_CASE("Narrow viewport clamps the logo width to stay visible") {
    const I2DRect destination{.x = 0, .y = 0, .width = 640, .height = 150};
    // 640x720: scale = 1.5, width capacity = 1.0 -> factor = 2/3.
    const auto placement{
        compute_top_center_placement(destination, 8.0F, true, 640, 720)};

    const float width{placement.x1 - placement.x0};
    CHECK(width < doctest::Approx(640.0F));
    // Still centred.
    CHECK((placement.x0 + placement.x1) / 2.0F == doctest::Approx(320.0F));
    // Aspect preserved: width/height stays 640/150.
    const float height{placement.y1 - placement.y0};
    CHECK(width / height == doctest::Approx(640.0F / 150.0F));
  }

  TEST_CASE("Clamp disabled leaves the width unchanged on narrow viewports") {
    const I2DRect destination{.x = 0, .y = 0, .width = 640, .height = 150};
    const auto placement{
        compute_top_center_placement(destination, 8.0F, false, 640, 720)};
    CHECK(placement.x1 - placement.x0 == doctest::Approx(640.0F));
  }
}

TEST_SUITE("Core::Interface::FontManagerRasterBuckets") {
  TEST_CASE("Raster buckets track the presentation scale") {
    using App::Interface::FontManager;
    CHECK_EQ(FontManager::raster_bucket(1.0F), 30);    // 480p
    CHECK_EQ(FontManager::raster_bucket(1.5F), 46);    // 720p (45 -> nearest even)
    CHECK_EQ(FontManager::raster_bucket(2.25F), 68);   // 1080p (67.5 -> 68)
    CHECK_EQ(FontManager::raster_bucket(3.0F), 90);    // 1440p
    CHECK_EQ(FontManager::raster_bucket(4.5F), 136);   // 2160p (135 -> nearest even)
  }

  TEST_CASE("Buckets are always even (2 px quantization)") {
    using App::Interface::FontManager;
    for (int step{0}; step < 24; ++step) {
      const float scale{1.0F + (static_cast<float>(step) * 0.17F)};
      CHECK(FontManager::raster_bucket(scale) % 2U == 0U);
    }
  }
}

}  // namespace

// NOLINTEND(misc-use-anonymous-namespace)
