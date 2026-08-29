#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace App {

enum class DisplayMode : std::uint8_t {
  k_windowed = 0,
  k_borderless_fullscreen = 1,
  k_exclusive_fullscreen = 2,
};

struct DisplayResolution {
  int width{800};
  int height{600};
  friend constexpr bool operator==(const DisplayResolution&, const DisplayResolution&) = default;
};

struct DisplayModeInfo {
  DisplayResolution resolution;
  float refresh_rate{0.0F};
  std::uint32_t format{0};
  float pixel_density{1.0F};
};

struct DisplayModeCatalog {
  DisplayResolution desktop{800, 600};
  std::vector<DisplayResolution> resolutions;
  std::vector<DisplayResolution> exclusive_resolutions;
};

/// Packs logical client dimensions as positive signed 32-bit `width << 16 |
/// height`. Width is limited to 15 bits and height to 16 bits so the value is
/// stable in GameSettings' signed integer serialization.
[[nodiscard]] std::optional<std::int32_t> pack_display_resolution(DisplayResolution resolution);
[[nodiscard]] std::optional<DisplayResolution> unpack_display_resolution(std::int32_t raw_value);
[[nodiscard]] DisplayModeCatalog build_display_mode_catalog(
    const std::vector<DisplayModeInfo>& modes,
    std::optional<DisplayResolution> desktop,
    std::optional<DisplayResolution> custom_windowed = std::nullopt);
[[nodiscard]] bool is_exclusive_resolution_supported(
    const DisplayModeCatalog& catalog, DisplayResolution resolution);
[[nodiscard]] std::optional<DisplayResolution> select_exclusive_resolution(
    const DisplayModeCatalog& catalog, DisplayResolution preferred);
[[nodiscard]] DisplayMode toggle_display_mode(DisplayMode actual, DisplayMode preferred_fullscreen);
/// Returns whether OpenNomad may request an absolute position for a normal top-level window.
[[nodiscard]] bool supports_toplevel_window_positioning(std::string_view video_driver);
[[nodiscard]] DisplayMode fallback_display_mode(DisplayMode requested,
    DisplayResolution requested_resolution,
    const DisplayModeCatalog& catalog,
    bool exclusive_application_succeeded);
[[nodiscard]] std::string display_mode_label(DisplayMode mode);
[[nodiscard]] std::string display_resolution_label(DisplayResolution resolution);

}  // namespace App
