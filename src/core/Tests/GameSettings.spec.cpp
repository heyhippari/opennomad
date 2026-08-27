#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Settings/GameSettings.hpp"

namespace {

std::filesystem::path test_path() {
  return std::filesystem::temp_directory_path() / "opennomad-gamesettings-test.cfg";
}

std::vector<App::Settings::SettingChoice> interpolation_choices() {
  return {App::Settings::SettingChoice{.label = "Off", .raw_value = 0},
      App::Settings::SettingChoice{.label = "On", .raw_value = 1}};
}

}  // namespace

TEST_SUITE("Settings::GameSettings") {
  TEST_CASE("persists lazily registered values and bindings") {
    const std::filesystem::path path{test_path()};
    std::filesystem::remove(path);

    App::Settings::GameSettings original;
    original.ensure_choice("enhancements.menu_interpolation", interpolation_choices(), 1U);
    original.ensure_number("audio.dialogue_volume", 0, 100, 10, 100);
    CHECK(original.adjust_choice("enhancements.menu_interpolation", -1));
    CHECK(original.adjust_number("audio.dialogue_volume", -1));
    CHECK(original.runtime_control_bindings().set_value(
        App::Settings::RuntimeControlDevice::k_keyboard, 0, 2, 0x2CU));
    CHECK(original.save(path).has_value());

    App::Settings::GameSettings loaded;
    CHECK(loaded.load(path).has_value());
    loaded.ensure_choice("enhancements.menu_interpolation", interpolation_choices(), 1U);
    loaded.ensure_number("audio.dialogue_volume", 0, 100, 10, 100);
    CHECK(loaded.choice_raw_value("enhancements.menu_interpolation").value() == 0);
    CHECK(loaded.number_value("audio.dialogue_volume").value() == 90);
    CHECK(loaded.runtime_control_bindings().value(
              App::Settings::RuntimeControlDevice::k_keyboard, 0, 2) == 0x2CU);
    std::filesystem::remove(path);
  }

  TEST_CASE("notifies only when a setting changes") {
    App::Settings::GameSettings settings;
    settings.ensure_choice("test.choice", interpolation_choices(), 0U);
    std::string changed_id;
    settings.set_change_callback(
        [&changed_id](const std::string_view stable_id) { changed_id = stable_id; });
    CHECK_FALSE(settings.adjust_choice("test.choice", -1));
    CHECK(changed_id.empty());
    CHECK(settings.adjust_choice("test.choice", 1));
    CHECK(changed_id == "test.choice");
  }

  TEST_CASE("validates pending values") {
    const std::filesystem::path path{test_path()};
    std::filesystem::remove(path);
    App::Settings::GameSettings source;
    CHECK(source.save(path).has_value());
    {
      std::ofstream output{path};
      output << "version=1\nchoice.test.choice=99\nnumber.test.number=999\n";
    }
    App::Settings::GameSettings loaded;
    CHECK(loaded.load(path).has_value());
    loaded.ensure_choice("test.choice", interpolation_choices(), 1U);
    loaded.ensure_number("test.number", 0, 100, 10, 50);
    CHECK(loaded.choice_raw_value("test.choice").value() == 1);
    CHECK(loaded.number_value("test.number").value() == 100);
    std::filesystem::remove(path);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
