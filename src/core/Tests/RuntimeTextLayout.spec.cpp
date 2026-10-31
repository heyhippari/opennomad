#include "Core/Interface/RuntimeTextLayout.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>

#include "Core/Interface/RuntimeText.hpp"

namespace {

[[nodiscard]] App::Interface::RuntimeGlyphMetrics fixed_metrics(
    const std::uint8_t font_key, const std::uint8_t /*text_byte*/) {
  return {
      .advance_x = font_key == 'S' ? 5.0F : 10.0F, .line_height = font_key == 'S' ? 8.0F : 12.0F};
}

}  // namespace

TEST_SUITE("Core::Interface::RuntimeTextLayout") {
  TEST_CASE("preserves independent font runs and exact byte provenance") {
    const auto document{App::Interface::parse_runtime_text("A{fS}BC{fJ}D")};
    const auto layout{App::Interface::layout_runtime_text(document, fixed_metrics)};
    REQUIRE_EQ(layout.glyphs.size(), 4);
    CHECK_EQ(layout.glyphs.at(0).style.font_key, 'D');
    CHECK_EQ(layout.glyphs.at(1).style.font_key, 'S');
    CHECK_EQ(layout.glyphs.at(2).style.font_key, 'S');
    CHECK_EQ(layout.glyphs.at(3).style.font_key, 'J');
    CHECK_EQ(layout.glyphs.at(1).source_offset, 5);
  }

  TEST_CASE("applies alignment absolute position and mode 0x10 boundary") {
    // Runtime right-aligns against the region's right bound; {X} only moves
    // the cursor (and the left-aligned origin).
    const auto right{App::Interface::layout_runtime_text(
        App::Interface::parse_runtime_text("{X090058}{D}AB"), fixed_metrics)};
    CHECK(right.glyphs.at(0).x == doctest::Approx(588.0F));
    CHECK(right.glyphs.at(0).y == doctest::Approx(278.4F));

    const auto forced_left{App::Interface::layout_runtime_text(
        App::Interface::parse_runtime_text("{F}AB"), fixed_metrics)};
    CHECK(forced_left.glyphs.at(0).x == doctest::Approx(32.0F));
    CHECK_EQ(forced_left.format_boundary_source_offsets.size(), 1);
  }

  TEST_CASE("wraps with per-font metrics and preserves authored breaks") {
    App::Interface::RuntimeTextLayoutOptions options;
    options.left = 0.0F;
    options.right = 25.0F;
    options.top = 0.0F;
    options.bottom = 100.0F;
    options.initial_style.horizontal = App::Interface::RuntimeHorizontalMode::k_left;
    options.initial_style.vertical = App::Interface::RuntimeVerticalMode::k_top;
    const auto layout{App::Interface::layout_runtime_text(
        App::Interface::parse_runtime_text("AA BB\n{fS}CCC"), fixed_metrics, options)};
    REQUIRE_EQ(layout.lines.size(), 3);
    CHECK_FALSE(layout.lines.at(0).authored_break_after);
    CHECK(layout.lines.at(1).authored_break_after);
    CHECK_EQ(layout.lines.at(2).height, doctest::Approx(8.0F));

    const auto leading_space{App::Interface::layout_runtime_text(
        App::Interface::parse_runtime_text(" A"), fixed_metrics, options)};
    REQUIRE_EQ(leading_space.glyphs.size(), 2);
    CHECK_EQ(leading_space.glyphs.front().text_byte, ' ');
  }

  TEST_CASE("places each credit line at its own absolute origin") {
    App::Interface::RuntimeTextLayoutOptions options;
    options.left = 16.0F;
    options.right = 624.0F;
    options.top = 0.0F;
    options.bottom = 480.0F;
    options.initial_style.horizontal = App::Interface::RuntimeHorizontalMode::k_left;
    options.initial_style.vertical = App::Interface::RuntimeVerticalMode::k_top;
    const auto layout{App::Interface::layout_runtime_text(
        App::Interface::parse_runtime_text(
            "{X010030}{f1}Written\r\n{X020035}{f3}David\r\nCAGE\r\n{X050050}{D}Right"),
        fixed_metrics,
        options)};
    REQUIRE_EQ(layout.lines.size(), 4);
    CHECK(layout.lines.at(0).x == doctest::Approx(64.0F));
    CHECK(layout.lines.at(0).y == doctest::Approx(144.0F));
    CHECK(layout.lines.at(1).x == doctest::Approx(128.0F));
    CHECK(layout.lines.at(1).y == doctest::Approx(168.0F));
    // A line break keeps the {X} origin and advances by 120% of 12 units.
    CHECK(layout.lines.at(2).x == doctest::Approx(128.0F));
    CHECK(layout.lines.at(2).y == doctest::Approx(182.0F));
    CHECK(layout.lines.at(3).x == doctest::Approx(624.0F - 50.0F));
    CHECK(layout.lines.at(3).y == doctest::Approx(240.0F));
  }

  TEST_CASE("bottom-anchors plain world text in the VOIXOFF region") {
    const auto layout{App::Interface::layout_runtime_world_text(
        App::Interface::parse_runtime_text("AB\nCD"), fixed_metrics)};
    REQUIRE_EQ(layout.lines.size(), 2);
    CHECK_EQ(layout.glyphs.at(0).style.font_key, 'V');
    CHECK(layout.lines.at(0).x == doctest::Approx(16.0F));
    // Two 14-unit advances measured, then placed 16 units above the bottom.
    CHECK(layout.lines.at(0).y == doctest::Approx(480.0F - 28.0F - 16.0F));
    CHECK(layout.lines.at(1).y == doctest::Approx(480.0F - 14.0F - 16.0F));
  }

  TEST_CASE("wraps at the last space and resumes after it") {
    App::Interface::RuntimeTextLayoutOptions options;
    options.left = 0.0F;
    options.right = 55.0F;
    options.top = 0.0F;
    options.initial_style.horizontal = App::Interface::RuntimeHorizontalMode::k_left;
    options.initial_style.vertical = App::Interface::RuntimeVerticalMode::k_top;
    const auto layout{App::Interface::layout_runtime_text(
        App::Interface::parse_runtime_text("AA BB CC"), fixed_metrics, options)};
    REQUIRE_EQ(layout.lines.size(), 2);
    CHECK_EQ(layout.lines.at(0).glyph_count, 2);
    CHECK_EQ(layout.lines.at(1).glyph_count, 5);
    CHECK(layout.lines.at(1).y == doctest::Approx(14.0F));
  }

  TEST_CASE("applies selected style only to the selected flat span") {
    App::Interface::RuntimeTextLayoutOptions options;
    options.selected_span_index = 1;
    options.selected_style.font_key = 'R';
    options.selected_style.color = {1U, 2U, 3U};
    options.selected_style.auxiliary_e = '!';
    const auto layout{App::Interface::layout_runtime_text(
        App::Interface::parse_runtime_text("[one][two][three]"), fixed_metrics, options)};
    const std::string visible{App::Interface::runtime_text_plain_bytes(
        App::Interface::parse_runtime_text("[one][two][three]"))};
    REQUIRE_EQ(layout.glyphs.size(), visible.size());
    CHECK_EQ(layout.glyphs.at(2).style.font_key, 'D');
    CHECK_EQ(layout.glyphs.at(3).style.font_key, 'R');
    CHECK_EQ(layout.glyphs.at(5).style.color, options.selected_style.color);
    CHECK_EQ(layout.glyphs.at(5).style.auxiliary_e, '!');
    CHECK_EQ(layout.glyphs.at(6).style.font_key, 'D');
    CHECK_EQ(layout.glyphs.at(6).style.auxiliary_e, 0);
  }

  TEST_CASE("keeps persistent colour flash and auxiliary style state") {
    const auto layout{App::Interface::layout_runtime_text(
        App::Interface::parse_runtime_text("{I010020030}{B}{E!}A{B}B"), fixed_metrics)};
    REQUIRE_EQ(layout.glyphs.size(), 2);
    CHECK_EQ(layout.glyphs.at(0).style.color, (std::array<std::uint8_t, 3>{10U, 20U, 30U}));
    CHECK(layout.glyphs.at(0).style.flash_red);
    CHECK_EQ(layout.glyphs.at(0).style.auxiliary_e, '!');
    CHECK_FALSE(layout.glyphs.at(1).style.flash_red);
  }
}