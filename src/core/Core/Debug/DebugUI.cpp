#include "DebugUI.hpp"

#include <glad/glad.h>
#include <imgui.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>
#include <spdlog/common.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/LogFilter.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/ModelViewerScene.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Scenario/ScenarioEngine.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"
#include "Core/Script/ScriptOpcode.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Sprite/SpriteFrame.hpp"
#include "Core/Sprite/SpriteInstance.hpp"
#include "Core/Sprite/SpritePool.hpp"
#include "Core/Sprite/SpriteRenderMode.hpp"
#include "Core/Sprite/SpriteRenderer.hpp"
#include "Core/Sprite/SpriteResource.hpp"
#include "Core/Startup/StartupCoordinator.hpp"
#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"
#include "Core/Texture.hpp"

namespace App::Debug {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Map an spdlog level to an ImVec4 colour.
inline ImVec4 level_color(spdlog::level::level_enum lev) {
  switch (lev) {
    case spdlog::level::trace:    return {0.6F, 0.6F, 0.6F, 1.0F};
    case spdlog::level::debug:    return {0.5F, 0.5F, 1.0F, 1.0F};
    case spdlog::level::info:     return {0.3F, 1.0F, 0.3F, 1.0F};
    case spdlog::level::warn:     return {1.0F, 1.0F, 0.2F, 1.0F};
    case spdlog::level::err:      return {1.0F, 0.3F, 0.3F, 1.0F};
    case spdlog::level::critical: return {1.0F, 0.0F, 0.0F, 1.0F};
    default:                      std::unreachable();  // off / n_levels never reach the sink
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
    case App::Sprite::SpriteRenderMode::k_default:         return "Default";
    case App::Sprite::SpriteRenderMode::k_cutout:          return "Cutout";
    case App::Sprite::SpriteRenderMode::k_alpha:           return "Alpha";
    case App::Sprite::SpriteRenderMode::k_alpha_cutout:    return "Alpha+Cutout";
    case App::Sprite::SpriteRenderMode::k_additive:        return "Additive";
    case App::Sprite::SpriteRenderMode::k_additive_cutout: return "Additive+Cutout";
    case App::Sprite::SpriteRenderMode::k_darken:          return "Darken";
    case App::Sprite::SpriteRenderMode::k_darken_cutout:   return "Darken+Cutout";
    case App::Sprite::SpriteRenderMode::k_alternate_cutout: return "AlternateCutout";
    default:                                              return "Unknown";
  }
}

/// Warning colour used by the inspector's visibility diagnostics.
inline constexpr ImVec4 K_WARNING_COLOR{1.0F, 0.55F, 0.1F, 1.0F};

/// Display name of a world-scene residency state.
inline const char* residency_name(const App::WorldSceneResidencyState state) {
  switch (state) {
    case App::WorldSceneResidencyState::Free:          return "Free";
    case App::WorldSceneResidencyState::LoadedInactive: return "LoadedInactive";
    case App::WorldSceneResidencyState::LoadedActive:   return "LoadedActive";
  }
  return "Unknown";
}

}  // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

DebugUI::DebugUI(SDL_Window* window) : m_window(window) {}

// ─────────────────────────────────────────────────────────────────────────────
// Toggles
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::toggle_performance() { m_show_performance = !m_show_performance; }
void DebugUI::toggle_system_info() { m_show_system_info = !m_show_system_info; }
void DebugUI::toggle_profiler() { m_show_profiler = !m_show_profiler; }
void DebugUI::toggle_log() { m_show_log = !m_show_log; }
void DebugUI::toggle_opengl_state() { m_show_opengl_state = !m_show_opengl_state; }
void DebugUI::toggle_sprite_inspector() { m_show_sprite_inspector = !m_show_sprite_inspector; }
void DebugUI::toggle_overlays() { m_show_overlays = !m_show_overlays; }
void DebugUI::toggle_script_debugger() { m_show_script_debugger = !m_show_script_debugger; }
void DebugUI::toggle_audio_inspector() { m_show_audio_inspector = !m_show_audio_inspector; }
void DebugUI::toggle_scenarios() { m_show_scenarios = !m_show_scenarios; }
void DebugUI::toggle_area_script() { m_show_area_script = !m_show_area_script; }
void DebugUI::toggle_startup() { m_show_startup = !m_show_startup; }
void DebugUI::toggle_interface() { m_show_interface = !m_show_interface; }
void DebugUI::toggle_startup_trace() { m_show_startup_trace = !m_show_startup_trace; }

