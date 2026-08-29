#include "Core/DisplayConfiguration.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace App {

namespace {

constexpr int K_MINIMUM_WIDTH{640};
constexpr int K_MINIMUM_HEIGHT{480};
constexpr int K_MAXIMUM_WIDTH{32767};
constexpr int K_MAXIMUM_HEIGHT{65535};

[[nodiscard]] bool usable(const DisplayResolution resolution) {
  return resolution.width >= K_MINIMUM_WIDTH && resolution.height >= K_MINIMUM_HEIGHT &&
         resolution.width <= K_MAXIMUM_WIDTH && resolution.height <= K_MAXIMUM_HEIGHT;
}

}  // namespace

std::optional<std::int32_t> pack_display_resolution(const DisplayResolution resolution) {
  if (!usable(resolution)) {
    return std::nullopt;
  }
  const std::uint32_t packed{(static_cast<std::uint32_t>(resolution.width) << 16U) |
                             static_cast<std::uint32_t>(resolution.height)};
  if (packed > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(packed);
}

std::optional<DisplayResolution> unpack_display_resolution(const std::int32_t raw_value) {
  if (raw_value <= 0) {
    return std::nullopt;
  }
  const std::uint32_t packed{static_cast<std::uint32_t>(raw_value)};
  const DisplayResolution resolution{
      .width = static_cast<int>(packed >> 16U), .height = static_cast<int>(packed & 0xFFFFU)};
  return usable(resolution) ? std::optional<DisplayResolution>{resolution} : std::nullopt;
}

DisplayModeCatalog build_display_mode_catalog(const std::vector<DisplayModeInfo>& modes,
    const std::optional<DisplayResolution> desktop,
    const std::optional<DisplayResolution> custom_windowed) {
  DisplayModeCatalog catalog;
  if (desktop.has_value() && usable(desktop.value())) {
    catalog.desktop = desktop.value();
  }
  for (const DisplayModeInfo& mode : modes) {
    if (usable(mode.resolution)) {
      catalog.resolutions.push_back(mode.resolution);
      catalog.exclusive_resolutions.push_back(mode.resolution);
    }
  }
  if (usable(catalog.desktop)) {
    catalog.resolutions.push_back(catalog.desktop);
  }
  if (custom_windowed.has_value() && usable(custom_windowed.value())) {
    catalog.resolutions.push_back(custom_windowed.value());
  }
  std::ranges::sort(catalog.resolutions, {}, [](const DisplayResolution resolution) {
    return std::pair{resolution.width, resolution.height};
  });
  const auto unique_resolutions{std::ranges::unique(catalog.resolutions)};
  catalog.resolutions.erase(unique_resolutions.begin(), unique_resolutions.end());
  std::ranges::sort(catalog.exclusive_resolutions, {}, [](const DisplayResolution resolution) {
    return std::pair{resolution.width, resolution.height};
  });
  const auto unique_exclusive{std::ranges::unique(catalog.exclusive_resolutions)};
  catalog.exclusive_resolutions.erase(unique_exclusive.begin(), unique_exclusive.end());
  return catalog;
}

bool is_exclusive_resolution_supported(
    const DisplayModeCatalog& catalog, const DisplayResolution resolution) {
  return std::ranges::find(catalog.exclusive_resolutions, resolution) !=
         catalog.exclusive_resolutions.end();
}

std::optional<DisplayResolution> select_exclusive_resolution(
    const DisplayModeCatalog& catalog, const DisplayResolution preferred) {
  if (is_exclusive_resolution_supported(catalog, preferred)) {
    return preferred;
  }
  if (is_exclusive_resolution_supported(catalog, catalog.desktop)) {
    return catalog.desktop;
  }
  if (!catalog.exclusive_resolutions.empty()) {
    return catalog.exclusive_resolutions.front();
  }
  return std::nullopt;
}

DisplayMode toggle_display_mode(const DisplayMode actual, const DisplayMode preferred_fullscreen) {
  return actual == DisplayMode::k_windowed ? preferred_fullscreen : DisplayMode::k_windowed;
}

bool supports_toplevel_window_positioning(const std::string_view video_driver) {
  // Wayland compositors own placement of normal top-level windows.
  return video_driver != "wayland";
}

DisplayMode fallback_display_mode(const DisplayMode requested,
    const DisplayResolution requested_resolution,
    const DisplayModeCatalog& catalog,
    const bool exclusive_application_succeeded) {
  if (requested != DisplayMode::k_exclusive_fullscreen) {
    return requested;
  }
  if (exclusive_application_succeeded &&
      is_exclusive_resolution_supported(catalog, requested_resolution)) {
    return requested;
  }
  return DisplayMode::k_borderless_fullscreen;
}

std::string display_mode_label(const DisplayMode mode) {
  switch (mode) {
    case DisplayMode::k_windowed:
      return "Windowed";
    case DisplayMode::k_borderless_fullscreen:
      return "Borderless Fullscreen";
    case DisplayMode::k_exclusive_fullscreen:
      return "Exclusive Fullscreen";
  }
  return "Windowed";
}

std::string display_resolution_label(const DisplayResolution resolution) {
  return std::to_string(resolution.width) + " x " + std::to_string(resolution.height);
}

}  // namespace App
