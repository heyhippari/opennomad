#include "Core/Interface/DialogTextLayout.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Dialog/DialogRuntime.hpp"

namespace App::Interface {

float DialogTextLayout::max_scroll() const {
  return std::max(0.0F, formatted_height - viewport_height);
}

namespace {

void append_wrapped_segment(std::string_view segment,
    const float maximum_width,
    const TextMeasure& measure,
    std::vector<DialogTextLine>& lines) {
  std::string line;
  std::size_t cursor{0};
  while (cursor < segment.size()) {
    while (cursor < segment.size() && segment.at(cursor) == ' ') {
      ++cursor;
    }
    const std::size_t word_begin{cursor};
    while (cursor < segment.size() && segment.at(cursor) != ' ') {
      ++cursor;
    }
    if (word_begin == cursor) {
      break;
    }
    const std::string_view word{segment.substr(word_begin, cursor - word_begin)};
    const std::string candidate{line.empty() ? std::string{word} : line + " " + std::string{word}};
    if (line.empty() || measure(candidate) <= maximum_width) {
      line = candidate;
      continue;
    }
    lines.push_back(DialogTextLine{.text = line, .width = measure(line)});
    line.clear();

    std::string fragment;
    for (const char byte : word) {
      const std::string next{fragment + byte};
      if (!fragment.empty() && measure(next) > maximum_width) {
        lines.push_back(DialogTextLine{.text = fragment, .width = measure(fragment)});
        fragment.clear();
      }
      fragment.push_back(byte);
    }
    line = std::move(fragment);
  }
  if (!line.empty() || segment.empty()) {
    lines.push_back(DialogTextLine{.text = line, .width = measure(line)});
  }
}

}  // namespace

DialogTextLayout format_dialog_text(const std::string_view text,
    const float maximum_width,
    const float viewport_height,
    const float line_height,
    const TextMeasure& measure) {
  DialogTextLayout result;
  result.viewport_height = viewport_height;
  std::size_t segment_begin{0};
  while (segment_begin <= text.size()) {
    const std::size_t newline{text.find('\n', segment_begin)};
    const std::size_t segment_end{
        newline == std::string_view::npos ? text.size() : newline};
    append_wrapped_segment(
        text.substr(segment_begin, segment_end - segment_begin), maximum_width, measure, result.lines);
    if (newline == std::string_view::npos) {
      break;
    }
    segment_begin = newline + 1U;
  }
  result.formatted_height = static_cast<float>(result.lines.size()) * line_height;
  return result;
}

DialogResponseBlockLayout layout_dialog_responses(
    const std::span<const float> response_heights) {
  DialogResponseBlockLayout result;
  for (const float height : response_heights) {
    result.total_height += height;
  }
  result.top = result.bottom - result.total_height;
  float cursor{result.top};
  result.response_tops.reserve(response_heights.size());
  for (const float height : response_heights) {
    result.response_tops.push_back(cursor);
    cursor += height;
  }
  return result;
}

std::array<float, 4> dialog_main_tint(const Dialog::DialogState state) {
  return state == Dialog::DialogState::k_presenting_automatic_player_line
      ? k_dialog_automatic_tint
      : k_dialog_white;
}

std::array<float, 4> dialog_response_tint(const bool selected) {
  return selected ? k_dialog_white : k_dialog_unselected_tint;
}

bool DialogScrollState::observe_generation(const std::uint64_t generation) {
  if (m_observed && m_generation == generation) {
    return false;
  }
  m_generation = generation;
  m_offset = 0.0F;
  m_maximum = 0.0F;
  m_observed = true;
  return true;
}

void DialogScrollState::reset() {
  m_generation = 0;
  m_offset = 0.0F;
  m_maximum = 0.0F;
  m_observed = false;
}

void DialogScrollState::update(
    const float delta_seconds, const bool up_held, const bool down_held) {
  const float direction{static_cast<float>(down_held) - static_cast<float>(up_held)};
  m_offset = std::clamp(m_offset + (direction * k_dialog_scroll_speed *
                                       std::max(0.0F, delta_seconds)),
      0.0F,
      m_maximum);
}

void DialogScrollState::set_maximum(const float maximum) {
  m_maximum = std::max(0.0F, maximum);
  m_offset = std::clamp(m_offset, 0.0F, m_maximum);
}

float DialogScrollState::clamped(const float maximum) const {
  return std::clamp(m_offset, 0.0F, std::max(0.0F, maximum));
}

}  // namespace App::Interface
