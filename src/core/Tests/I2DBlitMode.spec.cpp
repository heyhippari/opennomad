#include <doctest/doctest.h>

#include "Core/Interface/I2DModel.hpp"

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

namespace {

using App::Interface::I2DBitmapElement;
using App::Interface::resolve_bitmap_blit_options;
using App::Interface::uses_destination_color_key;
using App::Interface::uses_source_color_key;

}  // namespace

TEST_SUITE("Core::Interface::I2D blit mode") {
  TEST_CASE("Blit mode bits map to source and destination colour keys") {
    CHECK_FALSE(uses_source_color_key(0x00));
    CHECK_FALSE(uses_destination_color_key(0x00));

    CHECK(uses_source_color_key(0x01));
    CHECK_FALSE(uses_destination_color_key(0x01));

    CHECK_FALSE(uses_source_color_key(0x02));
    CHECK(uses_destination_color_key(0x02));

    CHECK(uses_source_color_key(0x03));
    CHECK(uses_destination_color_key(0x03));
  }

  TEST_CASE("Bit 0 enables the source colour key") {
    const auto options{resolve_bitmap_blit_options(I2DBitmapElement{.runtime_blit_mode = 0x03})};
    REQUIRE(options.source_colour_key.has_value());
    if (options.source_colour_key.has_value()) {
      const auto& key{options.source_colour_key.value()};
      CHECK_EQ(key.at(0), 0.0F);
      CHECK_EQ(key.at(1), 0.0F);
      CHECK_EQ(key.at(2), 0.0F);
    }
  }

  TEST_CASE("Bit 1 alone leaves the source colour key disabled") {
    const auto options{resolve_bitmap_blit_options(I2DBitmapElement{.runtime_blit_mode = 0x02})};
    CHECK_FALSE(options.source_colour_key.has_value());
  }

  TEST_CASE("Blit mode 0 leaves the source colour key disabled") {
    const auto options{resolve_bitmap_blit_options(I2DBitmapElement{.runtime_blit_mode = 0x00})};
    CHECK_FALSE(options.source_colour_key.has_value());
  }

  TEST_CASE("Default element has no blit mode") {
    const I2DBitmapElement element{};
    CHECK_EQ(element.runtime_blit_mode, 0);
    CHECK_FALSE(resolve_bitmap_blit_options(element).source_colour_key.has_value());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)
