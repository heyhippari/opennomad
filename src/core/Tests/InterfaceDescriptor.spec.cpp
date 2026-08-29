#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, readability-suspicious-call-argument)

#include <cstdint>

#include "Core/Interface/InterfaceDescriptor.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Interface/StartMenuLayout.hpp"

TEST_SUITE("Core::Interface::InterfaceDescriptor") {
  using App::Interface::descriptor_for_id;

  TEST_CASE("[RUNTIME] interface 29 matches recovered descriptor metadata") {
    const App::Interface::InterfaceDescriptor* descriptor{descriptor_for_id(29)};
    REQUIRE(descriptor != nullptr);
    CHECK_EQ(descriptor->id, 29);
    CHECK(descriptor->name == "OMK START MENU");
    CHECK(descriptor->bitmap_name == "gfxint.bmp");
    CHECK(descriptor->string_table_name == "Menu");
    CHECK(descriptor->init != nullptr);
    CHECK(descriptor->destroy != nullptr);
    CHECK_EQ(descriptor->runtime_flags, 0x20000400U);
    REQUIRE(descriptor->sounds.has_value());
    CHECK(descriptor->sounds->navigate == "I2D/SOUNDS/men001.wav");
    CHECK(descriptor->sounds->confirm == "I2D/SOUNDS/men002.wav");
    CHECK(descriptor->sounds->cancel == "I2D/SOUNDS/men003.wav");
  }

  TEST_CASE("[RUNTIME] interface 35 uses recovered options sound slots") {
    const App::Interface::InterfaceDescriptor* descriptor{descriptor_for_id(35)};
    REQUIRE(descriptor != nullptr);
    REQUIRE(descriptor->sounds.has_value());
    CHECK(descriptor->sounds->navigate == "I2D/SOUNDS/SNK001.wav");
    CHECK(descriptor->sounds->confirm == "I2D/SOUNDS/SNK002.wav");
    CHECK(descriptor->sounds->cancel == "I2D/SOUNDS/SNK003.wav");
  }

  TEST_CASE("[OPENNOMAD] interface 29 presentation is separate from Runtime metadata") {
    const App::Interface::InterfaceDescriptor* descriptor{descriptor_for_id(29)};
    REQUIRE(descriptor != nullptr);
    REQUIRE(descriptor->presentation_hints.enter_fade.has_value());

    const auto enter{descriptor->presentation_hints.enter_fade.value_or(
        App::Interface::InterfaceFadePresentationHint{})};
    CHECK(enter.duration_seconds == doctest::Approx(0.50F));
    CHECK(enter.easing == App::Interface::InterfacePresentationEasing::k_quadratic_in);
    CHECK(enter.color.at(0) == doctest::Approx(0.0F));

    REQUIRE_EQ(descriptor->presentation_hints.completion_transitions.size(), 1U);
    const auto& new_game{descriptor->presentation_hints.completion_transitions.front()};
    CHECK_EQ(new_game.result, 3);
    CHECK(new_game.pre_delay_seconds == doctest::Approx(0.25F));
    CHECK(new_game.fade.duration_seconds == doctest::Approx(0.25F));
    CHECK(new_game.fade.easing == App::Interface::InterfacePresentationEasing::k_quadratic_in);
    CHECK(new_game.fade.color.at(0) == doctest::Approx(1.0F));
    CHECK(new_game.fade.color.at(1) == doctest::Approx(1.0F));
    CHECK(new_game.fade.color.at(2) == doctest::Approx(1.0F));
  }

  TEST_CASE("[RUNTIME] DIVERS is known metadata even without an OpenNomad implementation") {
    CHECK(descriptor_for_id(7) == nullptr);
    const App::Interface::InterfaceDescriptor* divers{descriptor_for_id(28)};
    REQUIRE(divers != nullptr);
    CHECK(divers->name == "DIVERS");
    CHECK(divers->init == nullptr);
  }

  TEST_CASE("root layout reproduces the recovered text entries") {
    using App::Interface::k_start_menu_root_entries;
    REQUIRE_EQ(k_start_menu_root_entries.size(), 4U);

    CHECK_EQ(k_start_menu_root_entries.at(0).string_index, 0U);
    CHECK_EQ(k_start_menu_root_entries.at(1).string_index, 1U);
    CHECK_EQ(k_start_menu_root_entries.at(2).string_index, 4U);
    CHECK_EQ(k_start_menu_root_entries.at(3).string_index, 5U);

    CHECK_EQ(k_start_menu_root_entries.at(0).font_key, 'I');
    CHECK_EQ(k_start_menu_root_entries.at(1).font_key, 'I');
    CHECK_EQ(k_start_menu_root_entries.at(2).font_key, 'I');
    CHECK_EQ(k_start_menu_root_entries.at(3).font_key, 'I');

    CHECK_EQ(k_start_menu_root_entries.at(0).x, 0);
    CHECK_EQ(k_start_menu_root_entries.at(0).y, 120);
    CHECK_EQ(k_start_menu_root_entries.at(0).width, 640);
    CHECK_EQ(k_start_menu_root_entries.at(0).height, 40);
    CHECK_EQ(k_start_menu_root_entries.at(1).y, 200);
    CHECK_EQ(k_start_menu_root_entries.at(2).y, 280);
    CHECK_EQ(k_start_menu_root_entries.at(3).y, 360);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, readability-suspicious-call-argument)
