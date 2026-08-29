#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <glad/glad.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/DebugUI.hpp"
#include "Core/Debug/DebugUIInternal.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/LogFilter.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Debug/RuntimeTimingDebug.hpp"
#include "Core/FrameTiming.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Startup/StartupCoordinator.hpp"
#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"

namespace App::Debug {

void DebugUI::show_frame_timing() {
  if (!ImGui::Begin("Frame & Timing", &m_show_frame_timing)) {
    ImGui::End();
    return;
  }

  RuntimeTimingDebugSource* const source{m_context.runtime_timing};
  if (source == nullptr) {
    ImGui::TextUnformatted("Runtime timing source not available.");
    ImGui::End();
    return;
  }
  const RuntimeTimingDebugSnapshot timing{source->timing_debug_snapshot()};

  // Sampling is intentionally panel-local: no history work occurs while the
  // inspector is closed, and the timed-frame sequence prevents duplicate
  // samples when startup-video overlays render without a new timed frame.
  if (timing.last_engine_callback.timed_frame_sequence != 0U &&
      timing.last_engine_callback.timed_frame_sequence != m_runtime_frame_time_last_sequence) {
    m_runtime_frame_time_history.at(m_runtime_frame_time_history_head) =
        static_cast<float>(timing.last_completed_frame_time_ms);
    m_runtime_frame_time_history_head =
        (m_runtime_frame_time_history_head + 1U) % m_runtime_frame_time_history.size();
    m_runtime_frame_time_history_count =
        std::min(m_runtime_frame_time_history_count + 1U, m_runtime_frame_time_history.size());
    m_runtime_frame_time_last_sequence = timing.last_engine_callback.timed_frame_sequence;
  }

  ImGui::SeparatorText("Presentation");
  ImGui::Text("Last completed frame: %llu ms  |  %.1f FPS",
      static_cast<unsigned long long>(timing.last_completed_frame_time_ms),
      static_cast<double>(timing.current_fps));
  ImGui::Text("Runtime moving average: %llu ms  |  %.1f FPS",
      static_cast<unsigned long long>(timing.moving_average_frame_time_ms),
      static_cast<double>(timing.average_fps));
  ImGui::Text(
      "Dear ImGui presentation estimate: %.1f FPS", static_cast<double>(ImGui::GetIO().Framerate));
  const auto& window_info{Metrics::get().window_info()};
  if (const auto refresh{window_info.find("Refresh Rate")}; refresh != window_info.end()) {
    ImGui::Text("Window refresh rate: %s", refresh->second.c_str());
  }
  if (const auto vsync{window_info.find("VSync")}; vsync != window_info.end()) {
    ImGui::Text("VSync: %s", vsync->second.c_str());
  }

  ImGui::SeparatorText("Runtime engine timing");
  if (!timing.last_engine_callback.timed_frame_observed) {
    ImGui::TextUnformatted("Last engine callback: not observed yet");
  } else if (!timing.last_engine_callback.ran) {
    ImGui::TextUnformatted("Last engine callback: skipped");
    ImGui::Text("Consumed Runtime delta: n/a");
  } else {
    const float consumed{timing.last_engine_callback.consumed_delta_units.value_or(0.0F)};
    ImGui::TextUnformatted("Last engine callback: ran");
    ImGui::Text("Consumed Runtime delta: %.6f units", static_cast<double>(consumed));
    ImGui::Text("Consumed seconds: %.6f s",
        static_cast<double>(consumed / FrameTiming::k_delta_units_per_second));
  }
  ImGui::Text("Next callback base delta: %.6f Runtime units",
      static_cast<double>(timing.next_base_delta_units));
  ImGui::Text("Next callback effective delta: %.6f Runtime units",
      static_cast<double>(timing.next_effective_delta_units));
  ImGui::Text("Time-scale mode: %s", time_scale_mode_name(timing.time_scale_mode));
  if (timing.forced_delta.has_value()) {
    ImGui::Text(
        "Forced delta: %.6f Runtime units", static_cast<double>(timing.forced_delta.value()));
  } else {
    ImGui::TextUnformatted("Forced delta: disabled");
  }
  ImGui::Text("Gameplay pause: %s", timing.gameplay_paused ? "paused" : "running");
  ImGui::TextDisabled(
      "The callback consumes the effective delta calculated after the preceding timed frame.");
  ImGui::TextDisabled("1.0 Runtime timing unit = 1/30 second.");

  ImGui::SeparatorText("Selected SCX runtime");
  show_runtime_target_summary();
  ScenarioRuntime* const selected_runtime{m_runtime_context.resolved().runtime};
  const Script::ScriptRuntime* const script_runtime{
      selected_runtime == nullptr ? nullptr : selected_runtime->script_runtime()};
  if (script_runtime == nullptr) {
    ImGui::TextUnformatted("Selected target has no loaded SCX runtime.");
  } else {
    ImGui::Text(
        "Last real delta: %.6f s", static_cast<double>(script_runtime->last_real_delta_seconds()));
    ImGui::Text("Script delta: %.6f frames @ 30 Hz",
        static_cast<double>(script_runtime->last_script_delta_frames()));
    if (script_runtime->last_script_delta_clamped()) {
      ImGui::TextColored(K_WARNING_COLOR, "Clamped to the 3-frame SCX maximum");
    } else {
      ImGui::TextUnformatted("Clamp: no");
    }
    ImGui::Text("Script tick count: %" PRIu64, script_runtime->tick_count());
  }

  ImGui::SeparatorText("Interface timing");
  const I2DCounters& i2d{Metrics::get().i2d_counters()};
  ImGui::Text("Logical background endpoint tick: %llu",
      static_cast<unsigned long long>(i2d.background_tick));
  ImGui::Text(
      "Endpoint ticks crossed: %llu", static_cast<unsigned long long>(i2d.background_ticks));
  ImGui::Text("Interpolation alpha: %.3f", static_cast<double>(i2d.background_alpha));
  ImGui::Text(
      "Presentation: %s", i2d.background_interpolated ? "interpolated" : "stepped endpoints");
  ImGui::TextDisabled("Interpolation presents between authored 30 Hz logical endpoints.");

  ImGui::SeparatorText("Activity gates");
  ImGui::Text("Render window active: %s", timing.render_window_active ? "yes" : "no");
  ImGui::Text("Application active: %s", timing.application_active ? "yes" : "no");
  ImGui::Text("Updates suspended: %s", timing.updates_suspended ? "yes" : "no");
  ImGui::Text("May run frame: %s", timing.may_run_frame ? "yes" : "no");
  ImGui::Text("Timing re-baseline pending: %s", timing.timing_reset_pending ? "yes" : "no");
  ImGui::Text("Gameplay paused: %s", timing.gameplay_paused ? "yes" : "no");
  ImGui::Text("Engine callback skipped: %s",
      timing.last_engine_callback.timed_frame_observed && !timing.last_engine_callback.ran ? "yes"
                                                                                           : "no");
  ImGui::Text("Persistent skip-engine-frame state: %s",
      timing.skip_engine_frame ? "enabled (read-only)" : "disabled");
  ImGui::TextDisabled(
      "Gameplay pause zeroes effective delta; inactivity, suspension, and skip gate execution.");

  ImGui::SeparatorText("History");
  const std::vector<float> display_order{get_history_display_order(m_runtime_frame_time_history,
      m_runtime_frame_time_history_head,
      m_runtime_frame_time_history_count)};
  if (display_order.empty()) {
    ImGui::TextUnformatted("No completed-frame samples while this panel has been open.");
  } else {
    const float max_value{*std::ranges::max_element(display_order)};
    const float plot_max{std::max(max_value * 1.2F, 16.67F)};
    const std::string overlay{
        fmt::format("{} ms completed frame", timing.last_completed_frame_time_ms)};
    ImGui::PlotLines("Presentation frame time (ms)",
        display_order.data(),
        static_cast<int>(display_order.size()),
        0,
        overlay.c_str(),
        0.0F,
        plot_max,
        ImVec2{0.0F, 80.0F});
  }

  ImGui::SeparatorText("Debug Overrides");
  constexpr std::array<FrameTiming::TimeScaleMode, 5> k_modes{FrameTiming::TimeScaleMode::k_dynamic,
      FrameTiming::TimeScaleMode::k_fixed_30hz,
      FrameTiming::TimeScaleMode::k_fixed_60hz,
      FrameTiming::TimeScaleMode::k_fixed_300hz,
      FrameTiming::TimeScaleMode::k_fixed_15hz};
  int mode_index{static_cast<int>(timing.time_scale_mode)};
  if (ImGui::Combo("Time-scale mode",
          &mode_index,
          "Dynamic\0Fixed 30 Hz\0Fixed 60 Hz\0Fixed 300 Hz\0Fixed 15 Hz\0\0") &&
      mode_index >= 0 && static_cast<std::size_t>(mode_index) < k_modes.size()) {
    source->set_time_scale_mode(k_modes.at(static_cast<std::size_t>(mode_index)));
  }

  bool forced_enabled{timing.forced_delta.has_value()};
  if (timing.forced_delta.has_value()) {
    m_forced_delta_override_value = timing.forced_delta.value();
  }
  if (ImGui::Checkbox("Force Runtime delta", &forced_enabled)) {
    source->set_forced_delta(
        forced_enabled ? std::optional<float>{m_forced_delta_override_value} : std::nullopt);
  }
  ImGui::BeginDisabled(!forced_enabled);
  if (ImGui::DragFloat(
          "Forced Runtime units", &m_forced_delta_override_value, 0.01F, 0.0F, 10.0F, "%.3f")) {
    source->set_forced_delta(m_forced_delta_override_value);
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("Forced delta uses Runtime units: 1.0 = 1/30 second.");

  bool gameplay_paused{timing.gameplay_paused};
  if (ImGui::Checkbox("Gameplay pause", &gameplay_paused)) {
    source->set_gameplay_paused(gameplay_paused);
  }
  ImGui::TextDisabled("Activity gates and persistent skip-engine-frame are read-only here.");

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
#ifdef DEBUG
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

    // Table header
    ImGui::Text("%-6s %-50s %12s %8s", "Rank", "Scope", "Total (us)", "Calls");
    ImGui::Separator();

    std::size_t rank{1};
    for (const auto& entry : std::views::take(sorted, 15)) {
      const auto& [scope_name, scope_stats] = entry;

      // Truncate very long scope names for display.
      std::string display_name = scope_name;
      if (display_name.size() > 55) {
        display_name = "..." + display_name.substr(display_name.size() - 52);
      }

      ImGui::Text("%-6zu %-50s %12" PRId64 " %8zu",
          rank,
          display_name.c_str(),
          scope_stats.total_time.count(),
          scope_stats.call_count);
      ++rank;
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
