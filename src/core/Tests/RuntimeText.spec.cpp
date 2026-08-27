#include "Core/Interface/RuntimeText.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

using App::Interface::RuntimeTextDocument;
using App::Interface::RuntimeTextEvent;
using App::Interface::RuntimeTextEventType;

[[nodiscard]] std::vector<const RuntimeTextEvent*> events_of_type(
    const RuntimeTextDocument& document, const RuntimeTextEventType type) {
  std::vector<const RuntimeTextEvent*> matches;
  for (const auto& event : document.events()) {
    if (event.type == type) {
      matches.push_back(&event);
    }
  }
  return matches;
}

}  // namespace

TEST_SUITE("Core::Interface::RuntimeText") {
  TEST_CASE("parses persistent font and alignment commands byte by byte") {
    const auto impasse{App::Interface::parse_runtime_text("{fD}You have been the victim...")};
    CHECK_EQ(App::Interface::runtime_text_plain_bytes(impasse), "You have been the victim...");
    REQUIRE_EQ(events_of_type(impasse, RuntimeTextEventType::k_set_font).size(), 1);
    CHECK_EQ(events_of_type(impasse, RuntimeTextEventType::k_set_font).front()->byte_value, 'D');

    const auto runs{
        App::Interface::parse_runtime_text("A Pass to enter the {fS}Jaunpur{fJ} district")};
    CHECK_EQ(
        App::Interface::runtime_text_plain_bytes(runs), "A Pass to enter the Jaunpur district");
    const auto fonts{events_of_type(runs, RuntimeTextEventType::k_set_font)};
    REQUIRE_EQ(fonts.size(), 2);
    CHECK_EQ(fonts.at(0)->byte_value, 'S');
    CHECK_EQ(fonts.at(1)->byte_value, 'J');

    const auto medikit{App::Interface::parse_runtime_text("{fRC}LARGE MEDIKIT")};
    CHECK_EQ(events_of_type(medikit, RuntimeTextEventType::k_set_font).front()->byte_value, 'R');
    CHECK_EQ(
        events_of_type(medikit, RuntimeTextEventType::k_set_horizontal_mode).front()->horizontal,
        App::Interface::RuntimeHorizontalMode::k_center);

    const auto memorized{App::Interface::parse_runtime_text("{HDfR}DATA MEMORIZED")};
    CHECK_EQ(events_of_type(memorized, RuntimeTextEventType::k_set_vertical_mode).front()->vertical,
        App::Interface::RuntimeVerticalMode::k_top);
    CHECK_EQ(
        events_of_type(memorized, RuntimeTextEventType::k_set_horizontal_mode).front()->horizontal,
        App::Interface::RuntimeHorizontalMode::k_right);
    CHECK_EQ(events_of_type(memorized, RuntimeTextEventType::k_set_font).front()->byte_value, 'R');
  }

  TEST_CASE("parses colour flash position and neutral recovered commands") {
    const auto red{App::Interface::parse_runtime_text("{I255000000}Red")};
    CHECK_EQ(events_of_type(red, RuntimeTextEventType::k_set_color).front()->color,
        std::array<std::uint8_t, 3>{255U, 0U, 0U});

    const auto flashing{App::Interface::parse_runtime_text("{B}Warning{B} normal")};
    CHECK_EQ(events_of_type(flashing, RuntimeTextEventType::k_toggle_flash_red).size(), 2);
    const App::Interface::RuntimeTextStyle style{.color = {12U, 34U, 56U}, .flash_red = true};
    CHECK_EQ(App::Interface::resolve_runtime_text_color(style, 0), style.color);
    CHECK_EQ(App::Interface::resolve_runtime_text_color(style, 499), style.color);
    CHECK_EQ(App::Interface::resolve_runtime_text_color(style, 500),
        std::array<std::uint8_t, 3>{255U, 0U, 0U});

    const auto positioned{App::Interface::parse_runtime_text("{X050050}Text")};
    const auto* position{
        events_of_type(positioned, RuntimeTextEventType::k_absolute_position).front()};
    CHECK(position->position_x == doctest::Approx(320.0F));
    CHECK(position->position_y == doctest::Approx(240.0F));

    const auto credits{App::Interface::parse_runtime_text("{X090058}{f1}{D}Programming Directors")};
    CHECK(events_of_type(credits, RuntimeTextEventType::k_absolute_position).front()->position_x ==
          doctest::Approx(576.0F));
    CHECK(events_of_type(credits, RuntimeTextEventType::k_absolute_position).front()->position_y ==
          doctest::Approx(278.4F));
    CHECK_EQ(events_of_type(credits, RuntimeTextEventType::k_set_font).front()->byte_value, '1');
    CHECK_EQ(
        events_of_type(credits, RuntimeTextEventType::k_set_horizontal_mode).front()->horizontal,
        App::Interface::RuntimeHorizontalMode::k_right);

    const auto auxiliary{App::Interface::parse_runtime_text("{E!}text")};
    CHECK_EQ(events_of_type(auxiliary, RuntimeTextEventType::k_set_auxiliary_e).front()->byte_value,
        '!');
    CHECK_EQ(App::Interface::runtime_text_plain_bytes(auxiliary), "text");

    const auto boundary{App::Interface::parse_runtime_text("{F}text")};
    CHECK_EQ(
        events_of_type(boundary, RuntimeTextEventType::k_set_horizontal_mode).front()->horizontal,
        App::Interface::RuntimeHorizontalMode::k_mode_0x10);
    CHECK_EQ(events_of_type(boundary, RuntimeTextEventType::k_format_boundary).size(), 1);
    CHECK_EQ(
        App::Interface::runtime_text_plain_bytes(App::Interface::parse_runtime_text("{g}{Q}text")),
        "text");

    const auto remaining_modes{App::Interface::parse_runtime_text("{GLM}text")};
    CHECK_EQ(events_of_type(remaining_modes, RuntimeTextEventType::k_set_horizontal_mode)
                 .front()
                 ->horizontal,
        App::Interface::RuntimeHorizontalMode::k_left);
    const auto vertical_modes{
        events_of_type(remaining_modes, RuntimeTextEventType::k_set_vertical_mode)};
    REQUIRE_EQ(vertical_modes.size(), 2);
    CHECK_EQ(vertical_modes.at(0)->vertical, App::Interface::RuntimeVerticalMode::k_bottom);
    CHECK_EQ(vertical_modes.at(1)->vertical, App::Interface::RuntimeVerticalMode::k_middle);
  }

  TEST_CASE("swallows unknown command bytes after a consumed font operand") {
    const auto document{App::Interface::parse_runtime_text("{fI225120045}")};
    CHECK_EQ(events_of_type(document, RuntimeTextEventType::k_set_font).front()->byte_value, 'I');
    CHECK(events_of_type(document, RuntimeTextEventType::k_set_color).empty());
    CHECK(App::Interface::runtime_text_plain_bytes(document).empty());
  }

  TEST_CASE("preserves flat selectable ordinals and byte indexed text") {
    const auto spans{App::Interface::parse_runtime_text("[one][two][three]")};
    const auto begins{events_of_type(spans, RuntimeTextEventType::k_selectable_span_begin)};
    const auto ends{events_of_type(spans, RuntimeTextEventType::k_selectable_span_end)};
    REQUIRE_EQ(begins.size(), 3);
    REQUIRE_EQ(ends.size(), 3);
    CHECK_EQ(begins.at(0)->span_ordinal, 0);
    CHECK_EQ(begins.at(1)->span_ordinal, 1);
    CHECK_EQ(begins.at(2)->span_ordinal, 2);
    CHECK_EQ(ends.at(2)->span_ordinal, 2);

    const std::string retail_byte(1, static_cast<char>(0xE9));
    const auto document{App::Interface::parse_runtime_text(retail_byte)};
    CHECK_EQ(App::Interface::runtime_text_plain_bytes(document), retail_byte);
    REQUIRE_EQ(events_of_type(document, RuntimeTextEventType::k_text_bytes).size(), 1);
    CHECK_EQ(
        events_of_type(document, RuntimeTextEventType::k_text_bytes).front()->text_bytes.size(), 1);
  }

  TEST_CASE("supports literal mode and explicit authored line breaks") {
    const auto literal{App::Interface::parse_runtime_text(
        "{fD}[abc]", App::Interface::RuntimeTextParseOptions{.raw_mode = true})};
    CHECK_EQ(App::Interface::runtime_text_plain_bytes(literal), "{fD}[abc]");
    CHECK(events_of_type(literal, RuntimeTextEventType::k_set_font).empty());
    CHECK(events_of_type(literal, RuntimeTextEventType::k_selectable_span_begin).empty());

    const auto lines{App::Interface::parse_runtime_text("abc\r\ndef")};
    CHECK_EQ(App::Interface::runtime_text_plain_bytes(lines), "abc\ndef");
    CHECK_EQ(events_of_type(lines, RuntimeTextEventType::k_line_break).size(), 1);
  }

  TEST_CASE("truncated controls degrade safely and deterministically") {
    constexpr std::array<std::string_view, 6> malformed{
        "{", "{f", "{E", "{I255", "{X050", "[unterminated"};
    for (const std::string_view text : malformed) {
      const auto first{App::Interface::parse_runtime_text(std::string{text})};
      const auto second{App::Interface::parse_runtime_text(std::string{text})};
      CHECK_EQ(App::Interface::runtime_text_plain_bytes(first),
          App::Interface::runtime_text_plain_bytes(second));
      CHECK_EQ(first.events().size(), second.events().size());
    }
    CHECK_EQ(App::Interface::runtime_text_plain_bytes(
                 App::Interface::parse_runtime_text("[unterminated")),
        "unterminated");
  }
}