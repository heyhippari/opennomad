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

bool GameSettings::adjust_choice(
    const std::string_view stable_id, const std::int32_t delta) {
  ChoiceSetting* setting{find_choice(stable_id)};
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

ChoiceSetting* GameSettings::find_choice(const std::string_view stable_id) {
  const auto found{m_choices.find(stable_id)};
  return found == m_choices.end() ? nullptr : &found->second;
}

const ChoiceSetting* GameSettings::find_choice(const std::string_view stable_id) const {
  const auto found{m_choices.find(stable_id)};
  return found == m_choices.end() ? nullptr : &found->second;
}

}  // namespace App::Settings