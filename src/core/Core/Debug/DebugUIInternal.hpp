#pragma once

#include <fmt/format.h>
#include <imgui.h>
#include <spdlog/common.h>

#include <array>
#include <cstddef>
#include <ranges>
#include <string>
#include <vector>

#include "Core/Debug/Metrics.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Sprite/SpriteRenderMode.hpp"

namespace App::Debug {
namespace {

/// Map an spdlog level to an ImVec4 colour.
inline ImVec4 level_color(spdlog::level::level_enum lev) {
  switch (lev) {
    case spdlog::level::trace:
      return {0.6F, 0.6F, 0.6F, 1.0F};
    case spdlog::level::debug:
      return {0.5F, 0.5F, 1.0F, 1.0F};
    case spdlog::level::info:
      return {0.3F, 1.0F, 0.3F, 1.0F};
    case spdlog::level::warn:
      return {1.0F, 1.0F, 0.2F, 1.0F};
    case spdlog::level::err:
      return {1.0F, 0.3F, 0.3F, 1.0F};
    case spdlog::level::critical:
      return {1.0F, 0.0F, 0.0F, 1.0F};
    default:
      std::unreachable();  // off / n_levels never reach the sink
  }
}

/// Severity filter options shown in the log viewer combo.
inline constexpr std::array<spdlog::level::level_enum, 5> K_LOG_LEVEL_FILTER_OPTIONS{
    spdlog::level::trace,
    spdlog::level::debug,
    spdlog::level::info,
    spdlog::level::warn,
    spdlog::level::err};

/// Display names matching K_LOG_LEVEL_FILTER_OPTIONS.
inline constexpr std::array<const char*, 5> K_LOG_LEVEL_FILTER_NAMES{
    "Trace", "Debug", "Info", "Warning", "Error"};

/// Given the circular buffer history, return values in display order
/// (oldest first). 'head' points to the next write slot; 'count' is valid entries.
inline std::vector<float> get_history_display_order(
    const std::array<float, Metrics::kHistorySize>& history,
    const std::size_t head,
    const std::size_t count) {
  if (count == 0) {
    return {};
  }
  const std::size_t history_size{history.size()};
  const std::size_t start_idx{(head + history_size - count) % history_size};
  return std::views::iota(std::size_t{0}, count) |
         std::views::transform([&](const std::size_t idx) {
           return history.at((start_idx + idx) % history_size);
         }) |
         std::ranges::to<std::vector<float>>();
}

/// Build a label string for the frame-time plot overlay.
inline std::string plot_overlay_text(const float fps, const float frame_ms) {
  return fmt::format(
      "{:.1f} FPS  |  {:.2f} ms", static_cast<double>(fps), static_cast<double>(frame_ms));
}

/// Display name of one sprite render mode (inspector combo + tables).
inline const char* render_mode_name(const App::Sprite::SpriteRenderMode mode) {
  switch (mode) {
    case App::Sprite::SpriteRenderMode::k_default:
      return "Default";
    case App::Sprite::SpriteRenderMode::k_cutout:
      return "Cutout";
    case App::Sprite::SpriteRenderMode::k_alpha:
      return "Alpha";
    case App::Sprite::SpriteRenderMode::k_alpha_cutout:
      return "Alpha+Cutout";
    case App::Sprite::SpriteRenderMode::k_additive:
      return "Additive";
    case App::Sprite::SpriteRenderMode::k_additive_cutout:
      return "Additive+Cutout";
    case App::Sprite::SpriteRenderMode::k_darken:
      return "Darken";
    case App::Sprite::SpriteRenderMode::k_darken_cutout:
      return "Darken+Cutout";
    case App::Sprite::SpriteRenderMode::k_alternate_cutout:
      return "AlternateCutout";
    default:
      return "Unknown";
  }
}

/// Warning colour used by the inspector's visibility diagnostics.
inline constexpr ImVec4 K_WARNING_COLOR{1.0F, 0.55F, 0.1F, 1.0F};

/// Display name of a world-scene residency state.
inline const char* residency_name(const App::WorldSceneResidencyState state) {
  switch (state) {
    case App::WorldSceneResidencyState::Free:
      return "Free";
    case App::WorldSceneResidencyState::ResidentDetached:
      return "ResidentDetached";
    case App::WorldSceneResidencyState::ResidentAttached:
      return "ResidentAttached";
  }
  return "Unknown";
}

}  // anonymous namespace

}  // namespace App::Debug
