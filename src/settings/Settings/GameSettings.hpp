#pragma once

#include <cstddef>
#include <cstdint>
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

  [[nodiscard]] std::string_view label() const;
  [[nodiscard]] std::optional<std::int32_t> raw_value() const;
  [[nodiscard]] std::size_t selected_index() const;
  [[nodiscard]] std::size_t choice_count() const;

 private:
  std::vector<SettingChoice> m_choices;
  std::size_t m_selected_index{0};
};

/// Process-lifetime OpenNomad settings registry.
///
/// Phase 2 needs discrete values for the Runtime Video page. Sliders and
/// persistence can be added alongside ChoiceSetting later without changing the
/// I2D row model or making interface elements own game configuration.
class GameSettings {
 public:
  /// Defines a setting only when the stable ID is not already present.
  /// Reopening OPTIONS therefore preserves changes made earlier in the run.
  void ensure_choice(std::string stable_id,
      std::vector<SettingChoice> choices,
      std::size_t selected_index);

  /// Replaces a dynamic choice list. Used for values derived from the current
  /// runtime environment (resolution / active renderer in Phase 2).
  void replace_choice(std::string stable_id,
      std::vector<SettingChoice> choices,
      std::size_t selected_index);

  bool adjust_choice(std::string_view stable_id, std::int32_t delta);

  [[nodiscard]] std::string choice_label(std::string_view stable_id) const;
  [[nodiscard]] std::optional<std::int32_t> choice_raw_value(
      std::string_view stable_id) const;

  [[nodiscard]] ChoiceSetting* find_choice(std::string_view stable_id);
  [[nodiscard]] const ChoiceSetting* find_choice(std::string_view stable_id) const;

 private:
  // Transparent comparator permits find(string_view) without temporary keys.
  std::map<std::string, ChoiceSetting, std::less<>> m_choices;
};

}  // namespace App::Settings