// ─────────────────────────────────────────────────────────────────────────────
// Main update
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::update(const float delta_time) {
  // The menu bar is the debug pointer UI: it is visible only while the mouse
  // is detached (relative/capture mode off). F12 releases the mouse and
  // reveals the bar; clicking back into the scene hides it again.
  if (m_window != nullptr && !SDL_GetWindowRelativeMouseMode(m_window)) {
    show_menu_bar();
  }

  // Periodically refresh window info (every 60 frames ≈ 1s at 60fps).
  m_frame_count += 1;
  if (m_frame_count % 60 == 0 && m_window != nullptr) {
    auto& metrics = Metrics::get();

    int win_w{0};
    int win_h{0};
    SDL_GetWindowSize(m_window, &win_w, &win_h);
    metrics.set_window_info_entry("Width", fmt::format("{}", win_w));
    metrics.set_window_info_entry("Height", fmt::format("{}", win_h));

    const float dpi_scale = SDL_GetWindowDisplayScale(m_window);
    metrics.set_window_info_entry("DPI Scale", fmt::format("{:.2f}", dpi_scale));

    const SDL_DisplayMode* mode = SDL_GetWindowFullscreenMode(m_window);
    if (mode != nullptr) {
      metrics.set_window_info_entry("Refresh Rate", fmt::format("{} Hz", mode->refresh_rate));
    } else {
      // Query the current display's mode for refresh rate.
      const SDL_DisplayID display_id = SDL_GetDisplayForWindow(m_window);
      if (display_id != 0) {
        const SDL_DisplayMode* current_mode = SDL_GetCurrentDisplayMode(display_id);
        if (current_mode != nullptr) {
          metrics.set_window_info_entry("Refresh Rate",
              fmt::format("{} Hz", current_mode->refresh_rate));
        }
      }
    }

    int vsync_interval{0};
    SDL_GL_GetSwapInterval(&vsync_interval);
    metrics.set_window_info_entry("VSync", (vsync_interval == 1) ? "On" : "Off");

    const SDL_WindowFlags win_flags = SDL_GetWindowFlags(m_window);
    const bool fullscreen = (win_flags & SDL_WINDOW_FULLSCREEN) != 0;
    metrics.set_window_info_entry("Mode", fullscreen ? "Fullscreen" : "Windowed");
  }

  // --- Render active windows ---
  if (m_show_performance) {
    show_performance(delta_time);
  }
  if (m_show_system_info) {
    show_system_info();
  }
  if (m_show_profiler) {
    show_profiler();
  }
  if (m_show_log) {
    show_log();
  }
  if (m_show_opengl_state) {
    show_opengl_state();
  }
  if (m_show_overlays) {
    show_overlays();
  }
  if (m_show_sprite_inspector) {
    show_sprite_inspector(delta_time);
  }
  if (m_show_script_debugger) {
    show_script_debugger();
  }
  if (m_show_audio_inspector) {
    show_audio_inspector();
  }
  if (m_show_scenarios) {
    show_scenarios();
  }
  if (m_show_area_script) {
    show_area_script();
  }
  if (m_show_startup) {
    show_startup();
  }
  if (m_show_interface) {
    show_interface();
  }
  if (m_show_startup_trace) {
    show_startup_trace();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Menu bar
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_menu_bar() {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Performance", nullptr, &m_show_performance);
      ImGui::MenuItem("System Info", nullptr, &m_show_system_info);
      ImGui::MenuItem("Profiler", nullptr, &m_show_profiler);
      ImGui::MenuItem("Log", nullptr, &m_show_log);
      ImGui::MenuItem("OpenGL State", nullptr, &m_show_opengl_state);
      ImGui::MenuItem("Overlays", nullptr, &m_show_overlays);
      ImGui::MenuItem("Sprite Inspector", nullptr, &m_show_sprite_inspector);
      ImGui::MenuItem("Script Debugger", nullptr, &m_show_script_debugger);
      ImGui::MenuItem("Audio Inspector", nullptr, &m_show_audio_inspector);
      ImGui::MenuItem("Scenarios", nullptr, &m_show_scenarios);
      ImGui::MenuItem("Area Script", nullptr, &m_show_area_script);
      ImGui::MenuItem("Startup / IAM", nullptr, &m_show_startup);
      ImGui::MenuItem("Interface", nullptr, &m_show_interface);
      ImGui::MenuItem("Startup Trace", nullptr, &m_show_startup_trace);
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Performance window
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_performance(float /*delta_time*/) {
  // The performance readout is an overlay, not a window: undecorated,
  // auto-sized, non-focusable and pinned just below the menu bar. A
  // semi-transparent background keeps the scene visible behind it.
  constexpr ImGuiWindowFlags k_overlay_flags = ImGuiWindowFlags_NoDecoration |
                                               ImGuiWindowFlags_AlwaysAutoResize |
                                               ImGuiWindowFlags_NoSavedSettings |
                                               ImGuiWindowFlags_NoFocusOnAppearing |
                                               ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
  const ImGuiViewport* viewport{ImGui::GetMainViewport()};
  ImGui::SetNextWindowPos(
      ImVec2{viewport->Pos.x + 10.0F, viewport->Pos.y + ImGui::GetFrameHeight() + 10.0F});
  ImGui::SetNextWindowBgAlpha(0.55F);
  ImGui::Begin("Performance", &m_show_performance, k_overlay_flags);

  const auto& metrics = Metrics::get();

  // --- FPS and frame time ---
  const float fps = metrics.get_fps();
  const float frame_ms = metrics.get_frame_time_ms();

  // FPS color coding
  ImVec4 fps_color;
  if (fps >= 55.0F) {
    fps_color = {0.2F, 1.0F, 0.2F, 1.0F};  // Green
  } else if (fps >= 30.0F) {
    fps_color = {1.0F, 1.0F, 0.2F, 1.0F};  // Yellow
  } else {
    fps_color = {1.0F, 0.2F, 0.2F, 1.0F};  // Red
  }

  ImGui::Text("FPS: ");
  ImGui::SameLine();
  ImGui::TextColored(fps_color, "%.1f", static_cast<double>(fps));
  ImGui::SameLine(160.0F);
  ImGui::Text("Frame time: %.2f ms", static_cast<double>(frame_ms));

  // --- Frame time graph ---
  const auto history = metrics.get_frame_time_history();
  const std::size_t head = metrics.get_frame_time_history_head();
  const std::size_t count = metrics.get_frame_time_history_count();
  const auto display_order = get_history_display_order(history, head, count);

  if (!display_order.empty()) {
    const float max_val = *std::ranges::max_element(display_order);
    const float plot_max = std::max(max_val * 1.2F, 16.67F);  // at least 60fps baseline

    const std::string overlay = plot_overlay_text(fps, frame_ms);
    ImGui::PlotLines("##FrameTime",
        display_order.data(),
        static_cast<int>(display_order.size()),
        0,
        overlay.c_str(),
        0.0F,
        plot_max,
        ImVec2(0, 80));
  }

  // --- Stats ---
  ImGui::Separator();
  ImGui::Text("Min: %.2f ms  |  Avg: %.2f ms  |  Max: %.2f ms",
      static_cast<double>(metrics.get_frame_time_min()),
      static_cast<double>(metrics.get_frame_time_avg()),
      static_cast<double>(metrics.get_frame_time_max()));

  ImGui::Text("Frames: %lu  |  Elapsed: %.1f s",
      static_cast<unsigned long>(metrics.get_frame_count()),
      static_cast<double>(metrics.get_total_elapsed()));

  // --- Sprite pipeline ---
  const auto& sprites = metrics.sprite_counters();
  ImGui::Separator();
  ImGui::Text("Sprites: %lu live | %lu attached | %lu visible | %lu drawn",
      static_cast<unsigned long>(sprites.live),
      static_cast<unsigned long>(sprites.attached),
      static_cast<unsigned long>(sprites.visible),
      static_cast<unsigned long>(sprites.drawn));
  ImGui::Text("Culled: %lu | Invalid: %lu | Batches: %lu | Draw calls: %lu",
      static_cast<unsigned long>(sprites.culled),
      static_cast<unsigned long>(sprites.invalid),
      static_cast<unsigned long>(sprites.batches),
      static_cast<unsigned long>(sprites.draw_calls));

  // --- I2D interface pipeline ---
  const auto& i2d = metrics.i2d_counters();
  ImGui::Text("I2D: %lu draws | %lu quads | %lu glyphs",
      static_cast<unsigned long>(i2d.draw_calls),
      static_cast<unsigned long>(i2d.quads),
      static_cast<unsigned long>(i2d.glyphs));
  ImGui::Text("I2D bg: tick %lu | alpha %.3f | %s | ticks %lu | bytes %lu",
      static_cast<unsigned long>(i2d.background_tick),
      static_cast<double>(i2d.background_alpha),
      i2d.background_interpolated ? "interp" : "stepped",
      static_cast<unsigned long>(i2d.background_ticks),
      static_cast<unsigned long>(i2d.background_bytes_uploaded));
  ImGui::Text("I2D bg: %lu warp passes | %lu draws",
      static_cast<unsigned long>(i2d.background_warp_passes),
      static_cast<unsigned long>(i2d.background_draw_calls));

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// System Info window
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_system_info() {
  ImGui::Begin("System Info", &m_show_system_info);

  const auto& metrics = Metrics::get();

  auto show_section = [](const char* title, const std::flat_map<std::string, std::string>& info) {
    if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Indent();
      for (const auto& [key, value] : info) {
        ImGui::Text("%s:", key.c_str());
        ImGui::SameLine(180.0F);
        ImGui::TextUnformatted(value.c_str());
      }
      ImGui::Unindent();
    }
  };

  // Static app info
  if (ImGui::CollapsingHeader("App", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent();
    ImGui::Text("Name:      OpenNomad");
    ImGui::Text("Version:   0.0.1");
#if DEBUG
    ImGui::Text("Build:     Debug");
#else
    ImGui::Text("Build:     Release");
#endif
    ImGui::Unindent();
  }

  show_section("Window", metrics.window_info());
  show_section("OpenGL", metrics.opengl_info());
  show_section("SDL", metrics.sdl_info());
  show_section("Audio", metrics.audio_info());

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Profiler window
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_profiler() {
  ImGui::Begin("Profiler", &m_show_profiler);

  const auto profiles_copy = Instrumentor::get().get_recent_profiles();

  if (profiles_copy.empty()) {
    ImGui::TextUnformatted("No profile data yet. Is APP_PROFILE enabled?");
  } else {
    // Aggregate by scope name: sum durations and count occurrences.
    struct ScopeStats {
      std::chrono::microseconds total_time{0};
      std::size_t call_count{0};
    };
    std::flat_map<std::string, ScopeStats> aggregated;

    for (const auto& profile : profiles_copy) {
      auto& stats = aggregated[profile.name];
      stats.total_time += profile.elapsed_time;
      stats.call_count += 1;
    }

    // Sort by total time descending.
    std::vector<std::pair<std::string, ScopeStats>> sorted(
        aggregated.begin(), aggregated.end());
    std::ranges::sort(sorted,
        [](const auto& left, const auto& right) {
          return left.second.total_time > right.second.total_time;
        });

    // Limit to top 15 entries.
    const std::ptrdiff_t display_max{
        std::min(static_cast<std::ptrdiff_t>(sorted.size()), std::ptrdiff_t{15})};

    // Table header
    ImGui::Text("%-6s %-50s %12s %8s", "Rank", "Scope", "Total (us)", "Calls");
    ImGui::Separator();

    for (const auto& [rank, entry] : std::views::enumerate(std::views::take(sorted, display_max))) {
      const auto& [scope_name, scope_stats] = entry;

      // Truncate very long scope names for display.
      std::string display_name = scope_name;
      if (display_name.size() > 55) {
        display_name = "..." + display_name.substr(display_name.size() - 52);
      }

      ImGui::Text("%-6zu %-50s %12lld %8zu",
          static_cast<std::size_t>(rank) + 1,
          display_name.c_str(),
          static_cast<long long>(scope_stats.total_time.count()),
          scope_stats.call_count);
    }

    ImGui::Separator();
    ImGui::Text("Total scopes recorded: %zu", profiles_copy.size());
  }

  // Clear for next frame's data.
  Instrumentor::get().clear_recent_profiles();

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Log window
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_log() {
  ImGui::Begin("Log", &m_show_log);

  // Clear button
  if (ImGui::Button("Clear")) {
    if (auto sink = Log::get_debug_sink()) {
      sink->clear();
    }
  }
  ImGui::SameLine();
  ImGui::Checkbox("Auto-scroll", &m_log_auto_scroll);

  // Severity filter (display-side; the ring buffer captures trace already).
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.0F);
  ImGui::Combo("Min level",
      &m_log_min_level_index,
      K_LOG_LEVEL_FILTER_NAMES.data(),
      static_cast<int>(K_LOG_LEVEL_FILTER_NAMES.size()));

  // Category filter: "All" plus one entry per category.
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110.0F);
  std::array<const char*, k_log_category_count + 1> category_names{"All"};
  for (std::size_t index{0}; index < k_log_category_count; ++index) {
    category_names.at(index + 1) =
        log_category_name(k_all_log_categories.at(index)).data();
  }
  ImGui::Combo("Category",
      &m_log_category_index,
      category_names.data(),
      static_cast<int>(category_names.size()));

  // Text search (case-insensitive substring).
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200.0F);
  ImGui::InputTextWithHint("##logfilter", "search…", m_log_filter_text, sizeof(m_log_filter_text));

  // Materialize the combo selections into the filter.
  m_log_filter.min_level =
      K_LOG_LEVEL_FILTER_OPTIONS.at(static_cast<std::size_t>(m_log_min_level_index));
  if (m_log_category_index <= 0) {
    m_log_filter.category_mask = 0xFFFF'FFFFU;
  } else {
    const std::size_t category_index{static_cast<std::size_t>(m_log_category_index - 1)};
    m_log_filter.category_mask =
        1U << static_cast<std::uint32_t>(k_all_log_categories.at(category_index));
  }
  m_log_filter.text = m_log_filter_text;

  ImGui::Separator();

  // Reserve height for the log content area.
  const float footer_height = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("LogScrollRegion", ImVec2(0, -footer_height), 0,
      ImGuiWindowFlags_HorizontalScrollbar);

  if (auto sink = Log::get_debug_sink()) {
    const auto entries = sink->get_entries();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(entries.size()));
    while (clipper.Step()) {
      for (int line_idx = clipper.DisplayStart; line_idx < clipper.DisplayEnd; ++line_idx) {
        const auto& entry = entries.at(static_cast<std::size_t>(line_idx));
        if (!m_log_filter.matches(entry.level, entry.category, entry.line)) {
          continue;
        }
        ImGui::TextColored(level_color(entry.level), "%s", entry.line.c_str());
      }
    }
    clipper.End();

    // Auto-scroll to bottom.
    if (m_log_auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
      ImGui::SetScrollHereY(1.0F);
    }
  } else {
    ImGui::TextUnformatted("Log sink not available.");
  }

  ImGui::EndChild();
  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// OpenGL State window
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_opengl_state() {
  ImGui::Begin("OpenGL State", &m_show_opengl_state);

  if (ImGui::CollapsingHeader("Viewport", ImGuiTreeNodeFlags_DefaultOpen)) {
    std::array<GLint, 4> viewport{};
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    ImGui::Indent();
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    ImGui::Text("x: %d  y: %d  w: %d  h: %d",
        viewport[0], viewport[1], viewport[2], viewport[3]);
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    ImGui::Unindent();
  }

  if (ImGui::CollapsingHeader("Capabilities", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent();
    const auto show_gl_bool = [](const char* label, GLenum cap) {
      const bool enabled = glIsEnabled(cap) != 0U;
      const ImVec4 color = enabled ? ImVec4{0.2F, 1.0F, 0.2F, 1.0F}
                                   : ImVec4{1.0F, 0.3F, 0.3F, 1.0F};
      ImGui::Text("%s:", label);
      ImGui::SameLine(160.0F);
      ImGui::TextColored(color, "%s", enabled ? "Enabled" : "Disabled");
    };

    show_gl_bool("Depth Test", GL_DEPTH_TEST);
    show_gl_bool("Stencil Test", GL_STENCIL_TEST);
    show_gl_bool("Cull Face", GL_CULL_FACE);
    show_gl_bool("Blend", GL_BLEND);
    show_gl_bool("Multisample", GL_MULTISAMPLE);
    show_gl_bool("Framebuffer sRGB", GL_FRAMEBUFFER_SRGB);
    show_gl_bool("Scissor Test", GL_SCISSOR_TEST);
    ImGui::Unindent();
  }

  if (ImGui::CollapsingHeader("Clear Color")) {
    std::array<GLfloat, 4> clear_color{};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color.data());
    ImGui::Indent();
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    ImGui::Text("R: %.2f  G: %.2f  B: %.2f  A: %.2f",
        static_cast<double>(clear_color[0]),
        static_cast<double>(clear_color[1]),
        static_cast<double>(clear_color[2]),
        static_cast<double>(clear_color[3]));
    ImGui::ColorButton("##ClearColorPreview",
        ImVec4(clear_color[0], clear_color[1], clear_color[2], clear_color[3]),
        ImGuiColorEditFlags_NoAlpha,
        ImVec2(ImGui::GetTextLineHeight() * 4, ImGui::GetTextLineHeight()));
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    // Set the color edit to the clear color
    ImGui::SameLine();
    ImGui::ColorEdit4("##ClearColorEdit", clear_color.data(),
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::Unindent();
  }

  if (ImGui::CollapsingHeader("Depth")) {
    GLint depth_func{0};
    GLboolean depth_mask{0};
    glGetIntegerv(GL_DEPTH_FUNC, &depth_func);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    ImGui::Indent();

    const char* func_name = "Unknown";
    switch (depth_func) {
      case GL_NEVER:    func_name = "Never";    break;
      case GL_LESS:     func_name = "Less";     break;
      case GL_EQUAL:    func_name = "Equal";    break;
      case GL_LEQUAL:   func_name = "LEqual";   break;
      case GL_GREATER:  func_name = "Greater";  break;
      case GL_NOTEQUAL: func_name = "NotEqual"; break;
      case GL_GEQUAL:   func_name = "GEqual";   break;
      case GL_ALWAYS:   func_name = "Always";   break;
      default: break;
    }
    ImGui::Text("Function: %s", func_name);
    ImGui::Text("Write Mask: %s", (depth_mask != 0) ? "On" : "Off");
    ImGui::Unindent();
  }

  if (ImGui::CollapsingHeader("Cull Face")) {
    GLint cull_mode{0};
    GLint front_face{0};
    glGetIntegerv(GL_CULL_FACE_MODE, &cull_mode);
    glGetIntegerv(GL_FRONT_FACE, &front_face);
    ImGui::Indent();
    const char* cull_name = "Front & Back";
    if (cull_mode == GL_BACK) {
      cull_name = "Back";
    } else if (cull_mode == GL_FRONT) {
      cull_name = "Front";
    }
    ImGui::Text("Cull Mode: %s", cull_name);
    ImGui::Text("Front Face: %s", front_face == GL_CCW ? "CCW" : "CW");
    ImGui::Unindent();
  }

  if (ImGui::CollapsingHeader("Active Program")) {
    GLint program{0};
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    ImGui::Indent();
    ImGui::Text("Program: %d", program);
    ImGui::Unindent();
  }

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlays
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_overlays() {
  ImGui::Begin("Overlays", &m_show_overlays);

  auto* scene{dynamic_cast<ModelViewerScene*>(m_context.scene)};
  if (scene == nullptr) {
    ImGui::TextUnformatted("Requires a 3D model scene.");
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("World-space debug overlays drawn on top of the scene.");

  bool lights{scene->light_overlay_enabled()};
  if (ImGui::Checkbox("Lights", &lights)) {
    scene->set_light_overlay_enabled(lights);
  }
  ImGui::SameLine();
  ImGui::TextDisabled("markers, spot lines, attenuation spheres");

  bool sprites{scene->sprite_overlay_enabled()};
  if (ImGui::Checkbox("Sprites", &sprites)) {
    scene->set_sprite_overlay_enabled(sprites);
  }
  ImGui::SameLine();
  ImGui::TextDisabled("billboard outlines, colour-coded by render mode");

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprite Inspector
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_sprite_inspector(const float delta_time) {
  ImGui::Begin("Sprite Inspector", &m_show_sprite_inspector);

  ScenarioRuntime* runtime{m_context.scenario_manager == nullptr
                               ? nullptr
                               : m_context.scenario_manager->gameplay_runtime()};
  if (runtime == nullptr) {
    ImGui::TextUnformatted("Sprite runtime not available.");
    ImGui::End();
    return;
  }
  auto* scene{dynamic_cast<ModelViewerScene*>(m_context.scene)};

  if (ImGui::BeginTabBar("SpriteTabs")) {
    if (ImGui::BeginTabItem("Resources")) {
      show_sprite_resources_tab(*runtime, scene);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Instances")) {
      show_sprite_instances_tab(*runtime, scene, delta_time);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Frames")) {
      show_sprite_frames_tab(*runtime);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Render Queue")) {
      if (scene == nullptr) {
        ImGui::TextUnformatted("Not initialised: needs a 3D model scene.");
      } else {
        show_sprite_queue_tab(*scene);
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

void DebugUI::show_sprite_resources_tab(ScenarioRuntime& runtime, ModelViewerScene* const scene) {
  ImGui::Text("Select an embedded effect resource, then spawn an instance.");
  ImGui::Text("Decoding is lazy: the resource loads on the first spawn.");
  if (ImGui::Button("Spawn from selected resource")) {
    std::size_t object_index{0};
    if (const Sprite::SpriteResource* resource{runtime.sprite_resource(m_sprite_selected_resource)};
        resource != nullptr) {
      object_index = resource->default_object_index();
    }
    const std::array<float, 3> position{
        scene == nullptr ? runtime.world_anchor() : scene->camera_focus_position()};
    if (auto handle{runtime.spawn_sprite(m_sprite_selected_resource, object_index, position)};
        handle.has_value()) {
      m_sprite_selected_handle = handle.value();
      if (auto frame{runtime.set_sprite_frame(handle.value(), 0)}; !frame) {
        App::Log::warn(LogCategory::Debug, "Sprite frame selection failed: {}", frame.error());
      }
      App::Log::debug(LogCategory::Debug,
          "Spawned sprite {}:{} from resource '{}'",
          handle->index,
          handle->generation,
          runtime.sprite_resource_name(m_sprite_selected_resource));
    } else {
      App::Log::error(LogCategory::Debug, "Sprite spawn failed: {}", handle.error());
    }
  }
  ImGui::Separator();

  for (std::size_t index{0}; index < runtime.sprite_resource_count(); ++index) {
    const Sprite::SpriteResource* resource{runtime.sprite_resource(index)};
    const std::string label{fmt::format("{}: {}{}##resource{}",
        index,
        runtime.sprite_resource_name(index),
        resource == nullptr ? " (not decoded)" : "",
        index)};
    if (ImGui::Selectable(label.c_str(), m_sprite_selected_resource == index)) {
      m_sprite_selected_resource = index;
    }
  }
}

void DebugUI::show_sprite_instances_tab(ScenarioRuntime& runtime,
    ModelViewerScene* const scene,
    const float delta_time) {
  Sprite::SpritePool& pool{runtime.sprite_pool()};

  ImGui::Text("Pool: %lu live / %lu capacity / %lu attached",
      static_cast<unsigned long>(pool.live_count()),
      static_cast<unsigned long>(pool.capacity()),
      static_cast<unsigned long>(pool.attached_count()));

  if (ImGui::BeginChild("##SpriteInstances", ImVec2(0.0F, 120.0F), ImGuiChildFlags_Borders)) {
    for (auto head{pool.render_list_head()}; head.has_value();
         head = pool.render_list_next(*head)) {
      const Sprite::SpriteInstance* instance{pool.find(*head)};
      if (instance == nullptr) {
        continue;
      }
      const std::string label{fmt::format("{}:{} '{}' frame {} mode {}##inst{}",
          instance->handle.index,
          instance->handle.generation,
          runtime.sprite_resource_name(instance->resource_index),
          instance->frame_index,
          render_mode_name(instance->render_mode),
          instance->handle.index)};
      if (ImGui::Selectable(label.c_str(), m_sprite_selected_handle == *head)) {
        m_sprite_selected_handle = *head;
      }
    }
  }
  ImGui::EndChild();
  ImGui::Separator();

  const Sprite::SpriteInstance* instance{pool.find(m_sprite_selected_handle)};
  if (instance == nullptr) {
    ImGui::TextUnformatted("Select an instance to edit it (spawn one from the Resources tab).");
    return;
  }
  const Sprite::SpriteHandle handle{instance->handle};

  const std::string header{fmt::format("Handle {}:{} — resource '{}', object {}",
      handle.index,
      handle.generation,
      runtime.sprite_resource_name(instance->resource_index),
      instance->object_index)};
  ImGui::TextUnformatted(header.c_str());

  // --- Visibility diagnostics ---
  if (!pool.attached(handle)) {
    ImGui::TextColored(K_WARNING_COLOR, "Detached from the render list.");
  }
  if (instance->frame_index == Sprite::SpriteInstance::k_invalid_frame) {
    ImGui::TextColored(K_WARNING_COLOR, "No valid frame selected (0xFFFF).");
  }
  if (scene != nullptr) {
    for (const auto& [skip_handle, reason] : scene->sprite_queue_stats().skipped) {
      if (skip_handle == handle) {
        ImGui::TextColored(K_WARNING_COLOR, "Not drawn: %s", Sprite::skip_reason_name(reason));
      }
    }
  }

  // --- Lifecycle ---
  if (!pool.attached(handle) && ImGui::Button("Attach")) {
    if (auto result{runtime.attach_sprite(handle)}; !result) {
      App::Log::error(LogCategory::Debug, "Attach failed: {}", result.error());
    }
  }
  ImGui::SameLine();
  if (pool.attached(handle) && ImGui::Button("Detach")) {
    if (auto result{runtime.detach_sprite(handle)}; !result) {
      App::Log::error(LogCategory::Debug, "Detach failed: {}", result.error());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Destroy")) {
    if (auto result{runtime.destroy_sprite(handle)}; !result) {
      App::Log::error(LogCategory::Debug, "Destroy failed: {}", result.error());
    }
  }

  // --- Frame selection ---
  const Sprite::SpriteResource* resource{runtime.sprite_resource(instance->resource_index)};
  const std::size_t frame_count{resource == nullptr
                                    ? std::size_t{0}
                                    : resource->frame_count(instance->object_index)};
  ImGui::Separator();
  ImGui::Text("Frame: %u / %zu", instance->frame_index, frame_count);
  const auto set_frame = [&](const std::uint16_t frame) {
    if (auto result{runtime.set_sprite_frame(handle, frame)}; !result) {
      App::Log::error(LogCategory::Debug, "{}", result.error());
    }
  };
  const auto advance_frame = [&](const int step) {
    if (frame_count == 0) {
      return;
    }
    const int current{instance->frame_index == Sprite::SpriteInstance::k_invalid_frame
                          ? 0
                          : static_cast<int>(instance->frame_index)};
    const int wrapped{(current + step + static_cast<int>(frame_count)) %
                      static_cast<int>(frame_count)};
    set_frame(static_cast<std::uint16_t>(wrapped));
  };
  if (ImGui::Button("Prev") && frame_count > 0) {
    advance_frame(-1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Next") && frame_count > 0) {
    advance_frame(1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Invalidate") && frame_count > 0) {
    set_frame(static_cast<std::uint16_t>(frame_count));  // Fails → 0xFFFF state.
  }
  ImGui::Checkbox("Play frames", &m_sprite_play_frames);
  ImGui::SameLine();
  ImGui::SliderFloat("rate (fps)", &m_sprite_play_rate, 0.5F, 60.0F);
  if (m_sprite_play_frames && frame_count > 0) {
    m_sprite_play_accumulator += delta_time;
    const float interval{1.0F / m_sprite_play_rate};
    if (m_sprite_play_accumulator >= interval) {
      m_sprite_play_accumulator -= interval;
      advance_frame(1);
    }
  }

  // --- Transforms and appearance ---
  std::array<float, 3> position{instance->position};
  if (ImGui::DragFloat3("Position", position.data(), 0.1F)) {
    runtime.set_sprite_position(handle, position);
  }
  float scale_x{instance->scale_x};
  float scale_y{instance->scale_y};
  if (ImGui::DragFloat("Scale X", &scale_x, 0.05F)) {
    runtime.set_sprite_scale(handle, scale_x, scale_y);
  }
  if (ImGui::DragFloat("Scale Y", &scale_y, 0.05F)) {
    runtime.set_sprite_scale(handle, scale_x, scale_y);
  }
  constexpr float k_rad_to_deg{180.0F / std::numbers::pi_v<float>};
  float rotation_degrees{instance->rotation * k_rad_to_deg};
  if (ImGui::DragFloat("Rotation (degrees)", &rotation_degrees, 1.0F)) {
    runtime.set_sprite_rotation(handle, rotation_degrees / k_rad_to_deg);
  }
  std::array<float, 3> tint{instance->tint};
  if (ImGui::ColorEdit3("Tint", tint.data())) {
    runtime.set_sprite_tint(handle, tint);
  }
  float offset_u{instance->texture_offset_u};
  float offset_v{instance->texture_offset_v};
  if (ImGui::DragFloat("UV offset U", &offset_u, 0.01F)) {
    runtime.set_sprite_texture_offset(handle, offset_u, offset_v);
  }
  if (ImGui::DragFloat("UV offset V", &offset_v, 0.01F)) {
    runtime.set_sprite_texture_offset(handle, offset_u, offset_v);
  }
  float unknown_24{instance->unknown_24};
  if (ImGui::DragFloat("Unknown +0x24 (provisional)", &unknown_24, 0.05F)) {
    runtime.set_sprite_unknown_24(handle, unknown_24);
  }
  int mode{static_cast<int>(instance->render_mode)};
  if (ImGui::Combo("Render mode",
          &mode,
          "Default\0Cutout\0Alpha\0Alpha+Cutout\0Additive\0Additive+Cutout\0Darken\0Darken+"
          "Cutout\0AlternateCutout\0\0")) {
    runtime.set_sprite_render_mode(handle, static_cast<Sprite::SpriteRenderMode>(mode));
  }
  ImGui::Text("Effective: blend %s, depth write %s, cutout %s, fogged %s",
      Sprite::render_state(instance->render_mode).blend_enabled ? "on" : "off",
      Sprite::render_state(instance->render_mode).depth_write ? "on" : "off",
      Sprite::render_state(instance->render_mode).cutout ? "on" : "off",
      Sprite::render_state(instance->render_mode).fogged ? "on" : "off");

  if (ImGui::Button("Reset to Runtime defaults")) {
    runtime.reset_sprite_to_defaults(handle);
  }
  if (scene != nullptr) {
    ImGui::SameLine();
    if (ImGui::Button("Move to camera focus")) {
      scene->place_sprite_at_camera_focus(handle);
    }
    bool grayscale{scene->sprite_grayscale()};
    if (ImGui::Checkbox("Grayscale (3D scene)", &grayscale)) {
      scene->set_sprite_grayscale(grayscale);
    }
  }
}

void DebugUI::show_sprite_frames_tab(ScenarioRuntime& runtime) {
  const Sprite::SpriteInstance* instance{runtime.sprite_pool().find(m_sprite_selected_handle)};
  if (instance == nullptr) {
    ImGui::TextUnformatted("Select an instance first (Instances tab).");
    return;
  }
  const Sprite::SpriteResource* resource{runtime.sprite_resource(instance->resource_index)};
  if (resource == nullptr) {
    ImGui::TextUnformatted("The resource is not decoded yet; spawn the instance first.");
    return;
  }

  const auto resolved{resource->resolve_frame(instance->object_index,
      instance->frame_index,
      instance->texture_offset_u,
      instance->texture_offset_v)};
  if (!resolved.has_value()) {
    ImGui::TextColored(K_WARNING_COLOR, "Resolution error: %s", resolved.error().message.c_str());
    return;
  }
  const Sprite::SpriteFrame& frame{*resolved};

  ImGui::Text("Object %zu — frame %u of %zu",
      instance->object_index,
      instance->frame_index,
      resource->frame_count(instance->object_index));
  ImGui::Separator();
  ImGui::Text("Points: (%.3f, %.3f) -> (%.3f, %.3f)",
      static_cast<double>(frame.point0.at(0)),
      static_cast<double>(frame.point0.at(1)),
      static_cast<double>(frame.point1.at(0)),
      static_cast<double>(frame.point1.at(1)));
  ImGui::Text("Dimensions: %.3f x %.3f world units",
      static_cast<double>(frame.width),
      static_cast<double>(frame.height));
  ImGui::Text("Texture index: %d", frame.texture_index);
  ImGui::Text("UV0: %.4f, %.4f  |  UV1: %.4f, %.4f  (byte / 256 + offsets)",
      static_cast<double>(frame.uv0.at(0)),
      static_cast<double>(frame.uv0.at(1)),
      static_cast<double>(frame.uv1.at(0)),
      static_cast<double>(frame.uv1.at(1)));

  const std::vector<Omikron::Rectangle>& rectangles{
      resource->model.polygons.at(instance->object_index).rectangles};
  if (static_cast<std::size_t>(instance->frame_index) < rectangles.size()) {
    const Omikron::Rectangle& rectangle{rectangles.at(instance->frame_index)};
    ImGui::Separator();
    ImGui::Text("Raw descriptor: points %u, %u (slots +0x00/+0x04)",
        rectangle.vertices.at(0),
        rectangle.vertices.at(2));
    ImGui::Text("Raw UV bytes: (%u, %u) (%u, %u)  [bytes 2-3 and 6-7 ignored]",
        rectangle.uv.at(0),
        rectangle.uv.at(1),
        rectangle.uv.at(4),
        rectangle.uv.at(5));
  }

  const Texture2D* texture{runtime.sprite_texture(instance->resource_index,
      static_cast<std::size_t>(frame.texture_index))};
  if (texture != nullptr) {
    ImGui::Separator();
    ImGui::Text("Texture: %d x %d", texture->width(), texture->height());
    // ImGui takes the GL texture id as an opaque void*; flip V for GL's origin.
    // NOLINTNEXTLINE(performance-no-int-to-ptr, cppcoreguidelines-pro-type-reinterpret-cast)
    ImGui::Image(reinterpret_cast<void*>(static_cast<std::intptr_t>(texture->id())),
        ImVec2(128.0F, 128.0F),
        ImVec2(0.0F, 1.0F),
        ImVec2(1.0F, 0.0F));
  }
}

void DebugUI::show_sprite_queue_tab(ModelViewerScene& scene) {  const Sprite::SpriteQueueStats& stats{scene.sprite_queue_stats()};
  ImGui::Text("Attached %lu | visible %lu | drawn %lu | culled %lu | invalid %lu",
      static_cast<unsigned long>(stats.attached),
      static_cast<unsigned long>(stats.visible),
      static_cast<unsigned long>(stats.drawn),
      static_cast<unsigned long>(stats.culled),
      static_cast<unsigned long>(stats.invalid));
  ImGui::Text("Batches %lu | draw calls %lu",
      static_cast<unsigned long>(stats.batches),
      static_cast<unsigned long>(stats.draw_calls));
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Draw commands", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("%-4s %-4s %-6s %-16s", "Res", "Mat", "Verts", "Mode");
    for (const Sprite::SpriteDrawCommand& command : scene.sprite_commands()) {
      ImGui::Text("%-4lu %-4d %-6u %-16s",
          static_cast<unsigned long>(command.resource_index),
          command.material_index,
          command.vertex_count,
          render_mode_name(command.pipeline_key.render_mode));
    }
  }

  if (ImGui::CollapsingHeader("Skipped sprites")) {
    for (const auto& [handle, reason] : stats.skipped) {
      ImGui::Text("%u:%u — %s", handle.index, handle.generation, Sprite::skip_reason_name(reason));
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Script debugger
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Human-readable run-state label for the script debugger.
const char* script_run_state_name(const Script::ScriptRunState state) {
  switch (state) {
    case Script::ScriptRunState::k_running:            return "Running";
    case Script::ScriptRunState::k_user_paused:        return "User-paused";
    case Script::ScriptRunState::k_paused_on_unhandled: return "Paused on unhandled opcode";
    case Script::ScriptRunState::k_paused_on_error:    return "Paused on error";
    case Script::ScriptRunState::k_completed:          return "Completed";
    default:                                           return "Unknown";
  }
}

/// Human-readable label of one semantic parameter type.
const char* semantic_label(const std::uint16_t type) {
  switch (type) {
    case Script::k_semantic_sprite:            return "sprite";
    case Script::k_semantic_unknown_7:         return "unknown7";
    case Script::k_semantic_unknown_8:         return "unknown8";
    case Script::k_semantic_xyz_pointer:       return "xyz";
    case Script::k_semantic_duration:          return "duration";
    case Script::k_semantic_progress_elapsed:  return "elapsed";
    case Script::k_semantic_initial_scale:     return "initial";
    case Script::k_semantic_target_scale:      return "target";
    case Script::k_semantic_initial_roll:      return "initial roll";
    case Script::k_semantic_target_roll:       return "target roll";
    case Script::k_semantic_frame:             return "frame";
    default:                                   return nullptr;
  }
}

/// Copies one raw argument word to the clipboard in every interpretation.
std::string argument_text(const std::uint32_t raw) {
  const App::Omikron::ScriptValue value{.raw = raw};
  return fmt::format("raw {:#010x}  signed {}  unsigned {}  float {:.6g}",
      raw,
      value.as_signed(),
      value.as_unsigned(),
      static_cast<double>(value.as_float()));
}

}  // namespace

void DebugUI::show_script_command(Script::ScriptInstance& instance,
    Script::RuntimeScriptCommand& command,
    const std::size_t command_index,
    const bool is_root) {
  const Script::OpcodeInfo* info{Script::opcode_info(command.opcode)};
  const char* name{info == nullptr ? nullptr : info->name.data()};
  ImGui::Text("%s%s: %s (%#010x)", is_root ? "root " : "linked ",
      fmt::format("{}", command_index).c_str(),
      name == nullptr ? "unknown" : name,
      command.opcode);
  ImGui::Indent();
  ImGui::Text("args[%u..%u) next %d limit %#x count %u offset %#lx",
      command.first_value_index,
      command.first_value_index + command.value_count,
      command.next_linked_command_index.has_value()
          ? static_cast<int>(command.next_linked_command_index.value())
          : -1,
      command.execution_limit,
      command.execution_count,
      static_cast<unsigned long>(command.source_file_offset));
  for (std::uint32_t arg{0}; arg < command.value_count; ++arg) {
    const std::size_t pool_index{command.first_value_index + arg};
    if (pool_index >= instance.value_pool.size()) {
      ImGui::Text("  arg %u: <out of pool>", arg);
      continue;
    }
    const App::Omikron::ScriptValue& value{instance.value_pool.at(pool_index)};
    const char* label{nullptr};
    if (info != nullptr) {
      for (std::size_t param{0}; param < info->semantic_param_count; ++param) {
        if (info->semantic_params[param].argument_index == arg) {
          label = semantic_label(info->semantic_params[param].semantic_type);
          break;
        }
      }
    }
    ImGui::Text("  arg %u%s: raw %#010x signed %d unsigned %u float %.6g",
        arg,
        label == nullptr ? "" : fmt::format(" ({})", label).c_str(),
        value.raw,
        value.as_signed(),
        value.as_unsigned(),
        static_cast<double>(value.as_float()));
  }
  ImGui::Unindent();
}

void DebugUI::show_script_debugger() {
  ImGui::Begin("Script Debugger", &m_show_script_debugger);

  ScenarioRuntime* scenario_runtime{m_context.scenario_manager == nullptr
                                       ? nullptr
                                       : m_context.scenario_manager->gameplay_runtime()};
  if (scenario_runtime == nullptr) {
    ImGui::TextUnformatted("Scenario runtime not available.");
    ImGui::End();
    return;
  }
  Script::ScriptRuntime* runtime{scenario_runtime->script_runtime()};
  if (runtime == nullptr) {
    ImGui::TextUnformatted("Script runtime not initialised.");
    ImGui::End();
    return;
  }

  // --- Scenario overview ---
  ImGui::SeparatorText("Scenario");
  ImGui::Text("Path: %s", fmt::format("{}", scenario_runtime->script_scenario_name()).c_str());
  ImGui::Text("State: %s", script_run_state_name(runtime->run_state()));
  ImGui::Text("Tick: %llu", static_cast<unsigned long long>(runtime->tick_count()));
  ImGui::Text("Scripts: %lu, shared values: %lu",
      static_cast<unsigned long>(runtime->scx().scripts.size()),
      static_cast<unsigned long>(runtime->scx().shared_values.size()));
  ImGui::Text("Real delta:   %.6f s", static_cast<double>(runtime->last_real_delta_seconds()));
  ImGui::Text("Script delta: %.6f frames (30 Hz)%s",
      static_cast<double>(runtime->last_script_delta_frames()),
      runtime->last_script_delta_clamped() ? ", clamped to 3" : "");

  // --- Runtime controls ---
  ImGui::SeparatorText("Controls");
  const bool paused{runtime->run_state() != Script::ScriptRunState::k_running};
  if (ImGui::Button(paused ? "Resume" : "Pause")) {
    runtime->set_user_paused(!paused);
  }
  ImGui::SameLine();
  if (ImGui::Button("Step tick")) {
    runtime->step_tick(m_script_fixed_delta);
  }
  ImGui::SameLine();
  if (ImGui::Button("Step command")) {
    runtime->step_command();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset instances")) {
    runtime->reset_all();
  }
  ImGui::SliderFloat("Fixed delta (script frames)", &m_script_fixed_delta, 0.01F, 10.0F, "%.3f");
  ImGui::TextDisabled("Manual stepping uses 30 Hz script-frame units (1.0 = one frame).");

  bool trace{runtime->trace_enabled()};
  if (ImGui::Checkbox("Command trace", &trace)) {
    runtime->set_trace_enabled(trace);
  }

  // Debug-only manual activation, clearly marked as an override.
  ImGui::SeparatorText("Manual activation (debug override)");
  ImGui::TextDisabled("Not used by the normal startup path.");
  const auto& scripts{runtime->scx().scripts};
  if (ImGui::BeginCombo("Source script",
          !m_script_selected_source.has_value()
              ? "(none)"
              : fmt::format("{}: {}",
                    m_script_selected_source.value(),
                    scripts.at(m_script_selected_source.value()).name)
                    .c_str())) {
    for (std::size_t index{0}; index < scripts.size(); ++index) {
      const bool selected{m_script_selected_source == index};
      if (ImGui::Selectable(
              fmt::format("{}: {}", index, scripts.at(index).name).c_str(), selected)) {
        m_script_selected_source = index;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Activate")) {
    if (m_script_selected_source.has_value()) {
      if (auto created{scenario_runtime->spawn_script_instance(m_script_selected_source.value())};
          created) {
        App::Log::warn(LogCategory::Debug,
            "manual debug activation of script {} (override)",
            m_script_selected_source.value());
      } else {
        App::Log::error(LogCategory::Debug, "manual activation failed: {}", created.error());
      }
    }
  }

  // --- Pause display ---
  if (runtime->run_state() == Script::ScriptRunState::k_paused_on_unhandled ||
      runtime->run_state() == Script::ScriptRunState::k_paused_on_error) {
    const Script::ScriptPauseInfo& info{runtime->pause_info()};
    ImGui::SeparatorText("Pause");
    ImGui::TextColored(K_WARNING_COLOR, "Reason: %s", Script::pause_reason_name(info.reason));
    ImGui::TextWrapped("%s", info.reason_text.c_str());
    ImGui::Text("Script %lu '%s' instance %lu group %lu chain %lu",
        static_cast<unsigned long>(info.script_index),
        info.script_name.c_str(),
        static_cast<unsigned long>(info.instance_id),
        static_cast<unsigned long>(info.current_group_index),
        static_cast<unsigned long>(info.chain_position));
    ImGui::Text("Command %s %lu, opcode %#010x (%s), file offset %#lx",
        info.is_root_command ? "root" : "linked",
        static_cast<unsigned long>(info.command_index),
        info.opcode,
        info.opcode_name.c_str(),
        static_cast<unsigned long>(info.file_offset));
    ImGui::Text("args %u, limit %#x, count %u, next %d",
        info.value_count,
        info.execution_limit,
        info.execution_count,
        info.next_command_index);
    for (std::size_t arg{0}; arg < info.arguments.size(); ++arg) {
      const Script::ScriptArgumentView& view{info.arguments.at(arg)};
      ImGui::Text("  arg %lu: raw %#010x signed %d unsigned %u float %.6g",
          static_cast<unsigned long>(arg),
          view.raw,
          view.as_signed,
          view.as_unsigned,
          static_cast<double>(view.as_float));
    }
    if (ImGui::Button("Copy pause diagnostics")) {
      std::string text{fmt::format(
          "pause: {}\nscript {} '{}' instance {} group {} chain {} command {} {}\nopcode "
          "{:#010x} ({}) file {:#x}\nargs {} limit {:#x} count {} next {}\n",
          Script::pause_reason_name(info.reason),
          info.script_index,
          info.script_name,
          info.instance_id,
          info.current_group_index,
          info.chain_position,
          info.is_root_command ? "root" : "linked",
          info.command_index,
          info.opcode,
          info.opcode_name,
          info.file_offset,
          info.value_count,
          info.execution_limit,
          info.execution_count,
          info.next_command_index)};
      for (std::size_t arg{0}; arg < info.arguments.size(); ++arg) {
        text += fmt::format("  arg {}: {}\n", arg, argument_text(info.arguments.at(arg).raw));
      }
      ImGui::SetClipboardText(text.c_str());
    }
  }

  // --- Instance list ---
  ImGui::SeparatorText("Instances");
  const auto& instances{runtime->instances()};
  for (std::size_t index{0}; index < instances.size(); ++index) {
    const Script::ScriptInstance& instance{instances.at(index)};
    const std::string label{fmt::format("{}: '{}' (group {}/{}){}{}",
        instance.instance_id,
        instance.script_name,
        instance.current_group_index,
        instance.root_commands.size(),
        instance.completed ? " [completed]" : "",
        instance.paused ? " [paused]" : "")};
    if (ImGui::Selectable(label.c_str(), m_script_selected_instance == instance.instance_id)) {
      m_script_selected_instance = instance.instance_id;
    }
  }

  // --- Command/group inspector ---
  Script::ScriptInstance* selected{nullptr};
  for (Script::ScriptInstance& candidate : runtime->instances()) {
    if (candidate.instance_id == m_script_selected_instance) {
      selected = &candidate;
      break;
    }
  }
  if (selected != nullptr) {
    ImGui::SeparatorText("Selected instance");
    ImGui::Text("Source script %lu, field34 %d, sprite remaps %lu",
        static_cast<unsigned long>(selected->source_script_index),
        selected->execution_context_field_34,
        static_cast<unsigned long>(selected->sprite_remap.size()));
    for (const auto& [source, handle] : selected->sprite_remap) {
      ImGui::Text("  source sprite %u -> runtime %u:%u", source, handle.index, handle.generation);
    }
    if (ImGui::Button("Reset this instance")) {
      if (auto result{runtime->reset_instance(selected->instance_id)}; !result) {
        App::Log::warn(LogCategory::Debug, "reset failed: {}", result.error());
      }
    }

    if (ImGui::CollapsingHeader("Groups and commands")) {
      for (std::size_t group{0}; group < selected->root_commands.size(); ++group) {
        const bool is_current{group == selected->current_group_index};
        ImGui::Text("%sgroup %lu", is_current ? "> " : "  ", static_cast<unsigned long>(group));
        ImGui::Indent();
        show_script_command(*selected, selected->root_commands.at(group), group, true);
        std::optional<std::uint32_t> next{
            selected->root_commands.at(group).next_linked_command_index};
        while (next.has_value()) {
          if (*next >= selected->linked_commands.size()) {
            ImGui::Text("  <invalid next %u>", *next);
            break;
          }
          show_script_command(
              *selected, selected->linked_commands.at(*next), *next, false);
          next = selected->linked_commands.at(*next).next_linked_command_index;
        }
        ImGui::Unindent();
      }
    }
  }

  // --- Trace ---
  if (runtime->trace_enabled()) {
    ImGui::SeparatorText("Trace");
    if (ImGui::BeginChild("##ScriptTrace", ImVec2(0.0F, 180.0F), ImGuiChildFlags_Borders)) {
      for (const Script::CommandTraceEntry& entry : runtime->trace()) {
        ImGui::Text("tick %llu inst %lu group %lu chain %lu %s %s -> %s (count %u -> %u)",
            static_cast<unsigned long long>(entry.tick),
            static_cast<unsigned long>(entry.instance_id),
            static_cast<unsigned long>(entry.group_index),
            static_cast<unsigned long>(entry.chain_position),
            entry.opcode_name.c_str(),
            entry.status_before.c_str(),
            entry.status_after.c_str(),
            entry.execution_count_before,
            entry.execution_count_after);
        if (!entry.mutated_arguments.empty()) {
          ImGui::TextDisabled("    mutated: %s", entry.mutated_arguments.c_str());
        }
      }
    }
    ImGui::EndChild();
  }

  ImGui::End();
}

void DebugUI::show_scenarios() {
  ImGui::Begin("Scenarios", &m_show_scenarios);

  ScenarioManager* manager{m_context.scenario_manager};
  if (manager == nullptr) {
    ImGui::TextUnformatted("Scenario manager not available.");
    ImGui::End();
    return;
  }

  // --- Gameplay-mode slot ---
  ImGui::SeparatorText("Gameplay-mode slot");
  ImGui::Text("Mode: %s",
      fmt::format("{}", App::gameplay_mode_name(manager->current_gameplay_mode())).c_str());
  {
    const Omikron::ScxData* scx{manager->gameplay_mode_scx()};
    ImGui::Text("Path: %s",
        scx == nullptr
            ? "(not loaded)"
            : fmt::format("{}", App::gameplay_mode_scenario_path(manager->current_gameplay_mode()))
                  .c_str());
    ImGui::Text("Loaded: %s", scx == nullptr ? "no" : "yes");
    if (scx != nullptr) {
      ImGui::Text(
          "Scripts: %lu, active: %lu, sounds: %lu, sprites: %lu, models: %lu, shared values: %lu",
          static_cast<unsigned long>(scx->scripts.size()),
          static_cast<unsigned long>(manager->active_script_instances_total()),
          static_cast<unsigned long>(scx->sounds.size()),
          static_cast<unsigned long>(scx->sprites.size()),
          static_cast<unsigned long>(scx->models.size()),
          static_cast<unsigned long>(scx->shared_values.size()));
      if (ImGui::CollapsingHeader("Script templates##gameplay")) {
        for (std::size_t index{0}; index < scx->scripts.size(); ++index) {
          const Omikron::ScxScript& script{scx->scripts.at(index)};
          ImGui::Text("mode:%lu '%s' id %u — inactive",
              static_cast<unsigned long>(index),
              script.name.c_str(),
              script.script_id);
        }
      }
    }
  }

  ImGui::SeparatorText("World contexts");
  const auto contexts{manager->world_contexts()};
  for (std::size_t index{0}; index < contexts.size(); ++index) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const WorldSceneContext& context{contexts[index]};
    const std::string header{fmt::format("Context {} (scene {})", index, context.scene_id)};
    if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Text("Residency: %s", residency_name(context.residency));
      ImGui::Text("Cache index: %lu, generation: %u",
          static_cast<unsigned long>(index),
          context.generation);
      const char* decor{context.decor_path.has_value() ? context.decor_path->c_str() : "(not associated yet)"};
      ImGui::Text("Decor: %s", decor);
      if (!context.resolved_decor_path.empty()) {
        ImGui::Text("Resolved decor: %s", context.resolved_decor_path.c_str());
      }
      ImGui::Text("Scenario: %s", context.scenario_path.empty() ? "(none)" : context.scenario_path.c_str());
      if (!context.resolved_scenario_path.empty()) {
        ImGui::Text("Resolved scenario: %s", context.resolved_scenario_path.c_str());
      }
      ImGui::Text("File size: %lu bytes", static_cast<unsigned long>(context.file_size_bytes));
      if (context.scx_data) {
        ImGui::Text(
            "Version: %u, scripts: %lu, sounds: %lu, sprites: %lu, models: %lu, shared values: %lu",
            context.scx_data->header.version,
            static_cast<unsigned long>(context.scx_data->scripts.size()),
            static_cast<unsigned long>(context.scx_data->sounds.size()),
            static_cast<unsigned long>(context.scx_data->sprites.size()),
            static_cast<unsigned long>(context.scx_data->models.size()),
            static_cast<unsigned long>(context.scx_data->shared_values.size()));
        if (ImGui::CollapsingHeader(
                fmt::format("Script templates##world{}", index).c_str())) {
          for (std::size_t script_index{0}; script_index < context.scx_data->scripts.size(); ++script_index) {
            const Omikron::ScxScript& script{context.scx_data->scripts.at(script_index)};
            ImGui::Text("world:%lu '%s' id %u — inactive",
                static_cast<unsigned long>(script_index),
                script.name.c_str(),
                script.script_id);
          }
        }
      }
      if (!context.last_error.empty()) {
        ImGui::TextColored(K_WARNING_COLOR, "Last error: %s", context.last_error.c_str());
      }

      const std::uint32_t scene_id{context.scene_id};
      if (context.residency == WorldSceneResidencyState::LoadedInactive) {
        if (ImGui::Button(fmt::format("Activate##{}", index).c_str())) {
          if (auto result{manager->activate_world_context(scene_id)}; !result) {
            App::Log::error(LogCategory::Debug, "activate context {} failed: {}", scene_id, result.error());
          }
        }
      }
      if (context.residency == WorldSceneResidencyState::LoadedActive) {
        if (ImGui::Button(fmt::format("Deactivate##{}", index).c_str())) {
          if (auto result{manager->deactivate_world_context(scene_id)}; !result) {
            App::Log::error(LogCategory::Debug, "deactivate context {} failed: {}", scene_id, result.error());
          }
        }
      }
      if (context.residency != WorldSceneResidencyState::Free) {
        ImGui::SameLine();
        if (ImGui::Button(fmt::format("Unload##{}", index).c_str())) {
          if (context.residency == WorldSceneResidencyState::LoadedActive) {
            App::Log::warn(LogCategory::Debug, "deactivate context {} before unloading", scene_id);
          } else if (auto result{manager->unload_world_context(scene_id)}; !result) {
            App::Log::error(LogCategory::Debug, "unload context {} failed: {}", scene_id, result.error());
          }
        }
      }
    }
  }

  ImGui::SeparatorText("Development controls");
  ImGui::TextDisabled("Mode switches replace only the gameplay-mode slot.");
  if (ImGui::Button("Switch to FirstPersonShooting (shoot2.scx)")) {
    if (auto result{manager->set_gameplay_mode(GameplayMode::FirstPersonShooting)}; !result) {
      App::Log::error(LogCategory::Debug, "switch to FirstPersonShooting failed: {}", result.error());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Switch to HandToHandCombat (fight.scx)")) {
    if (auto result{manager->set_gameplay_mode(GameplayMode::HandToHandCombat)}; !result) {
      App::Log::error(LogCategory::Debug, "switch to HandToHandCombat failed: {}", result.error());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Switch to Adventure (aventure.scx)")) {
    if (auto result{manager->set_gameplay_mode(GameplayMode::Adventure)}; !result) {
      App::Log::error(LogCategory::Debug, "switch to Adventure failed: {}", result.error());
    }
  }

  ImGui::End();
}

void DebugUI::show_audio_inspector() {
  ImGui::Begin("Audio Inspector", &m_show_audio_inspector);

  Audio::AudioSystem* audio{m_context.audio_system};
  if (audio == nullptr) {
    ImGui::TextUnformatted("Audio system not available.");
    ImGui::End();
    return;
  }

  const Audio::AudioDebugSnapshot& snapshot{audio->debug_snapshot()};

  // --- Device / subsystem ---
  ImGui::SeparatorText("Device / subsystem");
  const char* state{"not initialized"};
  if (snapshot.unavailable) {
    state = "unavailable";
  } else if (snapshot.initialized) {
    state = "initialized";
  }
  ImGui::Text("State: %s", state);
  if (!snapshot.state_note.empty()) {
    ImGui::TextColored(K_WARNING_COLOR, "%s", snapshot.state_note.c_str());
  }
  ImGui::Text("SDL3_mixer: %s", snapshot.mixer_version.c_str());
  ImGui::Text("Device: %s", snapshot.device_name.c_str());
  ImGui::Text("Requested: %s", snapshot.requested_format.c_str());
  ImGui::Text("Negotiated: %s", snapshot.negotiated_format.c_str());

  float master{audio->master_gain()};
  float sfx{audio->sfx_gain()};
  float music{audio->music_gain()};
  if (ImGui::SliderFloat("Master gain", &master, 0.0F, 2.0F)) {
    audio->set_master_gain(master);
  }
  if (ImGui::SliderFloat("SFX gain", &sfx, 0.0F, 2.0F)) {
    audio->set_sfx_gain(sfx);
  }
  if (ImGui::SliderFloat("Music gain", &music, 0.0F, 2.0F)) {
    audio->set_music_gain(music);
  }
  ImGui::Text("Voices: %lu active / %lu free / 16 total",
      static_cast<unsigned long>(snapshot.active_voices),
      static_cast<unsigned long>(snapshot.free_voices));
  ImGui::Text("Cache: %lu / %lu resources",
      static_cast<unsigned long>(snapshot.cached_resources),
      static_cast<unsigned long>(snapshot.cache_capacity));
  if (ImGui::Button("Stop all SFX")) {
    audio->stop_all_sfx();
  }
  ImGui::SameLine();
  if (ImGui::Button("Stop music")) {
    audio->music().stop(0);
  }

  // --- Listener ---
  ImGui::SeparatorText("Listener");
  ImGui::Text("Position: (%.2f, %.2f, %.2f)",
      static_cast<double>(snapshot.listener_position.at(0)),
      static_cast<double>(snapshot.listener_position.at(1)),
      static_cast<double>(snapshot.listener_position.at(2)));
  ImGui::Text("Velocity: (%.2f, %.2f, %.2f)",
      static_cast<double>(snapshot.listener_velocity.at(0)),
      static_cast<double>(snapshot.listener_velocity.at(1)),
      static_cast<double>(snapshot.listener_velocity.at(2)));
  ImGui::Text("Forward: (%.2f, %.2f, %.2f)",
      static_cast<double>(snapshot.listener_forward.at(0)),
      static_cast<double>(snapshot.listener_forward.at(1)),
      static_cast<double>(snapshot.listener_forward.at(2)));
  ImGui::Text("Up: (%.2f, %.2f, %.2f)",
      static_cast<double>(snapshot.listener_up.at(0)),
      static_cast<double>(snapshot.listener_up.at(1)),
      static_cast<double>(snapshot.listener_up.at(2)));
  ImGui::Text("Right: (%.2f, %.2f, %.2f)",
      static_cast<double>(snapshot.listener_right.at(0)),
      static_cast<double>(snapshot.listener_right.at(1)),
      static_cast<double>(snapshot.listener_right.at(2)));
  ImGui::Text("Last audio update delta: %.6f s",
      static_cast<double>(snapshot.last_update_delta_seconds));
  if (snapshot.listener_degenerate) {
    ImGui::TextColored(K_WARNING_COLOR, "Degenerate listener transform (fallback basis used).");
  }

  // --- Resource cache ---
  ImGui::SeparatorText("Resource cache");
  if (ImGui::BeginChild("##AudioResources", ImVec2(0.0F, 140.0F), ImGuiChildFlags_Borders)) {
    for (const Audio::ResourceDebugInfo& resource : snapshot.resources) {
      ImGui::Text("%u: '%s' (%s) %s %d ch %d Hz %lld ms %lu bytes refs %lu",
          resource.resource.index,
          resource.name.c_str(),
          resource.scenario_name.c_str(),
          resource.loaded ? resource.format.c_str() : "FAILED",
          resource.channels,
          resource.frequency,
          static_cast<long long>(resource.duration_ms),
          static_cast<unsigned long>(resource.byte_size),
          static_cast<unsigned long>(resource.ref_count));
      if (!resource.load_error.empty()) {
        ImGui::TextDisabled("    error: %s", resource.load_error.c_str());
      }
      ImGui::SameLine();
      if (resource.loaded &&
          ImGui::SmallButton(fmt::format("Audition##{}", resource.resource.index).c_str())) {
        static_cast<void>(audio->audition(resource.resource));
      }
    }
  }
  ImGui::EndChild();

  // --- 16-slot voice table ---
  ImGui::SeparatorText("Voice slots");
  if (ImGui::BeginChild("##AudioVoices", ImVec2(0.0F, 220.0F), ImGuiChildFlags_Borders)) {
    for (const Audio::VoiceDebugInfo& voice : snapshot.voices) {
      const char* state_name{"Unknown"};
      switch (voice.state) {
        case Audio::VoiceState::k_free:     state_name = "Free";     break;
        case Audio::VoiceState::k_queued:   state_name = "Queued";   break;
        case Audio::VoiceState::k_playing:  state_name = "Playing";  break;
        case Audio::VoiceState::k_stopping: state_name = "Stopping"; break;
      }
      ImGui::Text("slot %u gen %u [%s] sound %u '%s' idx %u owner '%s'",
          voice.index,
          voice.generation,
          state_name,
          voice.resource.index,
          voice.sound_name.c_str(),
          voice.scenario_sound_index,
          voice.owner_description.c_str());
      if (voice.state != Audio::VoiceState::k_free) {
        ImGui::SameLine();
        if (ImGui::SmallButton(fmt::format("Stop##{}", voice.index).c_str())) {
          static_cast<void>(audio->stop_voice(
              Audio::VoiceHandle{.index = voice.index, .generation = voice.generation}));
        }
        ImGui::Indent();
        ImGui::Text("%s%s%s pos %.0f/%.0f ms dist %.1f prev %.1f gain %.2f pan %.2f l/r "
                    "%.2f/%.2f freq %.0f ratio %.3f",
            voice.looping ? "loop " : "once ",
            voice.nonspatial ? "nonspatial " : "spatial ",
            voice.unknown_flag ? "unknown-flag " : "",
            static_cast<double>(voice.playback_position_ms),
            static_cast<double>(voice.remaining_ms),
            static_cast<double>(voice.distance),
            static_cast<double>(voice.previous_distance),
            static_cast<double>(voice.attenuation_gain),
            static_cast<double>(voice.pan),
            static_cast<double>(voice.left_gain),
            static_cast<double>(voice.right_gain),
            static_cast<double>(voice.base_frequency_hz),
            static_cast<double>(voice.frequency_ratio));
        ImGui::Text("emitter pos (%.1f, %.1f, %.1f) vel (%.1f, %.1f, %.1f) min %.0f max %.0f",
            static_cast<double>(voice.emitter_position.at(0)),
            static_cast<double>(voice.emitter_position.at(1)),
            static_cast<double>(voice.emitter_position.at(2)),
            static_cast<double>(voice.emitter_velocity.at(0)),
            static_cast<double>(voice.emitter_velocity.at(1)),
            static_cast<double>(voice.emitter_velocity.at(2)),
            static_cast<double>(voice.minimum_distance),
            static_cast<double>(voice.maximum_distance));
        ImGui::Unindent();
      }
    }
  }
  ImGui::EndChild();

  // --- Music ---
  ImGui::SeparatorText("Music");
  const Audio::MusicDebugInfo& music_info{snapshot.music};
  ImGui::Text("Source: %s", music_info.source_name.empty() ? "(none)" : music_info.source_name.c_str());
  const char* music_state{"stopped"};
  if (music_info.playing) {
    music_state = music_info.paused ? "paused" : "playing";
  }
  ImGui::Text("State: %s%s", music_state, music_info.loop ? " (loop)" : "");
  ImGui::Text("Loop start: %lld ms", static_cast<long long>(music_info.loop_start_ms));
  ImGui::Text("Position: %lld / %lld ms",
      static_cast<long long>(music_info.playback_position_ms),
      static_cast<long long>(music_info.duration_ms));
  if (ImGui::Button("Pause")) {
    audio->music().pause();
  }
  ImGui::SameLine();
  if (ImGui::Button("Resume")) {
    audio->music().resume();
  }
  ImGui::TextDisabled("%s", music_info.status_note.c_str());

  // --- Event log ---
  ImGui::SeparatorText("Event log");
  if (ImGui::BeginChild("##AudioEvents", ImVec2(0.0F, 140.0F), ImGuiChildFlags_Borders)) {
    for (const Audio::AudioEvent& event : snapshot.events) {
      ImGui::Text("%s", event.message.c_str());
    }
  }
  ImGui::EndChild();

  ImGui::End();
}

void DebugUI::show_area_script() {
  ImGui::Begin("Area Script", &m_show_area_script);

  const ScenarioEngine* engine{m_context.scenario_engine};
  if (engine == nullptr) {
    ImGui::TextUnformatted("Scenario engine not available.");
    ImGui::End();
    return;
  }
  const Script::AreaScriptRuntime* script{engine->area_script()};
  if (script == nullptr) {
    ImGui::TextUnformatted("Area script not loaded.");
    ImGui::End();
    return;
  }

  const char* state_name{"Unknown"};
  switch (script->state()) {
    case Script::AreaScriptState::k_ready:             state_name = "Ready"; break;
    case Script::AreaScriptState::k_running:           state_name = "Running"; break;
    case Script::AreaScriptState::k_waiting:           state_name = "Waiting"; break;
    case Script::AreaScriptState::k_paused_unsupported: state_name = "Paused (unsupported opcode)"; break;
    case Script::AreaScriptState::k_completed:         state_name = "Completed"; break;
    case Script::AreaScriptState::k_failed:            state_name = "Failed"; break;
  }

  ImGui::Text("State: %s  active: %s  wait: %u",
      state_name,
      script->active() ? "yes" : "no",
      static_cast<unsigned int>(script->wait_state()));
  if (script->wait_info().interface.has_value()) {
    ImGui::Text("Wait interface: id=%u gen=%u",
        static_cast<unsigned int>(script->wait_info().interface->interface_id),
        script->wait_info().interface->generation);
  }
  ImGui::Text("Instruction pointer: %zu  executed: %zu",
      script->instruction_pointer(),
      script->executed_instruction_count());

  ImGui::SeparatorText("Variables");
  if (script->variables().empty()) {
    ImGui::TextUnformatted("(none)");
  } else {
    for (const auto& [id, value] : script->variables()) {
      ImGui::Text("%u = %d", static_cast<unsigned int>(id), value);
    }
  }

  if (script->state() == Script::AreaScriptState::k_paused_unsupported ||
      script->state() == Script::AreaScriptState::k_failed) {
    const Script::AreaPauseInfo& pause{script->pause_info()};
    ImGui::SeparatorText("Pause");
    ImGui::TextColored(K_WARNING_COLOR, "%s", pause.reason_text.c_str());
    ImGui::Text("offset %zu opcode %#010x (%s)",
        pause.offset,
        static_cast<unsigned int>(pause.opcode),
        pause.opcode_name.c_str());
    ImGui::Text("nearby: %s", pause.nearby_bytes.c_str());
  }

  ImGui::SeparatorText("Instruction trace");
  if (ImGui::BeginChild("##AreaTrace", ImVec2(0.0F, 240.0F), ImGuiChildFlags_Borders)) {
    for (const Script::AreaInstructionTrace& entry : script->trace()) {
      ImGui::Text("%zu %#010x %s",
          entry.offset,
          static_cast<unsigned int>(entry.opcode),
          entry.opcode_name.c_str());
      for (const std::int32_t operand : entry.operands) {
        ImGui::TextDisabled("    %d", operand);
      }
      if (!entry.effect.empty()) {
        ImGui::TextDisabled("    %s", entry.effect.c_str());
      }
    }
  }
  ImGui::EndChild();

  ImGui::End();
}

void DebugUI::show_startup() {
  ImGui::Begin("Startup / IAM", &m_show_startup);

  const ScenarioEngine* engine{m_context.scenario_engine};
  if (engine == nullptr) {
    ImGui::TextUnformatted("Scenario engine not available.");
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("IAM/START");
  ImGui::Text("Initial area: %d  linked area: %d",
      engine->initial_area_id(),
      engine->linked_area_id());

  ImGui::SeparatorText("Area mapping");
  const auto& mapping{engine->area_mapping_entries()};
  if (mapping.empty()) {
    ImGui::TextUnformatted("(empty)");
  } else {
    for (const auto& [area_id, linked] : mapping) {
      ImGui::Text("%d -> %d", area_id, linked);
    }
  }

  ImGui::SeparatorText("IAM/AREA record");
  const Omikron::IamAreaRecord* record{engine->area_record()};
  if (record == nullptr) {
    ImGui::TextUnformatted("(not loaded)");
  } else {
    ImGui::Text("Size: %zu bytes  script offset: %#x",
        record->record_size(),
        static_cast<unsigned int>(record->script_offset()));
    ImGui::Text("3DO: %s", record->model3do_name().c_str());
    ImGui::Text("SCX: %s", record->scenario_scx_name().c_str());
    ImGui::Text("MPT: %s", record->map_mpt_name().c_str());
    ImGui::Text("OPT: %s", record->options_opt_name().c_str());
    ImGui::Text("ANI: %s", record->animation_ani_name().c_str());
    ImGui::Text("Secondary 3DO: %s", record->secondary_3do_name().c_str());
    if (ImGui::CollapsingHeader("Tables")) {
      for (std::size_t index{0}; index < Omikron::IamAreaRecord::k_table_count; ++index) {
        const std::optional<std::size_t> stride{Omikron::IamAreaRecord::known_table_stride(index)};
        ImGui::Text("table %zu: offset %#x count %u stride %s",
            index,
            static_cast<unsigned int>(record->table_offset(index)),
            static_cast<unsigned int>(record->table_count(index)),
            stride.has_value() ? fmt::format("{}", *stride).c_str() : "unknown");
      }
    }
  }

  ImGui::SeparatorText("GRID dependencies");
  ImGui::Text("GRID.SCX: %s", engine->grid_scx_path().c_str());
  ImGui::Text("GRID.3DO: %s (%s)",
      engine->grid_3do_path().c_str(),
      engine->grid_3do_state().c_str());
  const Omikron::Model3DOData* grid{engine->grid_3do_model()};
  ImGui::Text("GRID.3DO parsed: %s", grid == nullptr ? "no" : "yes");
  if (grid != nullptr) {
    ImGui::Text("meshes %lu materials %lu",
        static_cast<unsigned long>(grid->meshes.size()),
        static_cast<unsigned long>(grid->materials.size()));
  }
  if (!engine->last_error().empty()) {
    ImGui::TextColored(K_WARNING_COLOR, "Last error: %s", engine->last_error().c_str());
  }
  ImGui::Text("Ticked: %s", engine->ticked() ? "yes" : "no");

  ImGui::End();
}

void DebugUI::show_interface() {
  ImGui::Begin("Interface", &m_show_interface);

  const ScenarioEngine* engine{m_context.scenario_engine};
  if (engine == nullptr) {
    ImGui::TextUnformatted("Scenario engine not available.");
    ImGui::End();
    return;
  }
  const InterfaceDispatcher& dispatcher{engine->dispatcher()};
  ImGui::Text("Main menu active: %s", engine->main_menu_active() ? "yes" : "no");
  ImGui::Text("Preliminary 29 active: %s", engine->preliminary_29_active() ? "yes" : "no");
  const InterfaceOpenRequest& request{dispatcher.last_request()};
  ImGui::Text("Last request: id %u b %d c %d",
      static_cast<unsigned int>(request.interface_id),
      request.operand_b,
      request.operand_c);

  const Interface::InterfaceManager* manager{m_context.interface_manager};
  if (manager == nullptr) {
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("I2D Inspector");
  {
    // Stepped = authentic 30 Hz updates only; interpolated (default) inserts
    // smooth presentation frames between endpoints.
    Interface::InterfaceManager* mutable_manager{m_context.interface_manager};
    bool interpolated{mutable_manager->background_interpolated()};
    if (ImGui::Checkbox("Interpolate background", &interpolated)) {
      mutable_manager->set_background_interpolated(interpolated);
    }
  }
  const Interface::InterfaceInstance* instance{manager->focused_instance()};
  if (instance == nullptr || instance->descriptor == nullptr) {
    ImGui::TextUnformatted("No active interface.");
    ImGui::End();
    return;
  }

  ImGui::Text("id %d  \"%s\"",
      instance->descriptor->id,
      fmt::format("{}", instance->descriptor->name).c_str());
  ImGui::Text("bitmap: %s", fmt::format("{}", instance->descriptor->bitmap_name).c_str());
  ImGui::Text("string table: %s (%zu entries)",
      fmt::format("{}", instance->descriptor->string_table_name).c_str(),
      instance->strings.size());
  ImGui::Text("selected element: %zu", instance->selected_element);

  const Interface::I2DState* state{instance->current_state};
  const char* state_name{"none"};
  if (state != nullptr) {
    state_name = state == instance->root_state ? "root" : "child";
  }
  ImGui::Text("state: %s", state_name);

  if (state != nullptr) {
    std::size_t selectable_ordinal{0};
    for (const Interface::I2DGroup& group : state->groups) {
      ImGui::Text("group flags 0x%08X", group.runtime_flags);
      for (const Interface::I2DElement& element : group.elements) {
        if (const auto* bitmap{std::get_if<Interface::I2DBitmapElement>(&element.data)}) {
          ImGui::Text("  bitmap src(%d,%d,%d,%d) dst(%d,%d,%d,%d) flags 0x%08X",
              bitmap->source.x,
              bitmap->source.y,
              bitmap->source.width,
              bitmap->source.height,
              bitmap->destination.x,
              bitmap->destination.y,
              bitmap->destination.width,
              bitmap->destination.height,
              bitmap->runtime_flags);
        } else if (const auto* text{std::get_if<Interface::I2DTextElement>(&element.data)}) {
          const bool selected{text->selectable() && selectable_ordinal == instance->selected_element};
          if (text->selectable()) {
            ++selectable_ordinal;
          }
          ImGui::Text("  text[%u] \"%s\" key '%c' rect(%d,%d,%d,%d) flags 0x%08X%s",
              text->string_index,
              fmt::format("{}", instance->strings.at(text->string_index)).c_str(),
              text->font_key,
              text->bounds.x,
              text->bounds.y,
              text->bounds.width,
              text->bounds.height,
              text->runtime_flags,
              selected ? "  <selected>" : "");
        }
      }
    }
  }

  ImGui::End();
}

void DebugUI::show_startup_trace() {
  ImGui::Begin("Startup Trace", &m_show_startup_trace);

  const Startup::StartupCoordinator* coordinator{m_context.startup_coordinator};
  if (coordinator != nullptr) {
    ImGui::Text("Phase: %s  finished: %s",
        fmt::format("{}", Startup::startup_phase_name(coordinator->current_phase())).c_str(),
        coordinator->finished() ? "yes" : "no");
    if (ImGui::CollapsingHeader("Ordered phases")) {
      for (const Startup::StartupPhase phase : Startup::StartupCoordinator::ordered_phases()) {
        const bool is_current{phase == coordinator->current_phase()};
        ImGui::Text("%s%s",
            is_current ? "> " : "  ",
            fmt::format("{}", Startup::startup_phase_name(phase)).c_str());
      }
    }
  }

  ImGui::SeparatorText("Events");
  ImGui::InputText("Filter", m_startup_trace_filter, sizeof(m_startup_trace_filter));
  const std::string filter{m_startup_trace_filter};
  if (ImGui::BeginChild("##StartupTraceEvents", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders)) {
    const Startup::StartupTraceRecorder* recorder{m_context.startup_trace};
    if (recorder == nullptr) {
      ImGui::TextUnformatted("Trace recorder not available.");
    } else {
      for (const Startup::StartupTraceEvent& event : recorder->events()) {
        if (!filter.empty() && !event.name.contains(filter)) {
          continue;
        }
        ImGui::Text("%u %s", event.sequence, event.name.c_str());
        if (!event.detail.empty()) {
          ImGui::TextDisabled("    %s", event.detail.c_str());
        }
      }
    }
  }
  ImGui::EndChild();

  ImGui::End();
}

}  // namespace App::Debug
