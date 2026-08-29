#include "Settings/GameSettings.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace App::Settings {

namespace {

// Exact retail defaults copied by Runtime_InitializeDefaults @ 0x0041F4C0.
constexpr RuntimeControlBindings::Table K_RUNTIME_KEYBOARD_DEFAULTS{{
    {{0xCBU,
        0xCDU,
        0xC8U,
        0xD0U,
        0x1CU,
        0x39U,
        0x00U,
        0x26U,
        0x00U,
        0x00U,
        0x9DU,
        0x36U,
        0x00U,
        0x0FU}},
    {{0xCBU,
        0xCDU,
        0xC8U,
        0xD0U,
        0x1CU,
        0x9DU,
        0x00U,
        0x26U,
        0x00U,
        0x00U,
        0x00U,
        0x36U,
        0x00U,
        0x00U}},
    {{0x4BU,
        0x4DU,
        0xC8U,
        0xD0U,
        0x36U,
        0x39U,
        0x9DU,
        0x00U,
        0x1CU,
        0x48U,
        0xCBU,
        0xCDU,
        0x50U,
        0x38U}},
    {{0xCBU,
        0xCDU,
        0xC8U,
        0xD0U,
        0x10U,
        0x11U,
        0x1EU,
        0x1FU,
        0x00U,
        0x00U,
        0xD3U,
        0xCFU,
        0x00U,
        0x00U}},
}};

constexpr RuntimeControlBindings::Table K_RUNTIME_MOUSE_DEFAULTS{{
    {{0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x0CU,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U}},
    {{0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x0CU,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U}},
    {{0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x0CU,
        0x0DU,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U}},
    {{0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U}},
}};

constexpr RuntimeControlBindings::Table K_RUNTIME_JOYSTICK_DEFAULTS{{
    {{0x00U,
        0x00U,
        0x04U,
        0x04U,
        0x30U,
        0x31U,
        0x32U,
        0x33U,
        0x34U,
        0x35U,
        0x36U,
        0x37U,
        0x38U,
        0x39U}},
    {{0x00U,
        0x00U,
        0x04U,
        0x04U,
        0x30U,
        0x31U,
        0x32U,
        0x33U,
        0x34U,
        0x35U,
        0x36U,
        0x37U,
        0x38U,
        0x39U}},
    {{0x00U,
        0x00U,
        0x04U,
        0x04U,
        0x30U,
        0x31U,
        0x32U,
        0x33U,
        0x34U,
        0x35U,
        0x36U,
        0x37U,
        0x38U,
        0x39U}},
    {{0x00U,
        0x00U,
        0x04U,
        0x04U,
        0x30U,
        0x31U,
        0x32U,
        0x33U,
        0x34U,
        0x35U,
        0x36U,
        0x37U,
        0x38U,
        0x39U}},
}};

}  // namespace

ChoiceSetting::ChoiceSetting(std::vector<SettingChoice> choices, const std::size_t selected_index)
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

bool ChoiceSetting::set_raw_value(const std::int32_t raw_value) {
  for (std::size_t index{0}; index < m_choices.size(); ++index) {
    if (m_choices.at(index).raw_value == raw_value) {
      const bool changed{m_selected_index != index};
      m_selected_index = index;
      return changed;
    }
  }
  return false;
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
  const std::int64_t bounded{std::clamp(
      candidate, static_cast<std::int64_t>(m_minimum), static_cast<std::int64_t>(m_maximum))};
  m_value = static_cast<std::int32_t>(bounded);
  return previous != m_value;
}

bool NumericSetting::set_value(const std::int32_t value) {
  const std::int32_t previous{m_value};
  m_value = std::clamp(value, m_minimum, m_maximum);
  return previous != m_value;
}

std::int32_t NumericSetting::value() const {
  return m_value;
}

float NumericSetting::fraction() const {
  if (m_maximum <= m_minimum) {
    return 0.0F;
  }
  return static_cast<float>(m_value - m_minimum) / static_cast<float>(m_maximum - m_minimum);
}

RuntimeControlBindings::RuntimeControlBindings()
    : m_keyboard(K_RUNTIME_KEYBOARD_DEFAULTS),
      m_mouse(K_RUNTIME_MOUSE_DEFAULTS),
      m_joystick(K_RUNTIME_JOYSTICK_DEFAULTS) {}

std::uint32_t RuntimeControlBindings::value(
    const RuntimeControlDevice device, const std::size_t group, const std::size_t slot) const {
  if (group >= k_group_count || slot >= k_slots_per_group) {
    return 0U;
  }

  switch (device) {
    case RuntimeControlDevice::k_keyboard:
      return m_keyboard.at(group).at(slot);
    case RuntimeControlDevice::k_mouse:
      return m_mouse.at(group).at(slot);
    case RuntimeControlDevice::k_joystick:
      return m_joystick.at(group).at(slot);
  }
  return 0U;
}

