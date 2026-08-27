#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Core/Interface/I2DBumpBackground.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/I2DStateTransition.hpp"
#include "Core/Interface/InterfaceManager.hpp"
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
    settings.set_change_callback([&changed_id](const std::string_view stable_id) {
      changed_id = stable_id;
    });
    CHECK_FALSE(settings.adjust_choice("test.choice", -1));
    CHECK(changed_id.empty());
    CHECK(settings.adjust_choice("test.choice", 1));
    CHECK(changed_id == "test.choice");
  }

  TEST_CASE("menu transition style defaults to modern and persists") {
    App::Settings::GameSettings settings;
    settings.ensure_choice("enhancements.menu_transition_style",
        {App::Settings::SettingChoice{.label = "Modern", .raw_value = 0},
            App::Settings::SettingChoice{.label = "Classic", .raw_value = 1},
            App::Settings::SettingChoice{.label = "Reduced Motion", .raw_value = 2}},
        0U);
    CHECK(settings.choice_raw_value("enhancements.menu_transition_style").value() == 0);
    CHECK(settings.adjust_choice("enhancements.menu_transition_style", 1));
    CHECK(settings.choice_raw_value("enhancements.menu_transition_style").value() == 1);
  }

  TEST_CASE("state graph derives semantic transition directions") {
    App::Interface::I2DState root{};
    App::Interface::I2DState child{};
    App::Interface::I2DState grandchild{};
    root.parent = nullptr;
    child.parent = &root;
    grandchild.parent = &child;

    CHECK(App::Interface::determine_transition_direction(&root, &child) ==
          App::Interface::I2DStateTransitionDirection::k_forward);
    CHECK(App::Interface::determine_transition_direction(&child, &grandchild) ==
          App::Interface::I2DStateTransitionDirection::k_forward);
    CHECK(App::Interface::determine_transition_direction(&child, &root) ==
          App::Interface::I2DStateTransitionDirection::k_back);
    CHECK(App::Interface::determine_transition_direction(&grandchild, &child) ==
          App::Interface::I2DStateTransitionDirection::k_back);

    App::Interface::I2DState sibling{};
    CHECK(App::Interface::determine_transition_direction(&root, &sibling) ==
          App::Interface::I2DStateTransitionDirection::k_replace);
    CHECK(App::Interface::determine_transition_direction(&root, &grandchild) ==
          App::Interface::I2DStateTransitionDirection::k_replace);
    CHECK(App::Interface::determine_transition_direction(&grandchild, &root) ==
          App::Interface::I2DStateTransitionDirection::k_replace);
  }

  TEST_CASE("menu selection advances exactly one entry") {
    using App::Interface::next_selection;
    using App::Interface::previous_selection;

    CHECK(next_selection(0U, 4U) == 1U);
    CHECK(next_selection(1U, 4U) == 2U);
    CHECK(next_selection(2U, 4U) == 3U);
    CHECK(next_selection(3U, 4U) == 0U);
    CHECK(previous_selection(0U, 4U) == 3U);

    CHECK(next_selection(0U, 3U) == 1U);
    CHECK(next_selection(1U, 3U) == 2U);
    CHECK(previous_selection(1U, 3U) == 0U);
  }

  TEST_CASE("transition destinations commit before completion callback") {
    using namespace App::Interface;
    InterfaceInstance host;
    InterfaceInstance child;
    host.states.push_back(std::make_unique<I2DState>());
    host.states.push_back(std::make_unique<I2DState>());
    child.states.push_back(std::make_unique<I2DState>());
    host.current_state = host.states.at(0U).get();
    child.current_state = nullptr;
    I2DState* options_shell{host.states.at(1U).get()};
    I2DState* child_root{child.states.at(0U).get()};
    const std::array destinations{
        TransitionStateDestination{.instance = &host, .state = options_shell},
        TransitionStateDestination{.instance = &child, .state = child_root}};
    bool callback_ran{false};

    commit_transition_destinations(destinations, [&]() {
      CHECK(host.current_state == options_shell);
      CHECK(child.current_state == child_root);
      callback_ran = true;
    });

    CHECK(callback_ran);

    I2DState* host_root{host.states.at(0U).get()};
    const std::array back_destination{
        TransitionStateDestination{.instance = &host, .state = host_root}};
    bool child_torn_down{false};
    commit_transition_destinations(back_destination, [&]() {
      CHECK(host.current_state == host_root);
      child_torn_down = true;
    });
    CHECK(child_torn_down);

    I2DState foreign_state;
    const std::array invalid_destination{
        TransitionStateDestination{.instance = &host, .state = &foreign_state}};
    commit_transition_destinations(invalid_destination);
    CHECK(host.current_state == host_root);
  }

  TEST_CASE("menu transition policy samples exact presentation motion") {
    using namespace App::Interface;
    const auto modern_start{sample_transition(I2DMenuTransitionStyle::k_modern,
        I2DStateTransitionDirection::k_forward,
        I2DTransitionContext::k_start_menu,
        0.0F)};
    CHECK(modern_start.outgoing.offset_x == doctest::Approx(0.0F));
    CHECK(modern_start.incoming.offset_x == doctest::Approx(24.0F));
    CHECK(modern_start.outgoing.alpha == doctest::Approx(1.0F));
    CHECK(modern_start.incoming.alpha == doctest::Approx(0.0F));

    const auto modern_end{sample_transition(I2DMenuTransitionStyle::k_modern,
        I2DStateTransitionDirection::k_forward,
        I2DTransitionContext::k_start_menu,
        1.0F)};
    CHECK(modern_end.outgoing.offset_x == doctest::Approx(-24.0F));
    CHECK(modern_end.incoming.offset_x == doctest::Approx(0.0F));
    CHECK(transition_duration(I2DMenuTransitionStyle::k_modern, I2DTransitionContext::k_options) ==
          doctest::Approx(0.20F));

    const auto classic_mid{sample_transition(I2DMenuTransitionStyle::k_classic,
        I2DStateTransitionDirection::k_forward,
        I2DTransitionContext::k_start_menu,
        0.5F)};
    CHECK(classic_mid.outgoing.offset_x == doctest::Approx(-320.0F));
    CHECK(classic_mid.incoming.offset_x == doctest::Approx(320.0F));
    CHECK(classic_mid.outgoing.alpha == doctest::Approx(1.0F));
    CHECK(transition_duration(I2DMenuTransitionStyle::k_classic, I2DTransitionContext::k_options) ==
          doctest::Approx(0.0F));

    const auto reduced_mid{sample_transition(I2DMenuTransitionStyle::k_reduced_motion,
        I2DStateTransitionDirection::k_back,
        I2DTransitionContext::k_options,
        0.5F)};
    CHECK(reduced_mid.outgoing.offset_x == doctest::Approx(0.0F));
    CHECK(reduced_mid.incoming.offset_x == doctest::Approx(0.0F));
    CHECK(transition_duration(I2DMenuTransitionStyle::k_reduced_motion,
              I2DTransitionContext::k_options) == doctest::Approx(0.10F));
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
