#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <glad/glad.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/DebugUI.hpp"
#include "Core/Debug/DebugUIInternal.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/LogFilter.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Startup/StartupCoordinator.hpp"
#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"

namespace App::Debug {

void DebugUI::show_performance(float /*delta_time*/) {
  // The performance readout is an overlay, not a window: undecorated,
  // auto-sized, non-focusable and pinned just below the menu bar. A
  // semi-transparent background keeps the scene visible behind it.
  constexpr ImGuiWindowFlags k_overlay_flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
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
    std::vector<std::pair<std::string, ScopeStats>> sorted(aggregated.begin(), aggregated.end());
    std::ranges::sort(sorted, [](const auto& left, const auto& right) {
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
    category_names.at(index + 1) = log_category_name(k_all_log_categories.at(index)).data();
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
  ImGui::BeginChild(
      "LogScrollRegion", ImVec2(0, -footer_height), 0, ImGuiWindowFlags_HorizontalScrollbar);

  if (auto sink = Log::get_debug_sink()) {
    const auto entries = sink->get_entries();
    const auto visible_entries{entries | std::views::filter([this](const auto& entry) {
      return m_log_filter.matches(entry.level, entry.category, entry.line);
    }) | std::ranges::to<std::vector>()};

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_entries.size()));
    while (clipper.Step()) {
      for (int line_idx = clipper.DisplayStart; line_idx < clipper.DisplayEnd; ++line_idx) {
        const auto& entry = visible_entries.at(static_cast<std::size_t>(line_idx));
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
    ImGui::Text("x: %d  y: %d  w: %d  h: %d", viewport[0], viewport[1], viewport[2], viewport[3]);
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    ImGui::Unindent();
  }

  if (ImGui::CollapsingHeader("Capabilities", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent();
    const auto show_gl_bool = [](const char* label, GLenum cap) {
      const bool enabled = glIsEnabled(cap) != 0U;
      const ImVec4 color =
          enabled ? ImVec4{0.2F, 1.0F, 0.2F, 1.0F} : ImVec4{1.0F, 0.3F, 0.3F, 1.0F};
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
    ImGui::ColorEdit4("##ClearColorEdit",
        clear_color.data(),
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
      case GL_NEVER:
        func_name = "Never";
        break;
      case GL_LESS:
        func_name = "Less";
        break;
      case GL_EQUAL:
        func_name = "Equal";
        break;
      case GL_LEQUAL:
        func_name = "LEqual";
        break;
      case GL_GREATER:
        func_name = "Greater";
        break;
      case GL_NOTEQUAL:
        func_name = "NotEqual";
        break;
      case GL_GEQUAL:
        func_name = "GEqual";
        break;
      case GL_ALWAYS:
        func_name = "Always";
        break;
      default:
        break;
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
// Startup Trace
// ─────────────────────────────────────────────────────────────────────────────

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