bool RuntimeControlBindings::set_value(const RuntimeControlDevice device,
    const std::size_t group,
    const std::size_t slot,
    const std::uint32_t value) {
  if (group >= k_group_count || slot >= k_slots_per_group) {
    return false;
  }

  switch (device) {
    case RuntimeControlDevice::k_keyboard:
      m_keyboard.at(group).at(slot) = value;
      return true;
    case RuntimeControlDevice::k_mouse:
      m_mouse.at(group).at(slot) = value;
      return true;
    case RuntimeControlDevice::k_joystick:
      m_joystick.at(group).at(slot) = value;
      return true;
  }
  return false;
}

void RuntimeControlBindings::restore_keyboard_mouse_defaults() {
  m_keyboard = K_RUNTIME_KEYBOARD_DEFAULTS;
  m_mouse = K_RUNTIME_MOUSE_DEFAULTS;
}

void RuntimeControlBindings::restore_joystick_defaults() {
  m_joystick = K_RUNTIME_JOYSTICK_DEFAULTS;
}

void GameSettings::ensure_choice(
    std::string stable_id, std::vector<SettingChoice> choices, const std::size_t selected_index) {
  if (m_choices.contains(stable_id)) {
    return;
  }
  ChoiceSetting setting{std::move(choices), selected_index};
  const auto pending{m_pending_choices.find(stable_id)};
  if (pending != m_pending_choices.end()) {
    setting.set_raw_value(pending->second);
    m_pending_choices.erase(pending);
  }
  m_choices.emplace(std::move(stable_id), std::move(setting));
}

void GameSettings::replace_choice(
    std::string stable_id, std::vector<SettingChoice> choices, const std::size_t selected_index) {
  ChoiceSetting replacement{std::move(choices), selected_index};
  std::optional<std::int32_t> previous;
  if (const ChoiceSetting* existing{find_choice(stable_id)}; existing != nullptr) {
    previous = existing->raw_value();
  } else if (const auto pending{m_pending_choices.find(stable_id)};
      pending != m_pending_choices.end()) {
    previous = pending->second;
  }
  if (previous.has_value()) {
    replacement.set_raw_value(previous.value());
    m_pending_choices.erase(stable_id);
  }
  m_choices.insert_or_assign(std::move(stable_id), std::move(replacement));
}

void GameSettings::ensure_number(std::string stable_id,
    const std::int32_t minimum,
    const std::int32_t maximum,
    const std::int32_t step,
    const std::int32_t value) {
  if (m_numbers.contains(stable_id)) {
    return;
  }
  NumericSetting setting{minimum, maximum, step, value};
  if (const auto pending{m_pending_numbers.find(stable_id)}; pending != m_pending_numbers.end()) {
    setting.set_value(pending->second);
    m_pending_numbers.erase(pending);
  }
  m_numbers.emplace(std::move(stable_id), setting);
}

bool GameSettings::adjust_choice(const std::string_view stable_id, const std::int32_t delta) {
  ChoiceSetting* setting{find_choice(stable_id)};
  const bool changed{setting != nullptr && setting->adjust(delta)};
  if (changed) {
    notify_change(stable_id);
  }
  return changed;
}

bool GameSettings::adjust_number(const std::string_view stable_id, const std::int32_t delta) {
  NumericSetting* setting{find_number(stable_id)};
  const bool changed{setting != nullptr && setting->adjust(delta)};
  if (changed) {
    notify_change(stable_id);
  }
  return changed;
}

bool GameSettings::set_choice_raw_value(
    const std::string_view stable_id, const std::int32_t raw_value, const bool notify) {
  ChoiceSetting* setting{find_choice(stable_id)};
  if (setting == nullptr || !setting->set_raw_value(raw_value)) {
    return false;
  }
  if (notify) {
    notify_change(stable_id);
  }
  return true;
}

bool GameSettings::set_runtime_control_binding(const RuntimeControlDevice device,
    const std::size_t group,
    const std::size_t slot,
    const std::uint32_t value) {
  if (!m_runtime_control_bindings.set_value(device, group, slot, value)) {
    return false;
  }
  notify_change("runtime_control_bindings");
  return true;
}

void GameSettings::restore_runtime_keyboard_mouse_defaults() {
  m_runtime_control_bindings.restore_keyboard_mouse_defaults();
  notify_change("runtime_control_bindings");
}

void GameSettings::restore_runtime_joystick_defaults() {
  m_runtime_control_bindings.restore_joystick_defaults();
  notify_change("runtime_control_bindings");
}

