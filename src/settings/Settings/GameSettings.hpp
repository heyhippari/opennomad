#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace App::Settings {

/// One selectable value exposed by an OpenNomad setting.
///
/// `raw_value` is deliberately kept separate from the display label. Runtime's
/// OPTIONS descriptors use the same distinction (for example clipping-distance
/// labels map to raw distances 25/50/100/150/200). Modern settings can use the
/// same backend without pretending to have Runtime provenance.
struct SettingChoice {
  std::string label;
  std::int32_t raw_value{0};
};

/// Backend state for one discrete/enum setting.
///
/// The UI never owns the selected index: interface rows only read/adjust this
/// object. That keeps settings alive when interface 35 is closed and gives
/// future configuration persistence and gameplay consumers one stable owner.
class ChoiceSetting {
 public:
  ChoiceSetting(std::vector<SettingChoice> choices, std::size_t selected_index);

  /// Moves one choice in the requested direction, clamped at the endpoints.
  /// Returns true only when the selected value changed.
  bool adjust(std::int32_t delta);
  bool set_raw_value(std::int32_t raw_value);

  [[nodiscard]] std::string_view label() const;
  [[nodiscard]] std::optional<std::int32_t> raw_value() const;
  [[nodiscard]] std::size_t selected_index() const;
  [[nodiscard]] std::size_t choice_count() const;

 private:
  std::vector<SettingChoice> m_choices;
  std::size_t m_selected_index{0};
};

/// Backend state for one bounded numeric setting.
///
/// Runtime type-1 OPTIONS rows use this for the three 0..100 volume sliders.
/// Keeping it generic makes the same storage useful for future OpenNomad
/// numeric options without coupling Settings to the I2D renderer.
class NumericSetting {
 public:
  NumericSetting(std::int32_t minimum, std::int32_t maximum, std::int32_t step, std::int32_t value);

  /// Moves one step in the sign of `delta`, clamped to [minimum, maximum].
  /// Returns true only when the value changed.
  bool adjust(std::int32_t delta);
  bool set_value(std::int32_t value);

  [[nodiscard]] std::int32_t value() const;
  [[nodiscard]] float fraction() const;

 private:
  std::int32_t m_minimum{0};
  std::int32_t m_maximum{100};
  std::int32_t m_step{10};
  std::int32_t m_value{100};
};

/// Physical-device column in Runtime's persistent 4x14 control tables.
///
/// Runtime copies three 0xE0-byte tables into OMK_SAVE at +0x034, +0x114 and
/// +0x1F4. Type-3 OPTIONS descriptors address them by group and slot. Keeping
/// the raw values here preserves retail import/export provenance without
/// forcing the current OpenNomad action vocabulary to impersonate the four
/// still-partially-understood Runtime control groups.
enum class RuntimeControlDevice : std::uint8_t {
  k_keyboard,
  k_mouse,
  k_joystick,
};

class RuntimeControlBindings {
 public:
  static constexpr std::size_t k_group_count{4};
  static constexpr std::size_t k_slots_per_group{14};
  using Table = std::array<std::array<std::uint32_t, k_slots_per_group>, k_group_count>;

  RuntimeControlBindings();

  [[nodiscard]] std::uint32_t value(
      RuntimeControlDevice device, std::size_t group, std::size_t slot) const;
  bool set_value(
      RuntimeControlDevice device, std::size_t group, std::size_t slot, std::uint32_t value);

  /// Runtime type-5 "Restore default settings" behavior in device mode 1:
  /// restore the complete keyboard and mouse tables, not merely the visible
  /// group (0x00493258).
  void restore_keyboard_mouse_defaults();

  /// Runtime type-5 behavior in device mode 2: restore the complete joystick
  /// table (0x004931AB).
  void restore_joystick_defaults();

 private:
  Table m_keyboard{};
  Table m_mouse{};
  Table m_joystick{};
};

/// Process-lifetime OpenNomad settings registry.
///
/// Choice and numeric values are owned here rather than by interface elements.
/// This lets OPTIONS close/reopen without losing edits and leaves one stable
/// backend for later persistence and engine consumers.
class GameSettings {
 public:
  using GameSettingChangeCallback = std::function<void(std::string_view stable_id)>;

  /// Defines a setting only when the stable ID is not already present.
  /// Reopening OPTIONS therefore preserves changes made earlier in the run.
  void ensure_choice(
      std::string stable_id, std::vector<SettingChoice> choices, std::size_t selected_index);

  /// Replaces a dynamic choice list. Used for values derived from the current
  /// runtime environment (resolution / active renderer in Phase 2).
  void replace_choice(
      std::string stable_id, std::vector<SettingChoice> choices, std::size_t selected_index);

  /// Defines a bounded numeric setting only when the stable ID is new.
  void ensure_number(std::string stable_id,
      std::int32_t minimum,
      std::int32_t maximum,
      std::int32_t step,
      std::int32_t value);

  bool adjust_choice(std::string_view stable_id, std::int32_t delta);
  bool adjust_number(std::string_view stable_id, std::int32_t delta);

  [[nodiscard]] std::expected<void, std::string> load(const std::filesystem::path& path);
  [[nodiscard]] std::expected<void, std::string> save(const std::filesystem::path& path) const;
  void set_change_callback(GameSettingChangeCallback callback);

  [[nodiscard]] std::string choice_label(std::string_view stable_id) const;
  [[nodiscard]] std::optional<std::int32_t> choice_raw_value(std::string_view stable_id) const;
  [[nodiscard]] std::optional<std::int32_t> number_value(std::string_view stable_id) const;
  [[nodiscard]] float number_fraction(std::string_view stable_id) const;

  [[nodiscard]] ChoiceSetting* find_choice(std::string_view stable_id);
  [[nodiscard]] const ChoiceSetting* find_choice(std::string_view stable_id) const;
  [[nodiscard]] NumericSetting* find_number(std::string_view stable_id);
  [[nodiscard]] const NumericSetting* find_number(std::string_view stable_id) const;

  [[nodiscard]] RuntimeControlBindings& runtime_control_bindings() {
    return m_runtime_control_bindings;
  }
  [[nodiscard]] const RuntimeControlBindings& runtime_control_bindings() const {
    return m_runtime_control_bindings;
  }

 private:
  void notify_change(std::string_view stable_id);

  // Transparent comparator permits find(string_view) without temporary keys.
  std::map<std::string, ChoiceSetting, std::less<>> m_choices;
  std::map<std::string, NumericSetting, std::less<>> m_numbers;
  std::map<std::string, std::int32_t, std::less<>> m_pending_choices;
  std::map<std::string, std::int32_t, std::less<>> m_pending_numbers;
  RuntimeControlBindings m_runtime_control_bindings;
  GameSettingChangeCallback m_change_callback;
};

}  // namespace App::Settings