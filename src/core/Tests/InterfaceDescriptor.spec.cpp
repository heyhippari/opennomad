#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, readability-suspicious-call-argument)

#include <cstdint>

#include "Core/Interface/InterfaceDescriptor.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Interface/StartMenuLayout.hpp"

TEST_SUITE("Core::Interface::InterfaceDescriptor") {
  using App::Interface::descriptor_for_id;

  TEST_CASE("interface 29 matches the recovered descriptor metadata") {
    const App::Interface::InterfaceDescriptor* descriptor{descriptor_for_id(29)};
    REQUIRE(descriptor != nullptr);
    CHECK_EQ(descriptor->id, 29);
    CHECK(descriptor->name == "OMK START MENU");
    CHECK(descriptor->bitmap_name == "gfxint.bmp");
    CHECK(descriptor->string_table_name == "Menu");
    CHECK(descriptor->init != nullptr);
    CHECK(descriptor->destroy != nullptr);
    CHECK_EQ(descriptor->runtime_flags, 0x20000400U);
  }

  TEST_CASE("an unknown interface has no descriptor") {
    CHECK(descriptor_for_id(7) == nullptr);
    CHECK(descriptor_for_id(28) == nullptr);
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
