#include <doctest/doctest.h>

#include <array>
#include <span>
#include <string_view>

#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/Interface/DialogTextLayout.hpp"
#include "Core/Interface/FontManager.hpp"

namespace {

float monospace_measure(const std::string_view text) {
  return static_cast<float>(text.size()) * 7.0F;
}

}  // namespace

TEST_CASE("Dialog main text is left-layouted in a fixed 64-pixel viewport") {
  const auto one_line{App::Interface::format_dialog_text(
      "hello", 576.0F, 64.0F, 17.0F, monospace_measure)};
  REQUIRE(one_line.lines.size() == 1U);
  CHECK(one_line.lines.front().text == "hello");
  CHECK(one_line.formatted_height == doctest::Approx(17.0F));
  CHECK(one_line.max_scroll() == doctest::Approx(0.0F));
  CHECK(App::Interface::k_dialog_main_viewport_top == doctest::Approx(412.0F));
  CHECK(App::Interface::k_dialog_main_viewport_bottom == doctest::Approx(476.0F));

  const auto five_lines{App::Interface::format_dialog_text(
      "one\ntwo\nthree\nfour\nfive", 576.0F, 64.0F, 17.0F, monospace_measure)};
  CHECK(five_lines.lines.size() == 5U);
  CHECK(five_lines.formatted_height == doctest::Approx(85.0F));
  CHECK(five_lines.max_scroll() == doctest::Approx(21.0F));

  const auto wrapped{App::Interface::format_dialog_text(
      "AA AA", 20.0F, 64.0F, 17.0F, monospace_measure)};
  REQUIRE(wrapped.lines.size() == 2U);
  CHECK(wrapped.lines.at(0).text == "AA");
  CHECK(wrapped.lines.at(1).text == "AA");
}

TEST_CASE("Dialog responses bottom-align their actual formatted heights") {
  constexpr std::array<float, 3> heights{17.0F, 34.0F, 17.0F};
  const auto block{App::Interface::layout_dialog_responses(heights)};
  REQUIRE(block.response_tops.size() == heights.size());
  CHECK(block.total_height == doctest::Approx(68.0F));
  CHECK(block.top == doctest::Approx(380.0F));
  CHECK(block.response_tops.at(0) == doctest::Approx(380.0F));
  CHECK(block.response_tops.at(1) == doctest::Approx(397.0F));
  CHECK(block.response_tops.at(2) == doctest::Approx(431.0F));
  CHECK(block.bottom == doctest::Approx(448.0F));
  CHECK(App::Interface::k_dialog_text_left == doctest::Approx(32.0F));
  CHECK(App::Interface::k_dialog_text_width == doctest::Approx(576.0F));
}

TEST_CASE("Dialog presenter uses D and retail authored colours without markers") {
  CHECK(App::Interface::dialog_font_key() == 'D');
  CHECK(App::Interface::dialog_main_tint(
            App::Dialog::DialogState::k_presenting_automatic_player_line) ==
      App::Interface::k_dialog_automatic_tint);
  CHECK(App::Interface::dialog_main_tint(App::Dialog::DialogState::k_presenting_line) ==
      App::Interface::k_dialog_white);
  CHECK(App::Interface::dialog_response_tint(true) == App::Interface::k_dialog_white);
  CHECK(App::Interface::dialog_response_tint(false) ==
      App::Interface::k_dialog_unselected_tint);

  const auto response{App::Interface::format_dialog_text(
      "Authored response", 576.0F, 96.0F, 17.0F, monospace_measure)};
  REQUIRE(response.lines.size() == 1U);
  CHECK(response.lines.front().text == "Authored response");

  const auto dialog_entry{App::Interface::FontManager::font_registry_entry('D')};
  const auto selection_entry{App::Interface::FontManager::font_registry_entry('R')};
  REQUIRE(dialog_entry.has_value());
  REQUIRE(selection_entry.has_value());
  const App::Interface::FontRegistryEntry dialog_metrics{dialog_entry.value_or({})};
  const App::Interface::FontRegistryEntry selection_metrics{selection_entry.value_or({})};
  CHECK(dialog_metrics.logical_name == "DIALOGUE");
  CHECK(dialog_metrics.letter_spacing == 1);
  CHECK(dialog_metrics.blank_width == 6);
  CHECK(dialog_metrics.line_height == 17);
  CHECK(selection_metrics.logical_name == "DIALSELE");
  CHECK(selection_metrics.letter_spacing == -1);
  CHECK(selection_metrics.blank_width == 6);
  CHECK(selection_metrics.line_height == 17);
}

TEST_CASE("Dialog held scrolling is presentation-owned and generation-reset") {
  App::Interface::DialogScrollState scroll;
  CHECK(scroll.observe_generation(4U));
  CHECK(scroll.offset() == doctest::Approx(0.0F));
  scroll.set_maximum(21.0F);
  scroll.update(1.0F, false, true);
  CHECK(scroll.offset() == doctest::Approx(21.0F));
  CHECK(scroll.clamped(21.0F) == doctest::Approx(21.0F));
  CHECK_FALSE(scroll.observe_generation(4U));
  CHECK(scroll.offset() == doctest::Approx(21.0F));
  CHECK(scroll.observe_generation(5U));
  CHECK(scroll.offset() == doctest::Approx(0.0F));
  scroll.update(1.0F, true, false);
  CHECK(scroll.offset() == doctest::Approx(0.0F));
}