GraphicsPreferences GameSettings::graphics_preferences() const {
  GraphicsPreferences preferences{};
  if (const auto raw{choice_raw_value("video.clipping_distance")}; raw.has_value()) {
    const std::int32_t value{std::clamp(raw.value(), 25, 200)};
    switch (value) {
      case 25:
        preferences.clip_distance_metres = 25.0F;
        break;
      case 50:
        preferences.clip_distance_metres = 50.0F;
        break;
      case 100:
        preferences.clip_distance_metres = 100.0F;
        break;
      case 150:
        preferences.clip_distance_metres = 150.0F;
        break;
      case 200:
        preferences.clip_distance_metres = 200.0F;
        break;
      default:
        preferences.clip_distance_metres = 50.0F;
        break;
    }
  }
  if (const auto raw{choice_raw_value("video.display_sky")}; raw.has_value()) {
    preferences.display_sky = raw.value() == 1;
  }
  if (const auto raw{choice_raw_value("video.display_shadow")}; raw.has_value()) {
    preferences.display_shadows = raw.value() == 1;
  }
  if (const auto raw{choice_raw_value("video.street_activity")}; raw.has_value()) {
    const std::int32_t value{std::clamp(raw.value(), 0, 4)};
    preferences.street_activity = static_cast<std::uint8_t>(value);
  }
  if (const auto raw{choice_raw_value("video.detail_level")}; raw.has_value()) {
    const std::int32_t value{std::clamp(raw.value(), 0, 2)};
    preferences.detail_level = static_cast<std::uint8_t>(value);
  }
  return preferences;
}

GameplayPreferences GameSettings::gameplay_preferences() const {
  GameplayPreferences preferences{};
  if (const auto raw{choice_raw_value("game.fight_difficulty")}; raw.has_value()) {
    const std::int32_t value{std::clamp(raw.value(), 0, 2)};
    preferences.melee_difficulty = static_cast<GameplayDifficulty>(value);
  }
  if (const auto raw{choice_raw_value("game.shoot_difficulty")}; raw.has_value()) {
    const std::int32_t value{std::clamp(raw.value(), 0, 2)};
    preferences.shooting_difficulty = static_cast<GameplayDifficulty>(value);
  }
  if (const auto raw{choice_raw_value("game.fight_camera")}; raw.has_value()) {
    preferences.combat_camera = raw.value() == 1;
  }
  return preferences;
}

std::expected<void, std::string> GameSettings::load(const std::filesystem::path& path) {
  std::ifstream input{path};
  if (!input.is_open()) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) && !error) {
      return {};
    }
    return std::unexpected{std::string{"unable to open settings file"}};
  }

  std::string line;
  if (!std::getline(input, line) || line != "version=1") {
    return std::unexpected{std::string{"unsupported or malformed settings version"}};
  }

  auto parse_value = [](const std::string_view text) -> std::optional<std::int32_t> {
    std::int32_t value{0};
    const auto result{std::from_chars(text.data(), text.data() + text.size(), value)};
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
      return std::nullopt;
    }
    return value;
  };
  const auto canonical_choice_id = [](const std::string_view stable_id) {
    if (stable_id == "enhancements.menu_interpolation") {
      return std::string_view{"enhancements.animation_interpolation"};
    }
    if (stable_id == "audio.3d_sound") {
      return std::string_view{"audio.spatial_audio"};
    }
    return stable_id;
  };
  std::map<std::string, std::int32_t, std::less<>> loaded_choices;
  std::map<std::string, std::int32_t, std::less<>> legacy_choices;
  while (std::getline(input, line)) {
    const std::size_t equals{line.find('=')};
    if (equals == std::string::npos) {
      continue;
    }
    const std::string_view key{line.data(), equals};
    const std::string_view value{line.data() + equals + 1, line.size() - equals - 1};
    const std::optional<std::int32_t> parsed{parse_value(value)};
    if (key.starts_with("choice.") && parsed.has_value()) {
      const std::string_view persisted_id{key.substr(7)};
      const std::string canonical_id{canonical_choice_id(persisted_id)};
      if (canonical_id == persisted_id) {
        loaded_choices.insert_or_assign(canonical_id, parsed.value());
      } else {
        legacy_choices.insert_or_assign(canonical_id, parsed.value());
      }
    } else if (key.starts_with("number.") && parsed.has_value()) {
      const std::string stable_id{key.substr(7)};
      if (NumericSetting* setting{find_number(stable_id)}; setting != nullptr) {
        if (setting->set_value(parsed.value())) {
          notify_change(stable_id);
        }
      } else {
        m_pending_numbers.insert_or_assign(stable_id, parsed.value());
      }
    } else if (key.starts_with("runtime_control.")) {
      const std::string_view prefix{key.substr(16)};
      const bool keyboard{prefix.starts_with("keyboard.")};
      const bool mouse{prefix.starts_with("mouse.")};
      const bool joystick{prefix.starts_with("joystick.")};
      const std::string_view indices{
          prefix.substr(keyboard || mouse || joystick ? 9U : prefix.size())};
      const std::size_t dot{indices.find('.')};
      if (parsed.has_value() && (keyboard || mouse || joystick) && dot != std::string_view::npos) {
        const auto group{parse_value(indices.substr(0, dot))};
        const auto slot{parse_value(indices.substr(dot + 1))};
        if (group.has_value() && slot.has_value()) {
          RuntimeControlDevice device{RuntimeControlDevice::k_joystick};
          if (keyboard) {
            device = RuntimeControlDevice::k_keyboard;
          } else if (mouse) {
            device = RuntimeControlDevice::k_mouse;
          }
          m_runtime_control_bindings.set_value(device,
              static_cast<std::size_t>(group.value()),
              static_cast<std::size_t>(slot.value()),
              static_cast<std::uint32_t>(parsed.value()));
        }
      }
    }
  }
  for (const auto& [stable_id, value] : legacy_choices) {
    loaded_choices.try_emplace(stable_id, value);
  }
  for (const auto& [stable_id, value] : loaded_choices) {
    if (ChoiceSetting* setting{find_choice(stable_id)}; setting != nullptr) {
      if (setting->set_raw_value(value)) {
        notify_change(stable_id);
      }
    } else {
      m_pending_choices.insert_or_assign(stable_id, value);
    }
  }
  return {};
}

