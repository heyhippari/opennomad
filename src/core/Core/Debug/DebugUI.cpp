#include "Core/Debug/DebugUI.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>
#include <fmt/format.h>
#include <imgui.h>

#include <optional>
#include <string_view>

#include "Core/Debug/DebugRuntimeContext.hpp"
#include "Core/Debug/DebugUIInternal.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Sprite/SpriteInstance.hpp"

namespace App::Debug {

DebugUI::DebugUI(SDL_Window* window) : m_window(window) {}

// ─────────────────────────────────────────────────────────────────────────────
// Toggles
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::toggle_performance() {
  m_show_performance = !m_show_performance;
}
void DebugUI::toggle_system_info() {
  m_show_system_info = !m_show_system_info;
}
void DebugUI::toggle_profiler() {
  m_show_profiler = !m_show_profiler;
}
void DebugUI::toggle_log() {
  m_show_log = !m_show_log;
}
void DebugUI::toggle_opengl_state() {
  m_show_opengl_state = !m_show_opengl_state;
}
void DebugUI::toggle_sprite_inspector() {
  m_show_sprite_inspector = !m_show_sprite_inspector;
}
void DebugUI::toggle_world_inspector() {
  m_show_world_inspector = !m_show_world_inspector;
}
void DebugUI::toggle_visualizers() {
  m_show_visualizers = !m_show_visualizers;
}
void DebugUI::toggle_scx_script_inspector() {
  m_show_scx_script_inspector = !m_show_scx_script_inspector;
}
void DebugUI::toggle_audio_inspector() {
  m_show_audio_inspector = !m_show_audio_inspector;
}
void DebugUI::toggle_scenarios() {
  m_show_scenarios = !m_show_scenarios;
}
void DebugUI::toggle_area_vm() {
  m_show_area_vm = !m_show_area_vm;
}
void DebugUI::toggle_runtime_overview() {
  m_show_runtime_overview = !m_show_runtime_overview;
}
void DebugUI::toggle_interface() {
  m_show_interface = !m_show_interface;
}
void DebugUI::toggle_startup_trace() {
  m_show_startup_trace = !m_show_startup_trace;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main update
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::update(const float delta_time) {
  refresh_runtime_context();

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
          metrics.set_window_info_entry(
              "Refresh Rate", fmt::format("{} Hz", current_mode->refresh_rate));
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
  if (m_show_world_inspector) {
    show_world_inspector();
  }
  if (m_show_visualizers) {
    show_visualizers();
  }
  if (m_show_sprite_inspector) {
    show_sprite_inspector(delta_time);
  }
  if (m_show_scx_script_inspector) {
    show_scx_script_inspector();
  }
  if (m_show_audio_inspector) {
    show_audio_inspector();
  }
  if (m_show_scenarios) {
    show_scenarios();
  }
  if (m_show_area_vm) {
    show_area_vm();
  }
  if (m_show_runtime_overview) {
    show_runtime_overview();
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
    if (ImGui::BeginMenu("Runtime")) {
      ImGui::MenuItem("Runtime Overview", nullptr, &m_show_runtime_overview);
      ImGui::MenuItem("Scenarios", nullptr, &m_show_scenarios);
      ImGui::MenuItem("AREA VM", nullptr, &m_show_area_vm);
      ImGui::MenuItem("SCX Script Inspector", nullptr, &m_show_scx_script_inspector);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("World")) {
      ImGui::MenuItem("World Inspector", nullptr, &m_show_world_inspector);
      ImGui::MenuItem("Sprite Inspector", nullptr, &m_show_sprite_inspector);
      ImGui::MenuItem("Interface Inspector", nullptr, &m_show_interface);
      ImGui::MenuItem("Visualizers", nullptr, &m_show_visualizers);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Audio")) {
      ImGui::MenuItem("Audio Inspector", nullptr, &m_show_audio_inspector);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Diagnostics")) {
      ImGui::MenuItem("Performance", nullptr, &m_show_performance);
      ImGui::MenuItem("Profiler", nullptr, &m_show_profiler);
      ImGui::MenuItem("Log", nullptr, &m_show_log);
      ImGui::MenuItem("Startup Trace", nullptr, &m_show_startup_trace);
      ImGui::MenuItem("System Info", nullptr, &m_show_system_info);
      ImGui::MenuItem("OpenGL State", nullptr, &m_show_opengl_state);
      ImGui::EndMenu();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Target:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0F);
    show_runtime_target_selector();
    ImGui::EndMainMenuBar();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared scenario-runtime target
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_runtime_target_selector() {
  int target{static_cast<int>(m_runtime_context.selected_target())};
  if (ImGui::Combo("##DebugRuntimeTarget",
          &target,
          "Active World\0Gameplay Mode\0World Slot 0\0World Slot 1\0\0")) {
    m_runtime_context.set_selected_target(static_cast<DebugRuntimeTarget>(target));
    refresh_runtime_context();
  }
}

void DebugUI::refresh_runtime_context() {
  m_runtime_context.refresh(m_context.scenario_manager);
  if (m_applied_runtime_selection_epoch != m_runtime_context.selection_epoch()) {
    m_applied_runtime_selection_epoch = m_runtime_context.selection_epoch();
    m_sprite_selected_resource = 0;
    m_sprite_selected_handle = Sprite::SpriteHandle{};
    m_sprite_play_frames = false;
    m_sprite_play_accumulator = 0.0F;
    m_script_selected_instance = 0;
    m_script_selected_source.reset();
  }
}

void DebugUI::show_runtime_target_summary() const {
  const ResolvedDebugRuntimeTarget& target{m_runtime_context.resolved()};
  ImGui::Text("Target: %s",
      fmt::format("{}", debug_runtime_target_name(m_runtime_context.selected_target())).c_str());
  if (!target.identity.role.has_value()) {
    ImGui::SameLine();
    ImGui::TextDisabled("| unavailable");
    return;
  }

  if (target.identity.role == ScenarioRole::GameplayMode) {
    const std::string_view mode{target.gameplay_mode.has_value()
                                    ? gameplay_mode_name(target.gameplay_mode.value())
                                    : std::string_view{"unknown"}};
    ImGui::SameLine();
    ImGui::TextDisabled("| Gameplay Mode | %s | generation %u | %s",
        fmt::format("{}", mode).c_str(),
        target.identity.generation,
        target.available() ? "loaded" : "not loaded");
  } else {
    ImGui::SameLine();
    ImGui::TextDisabled("| World slot %zu | Scene %u | generation %u | %s",
        target.identity.slot.value_or(0),
        target.identity.scene_id,
        target.identity.generation,
        target.residency.has_value() ? residency_name(target.residency.value()) : "unknown");
  }
  if (!target.scenario_path.empty()) {
    ImGui::TextDisabled("%s", fmt::format("{}", target.scenario_path).c_str());
  } else {
    ImGui::TextDisabled("scenario: unavailable");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
}  // namespace App::Debug
