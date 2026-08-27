#include "Settings/GameSettings.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace App::Settings {

ChoiceSetting::ChoiceSetting(
    std::vector<SettingChoice> choices, const std::size_t selected_index)
    : m_choices(std::move(choices)) {
  if (!m_choices.empty()) {
    m_selected_index = std::min(selected_index, m_choices.size() - 1U);
  }
}

bool ChoiceSetting::adjust(const std::int32_t delta) {
  if (m_choices.empty() || delta == 0) {
    return false;
  }

  const std::size_t previous{m_selected_index};
  if (delta < 0) {
    if (m_selected_index > 0U) {
      --m_selected_index;
    }
  } else if (m_selected_index + 1U < m_choices.size()) {
    ++m_selected_index;
  }
  return previous != m_selected_index;
}

std::string_view ChoiceSetting::label() const {
  if (m_choices.empty()) {
    return {};
  }
  return m_choices.at(m_selected_index).label;
}

std::optional<std::int32_t> ChoiceSetting::raw_value() const {
  if (m_choices.empty()) {
    return std::nullopt;
  }
  return m_choices.at(m_selected_index).raw_value;
}

std::size_t ChoiceSetting::selected_index() const {
  return m_selected_index;
}

std::size_t ChoiceSetting::choice_count() const {
  return m_choices.size();
}

NumericSetting::NumericSetting(const std::int32_t minimum,
    const std::int32_t maximum,
    const std::int32_t step,
    const std::int32_t value)
    : m_minimum(std::min(minimum, maximum)),
      m_maximum(std::max(minimum, maximum)),
      m_step(std::max<std::int32_t>(1, step)),
      m_value(std::clamp(value, m_minimum, m_maximum)) {}

bool NumericSetting::adjust(const std::int32_t delta) {
  if (delta == 0) {
    return false;
  }

  const std::int32_t previous{m_value};
  const std::int64_t signed_step{
      delta < 0 ? -static_cast<std::int64_t>(m_step) : static_cast<std::int64_t>(m_step)};
  const std::int64_t candidate{static_cast<std::int64_t>(m_value) + signed_step};
  const std::int64_t bounded{std::clamp(candidate,
      static_cast<std::int64_t>(m_minimum),
      static_cast<std::int64_t>(m_maximum))};
  m_value = static_cast<std::int32_t>(bounded);
  return previous != m_value;
}

std::int32_t NumericSetting::value() const {
  return m_value;
}

float NumericSetting::fraction() const {
  if (m_maximum <= m_minimum) {
    return 0.0F;
  }
  return static_cast<float>(m_value - m_minimum) /
         static_cast<float>(m_maximum - m_minimum);
}

void GameSettings::ensure_choice(std::string stable_id,
    std::vector<SettingChoice> choices,
    const std::size_t selected_index) {
  if (m_choices.contains(stable_id)) {
    return;
  }
  m_choices.emplace(
      std::move(stable_id), ChoiceSetting{std::move(choices), selected_index});
}

void GameSettings::replace_choice(std::string stable_id,
    std::vector<SettingChoice> choices,
    const std::size_t selected_index) {
  m_choices.insert_or_assign(
      std::move(stable_id), ChoiceSetting{std::move(choices), selected_index});
}

void GameSettings::ensure_number(std::string stable_id,
    const std::int32_t minimum,
    const std::int32_t maximum,
    const std::int32_t step,
    const std::int32_t value) {
  if (m_numbers.contains(stable_id)) {
    return;
  }
  m_numbers.emplace(
      std::move(stable_id), NumericSetting{minimum, maximum, step, value});
}

bool GameSettings::adjust_choice(
    const std::string_view stable_id, const std::int32_t delta) {
  ChoiceSetting* setting{find_choice(stable_id)};
  return setting != nullptr && setting->adjust(delta);
}

bool GameSettings::adjust_number(
    const std::string_view stable_id, const std::int32_t delta) {
  NumericSetting* setting{find_number(stable_id)};
  return setting != nullptr && setting->adjust(delta);
}

std::string GameSettings::choice_label(const std::string_view stable_id) const {
  const ChoiceSetting* setting{find_choice(stable_id)};
  return setting == nullptr ? std::string{} : std::string{setting->label()};
}

std::optional<std::int32_t> GameSettings::choice_raw_value(
    const std::string_view stable_id) const {
  const ChoiceSetting* setting{find_choice(stable_id)};
  return setting == nullptr ? std::nullopt : setting->raw_value();
}

std::optional<std::int32_t> GameSettings::number_value(
    const std::string_view stable_id) const {
  const NumericSetting* setting{find_number(stable_id)};
  return setting == nullptr ? std::nullopt
                            : std::optional<std::int32_t>{setting->value()};
}

float GameSettings::number_fraction(const std::string_view stable_id) const {
  const NumericSetting* setting{find_number(stable_id)};
  return setting == nullptr ? 0.0F : setting->fraction();
}

ChoiceSetting* GameSettings::find_choice(const std::string_view stable_id) {
  const auto found{m_choices.find(stable_id)};
  return found == m_choices.end() ? nullptr : &found->second;
}

const ChoiceSetting* GameSettings::find_choice(const std::string_view stable_id) const {
  const auto found{m_choices.find(stable_id)};
  return found == m_choices.end() ? nullptr : &found->second;
}

NumericSetting* GameSettings::find_number(const std::string_view stable_id) {
  const auto found{m_numbers.find(stable_id)};
  return found == m_numbers.end() ? nullptr : &found->second;
}

const NumericSetting* GameSettings::find_number(const std::string_view stable_id) const {
  const auto found{m_numbers.find(stable_id)};
  return found == m_numbers.end() ? nullptr : &found->second;
}

}  // namespace App::Settings