std::expected<void, std::string> GameSettings::save(const std::filesystem::path& path) const {
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      return std::unexpected{std::string{"unable to create settings directory"}};
    }
  }
  std::ofstream output{path};
  if (!output.is_open()) {
    return std::unexpected{std::string{"unable to open settings file for writing"}};
  }
  output << "version=1\n";
  for (const auto& [stable_id, setting] : m_choices) {
    if (const auto raw{setting.raw_value()}; raw.has_value()) {
      output << "choice." << stable_id << '=' << raw.value() << '\n';
    }
  }
  for (const auto& [stable_id, setting] : m_numbers) {
    output << "number." << stable_id << '=' << setting.value() << '\n';
  }
  for (const RuntimeControlDevice device : {RuntimeControlDevice::k_keyboard,
           RuntimeControlDevice::k_mouse,
           RuntimeControlDevice::k_joystick}) {
    std::string_view device_name;
    switch (device) {
      case RuntimeControlDevice::k_keyboard:
        device_name = "keyboard";
        break;
      case RuntimeControlDevice::k_mouse:
        device_name = "mouse";
        break;
      case RuntimeControlDevice::k_joystick:
        device_name = "joystick";
        break;
    }
    for (std::size_t group{0}; group < RuntimeControlBindings::k_group_count; ++group) {
      for (std::size_t slot{0}; slot < RuntimeControlBindings::k_slots_per_group; ++slot) {
        output << "runtime_control." << device_name << '.' << group << '.' << slot << '='
               << m_runtime_control_bindings.value(device, group, slot) << '\n';
      }
    }
  }
  if (!output) {
    return std::unexpected{std::string{"unable to write settings file"}};
  }
  return {};
}

GameSettings::ListenerId GameSettings::add_change_listener(GameSettingChangeCallback callback) {
  const ListenerId listener_id{m_next_listener_id++};
  m_change_listeners.emplace(listener_id, std::move(callback));
  return listener_id;
}

void GameSettings::remove_change_listener(const ListenerId listener_id) {
  m_change_listeners.erase(listener_id);
}

void GameSettings::notify_change(const std::string_view stable_id) {
  for (const auto& [listener_id, callback] : m_change_listeners) {
    static_cast<void>(listener_id);
    callback(stable_id);
  }
}

std::string GameSettings::choice_label(const std::string_view stable_id) const {
  const ChoiceSetting* setting{find_choice(stable_id)};
  return setting == nullptr ? std::string{} : std::string{setting->label()};
}

std::optional<std::int32_t> GameSettings::choice_raw_value(const std::string_view stable_id) const {
  const ChoiceSetting* setting{find_choice(stable_id)};
  return setting == nullptr ? std::nullopt : setting->raw_value();
}

std::optional<std::int32_t> GameSettings::loaded_choice_raw_value(
    const std::string_view stable_id) const {
  if (const auto value{choice_raw_value(stable_id)}; value.has_value()) {
    return value;
  }
  const auto pending{m_pending_choices.find(stable_id)};
  return pending == m_pending_choices.end() ? std::nullopt
                                            : std::optional<std::int32_t>{pending->second};
}

std::optional<std::int32_t> GameSettings::number_value(const std::string_view stable_id) const {
  const NumericSetting* setting{find_number(stable_id)};
  return setting == nullptr ? std::nullopt : std::optional<std::int32_t>{setting->value()};
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