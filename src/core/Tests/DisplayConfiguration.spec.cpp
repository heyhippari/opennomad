#include "Core/DisplayConfiguration.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <vector>

TEST_SUITE("Core::DisplayConfiguration") {
  TEST_CASE("resolution packing is stable and reversible") {
    const App::DisplayResolution resolution{.width = 1920, .height = 1080};
    const std::optional<std::int32_t> packed{App::pack_display_resolution(resolution)};
    REQUIRE(packed.has_value());
    const std::int32_t packed_value{packed.value_or(0)};
    CHECK(App::unpack_display_resolution(packed_value) == resolution);
    CHECK_FALSE(App::unpack_display_resolution(0).has_value());
    CHECK_FALSE(App::pack_display_resolution(App::DisplayResolution{.width = 639, .height = 480})
            .has_value());
    CHECK_FALSE(App::pack_display_resolution(App::DisplayResolution{.width = 640, .height = 479})
            .has_value());
    CHECK_FALSE(App::pack_display_resolution(App::DisplayResolution{.width = 32768, .height = 1080})
            .has_value());
    CHECK_FALSE(App::unpack_display_resolution(-1).has_value());
  }

  TEST_CASE("catalog deduplicates, filters and sorts dimensions") {
    const std::vector<App::DisplayModeInfo> modes{
        {.resolution = {.width = 1920, .height = 1080}, .refresh_rate = 60.0F, .format = 1U},
        {.resolution = {.width = 800, .height = 600}, .refresh_rate = 75.0F, .format = 2U},
        {.resolution = {.width = 1920, .height = 1080}, .refresh_rate = 144.0F, .format = 3U},
        {.resolution = {.width = 320, .height = 200}, .refresh_rate = 60.0F, .format = 4U},
        {.resolution = {.width = 1280, .height = 720}, .refresh_rate = 60.0F, .format = 5U},
    };
    const App::DisplayModeCatalog catalog{App::build_display_mode_catalog(
        modes, App::DisplayResolution{.width = 2560, .height = 1440})};
    REQUIRE(catalog.resolutions.size() == 4U);
    CHECK(catalog.resolutions.at(0) == App::DisplayResolution{.width = 800, .height = 600});
    CHECK(catalog.resolutions.at(1) == App::DisplayResolution{.width = 1280, .height = 720});
    CHECK(catalog.resolutions.at(2) == App::DisplayResolution{.width = 1920, .height = 1080});
    CHECK(catalog.resolutions.at(3) == App::DisplayResolution{.width = 2560, .height = 1440});
  }

  TEST_CASE("catalog order is independent of SDL enumeration order and supports custom windows") {
    const std::vector<App::DisplayModeInfo> modes{
        {.resolution = {.width = 1920, .height = 1080}, .refresh_rate = 60.0F, .format = 1U},
        {.resolution = {.width = 800, .height = 600}, .refresh_rate = 60.0F, .format = 1U}};
    const App::DisplayModeCatalog catalog{App::build_display_mode_catalog(modes,
        App::DisplayResolution{.width = 1920, .height = 1080},
        App::DisplayResolution{.width = 1360, .height = 850})};
    CHECK(App::is_exclusive_resolution_supported(
        catalog, App::DisplayResolution{.width = 1920, .height = 1080}));
    CHECK_FALSE(App::is_exclusive_resolution_supported(
        catalog, App::DisplayResolution{.width = 1360, .height = 850}));
    bool custom_present{false};
    for (const App::DisplayResolution resolution : catalog.resolutions) {
      custom_present =
          custom_present || resolution == App::DisplayResolution{.width = 1360, .height = 850};
    }
    CHECK(custom_present);
  }

  TEST_CASE("fullscreen shortcut toggles only the preferred fullscreen mode") {
    CHECK(App::toggle_display_mode(
              App::DisplayMode::k_windowed, App::DisplayMode::k_borderless_fullscreen) ==
          App::DisplayMode::k_borderless_fullscreen);
    CHECK(
        App::toggle_display_mode(App::DisplayMode::k_windowed,
            App::DisplayMode::k_exclusive_fullscreen) == App::DisplayMode::k_exclusive_fullscreen);
    CHECK(App::toggle_display_mode(App::DisplayMode::k_borderless_fullscreen,
              App::DisplayMode::k_exclusive_fullscreen) == App::DisplayMode::k_windowed);
    CHECK(App::toggle_display_mode(App::DisplayMode::k_exclusive_fullscreen,
              App::DisplayMode::k_borderless_fullscreen) == App::DisplayMode::k_windowed);
  }

  TEST_CASE("top-level positioning is compositor-controlled on Wayland") {
    CHECK_FALSE(App::supports_toplevel_window_positioning("wayland"));
    CHECK(App::supports_toplevel_window_positioning("x11"));
    CHECK(App::supports_toplevel_window_positioning("windows"));
  }

  TEST_CASE("display mode labels are modern") {
    CHECK(App::display_mode_label(App::DisplayMode::k_windowed) == "Windowed");
    CHECK(App::display_mode_label(App::DisplayMode::k_borderless_fullscreen) ==
          "Borderless Fullscreen");
    CHECK(App::display_mode_label(App::DisplayMode::k_exclusive_fullscreen) ==
          "Exclusive Fullscreen");
  }

  TEST_CASE("missing exclusive resolutions fall back without inventing a mode") {
    const App::DisplayModeCatalog catalog{.desktop = {.width = 1920, .height = 1080},
        .resolutions = {{.width = 1280, .height = 720}},
        .exclusive_resolutions = {{.width = 1280, .height = 720}}};
    CHECK(App::fallback_display_mode(App::DisplayMode::k_exclusive_fullscreen,
              {.width = 1920, .height = 1080},
              catalog,
              false) == App::DisplayMode::k_borderless_fullscreen);
    CHECK(App::fallback_display_mode(App::DisplayMode::k_exclusive_fullscreen,
              {.width = 1280, .height = 720},
              catalog,
              true) == App::DisplayMode::k_exclusive_fullscreen);
    CHECK(App::select_exclusive_resolution(catalog, {.width = 1024, .height = 768}) ==
          App::DisplayResolution{.width = 1280, .height = 720});
  }

  TEST_CASE("exclusive resolution fallback prefers desktop then deterministic first mode") {
    const App::DisplayModeCatalog desktop_supported{.desktop = {.width = 1920, .height = 1080},
        .resolutions = {{.width = 800, .height = 600}, {.width = 1920, .height = 1080}},
        .exclusive_resolutions = {{.width = 800, .height = 600}, {.width = 1920, .height = 1080}}};
    CHECK(App::select_exclusive_resolution(desktop_supported, {.width = 2560, .height = 1440}) ==
          App::DisplayResolution{.width = 1920, .height = 1080});

    const App::DisplayModeCatalog desktop_unsupported{.desktop = {.width = 1920, .height = 1080},
        .resolutions = {{.width = 800, .height = 600}, {.width = 1280, .height = 720}},
        .exclusive_resolutions = {{.width = 800, .height = 600}, {.width = 1280, .height = 720}}};
    CHECK(App::select_exclusive_resolution(desktop_unsupported, {.width = 2560, .height = 1440}) ==
          App::DisplayResolution{.width = 800, .height = 600});
  }
}
