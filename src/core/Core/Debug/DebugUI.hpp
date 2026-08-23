#pragma once

#include <SDL3/SDL_video.h>

#include <cstddef>
#include <cstdint>
#include <optional>

#include "Core/Debug/DebugContext.hpp"
#include "Core/Debug/DebugRuntimeContext.hpp"
#include "Core/Debug/LogFilter.hpp"
#include "Core/Debug/SceneDebugView.hpp"
#include "Core/Scene.hpp"

namespace App {
class ScenarioManager;
class ScenarioRuntime;

namespace Script {
struct RuntimeScriptCommand;
struct ScriptInstance;
}  // namespace Script
}  // namespace App

namespace App::Debug {

/// Manages all ImGui debug/performance-monitoring windows.
///
/// Instantiated by Window; update() is called each frame between
/// ImGui::NewFrame() and ImGui::Render().
///
/// All windows are compiled out when APP_DEBUG_UI is not defined.
class DebugUI {
 public:
  explicit DebugUI(SDL_Window* window = nullptr);
  ~DebugUI() = default;

  DebugUI(const DebugUI&) = delete;
  DebugUI(DebugUI&&) = delete;
  DebugUI& operator=(DebugUI other) = delete;
  DebugUI& operator=(DebugUI&& other) = delete;

  /// Set or update the SDL window reference.
  void set_window(SDL_Window* window) {
    m_window = window;
  }

  /// Wires the active scene so sprite tools can inspect and drive it. The
  /// pointer is non-owning; clear it before the scene is destroyed.
  void set_scene(Scene* scene) {
    m_context.scene = scene;
  }

  /// Wires the scenario manager so the Scenarios view can inspect the
  /// gameplay-mode slot and both world contexts. Non-owning.
  void set_scenario_manager(ScenarioManager* manager) {
    m_context.scenario_manager = manager;
  }

  /// Replaces the full debug-context wiring in one call. Non-owning pointers;
  /// Application owns every subsystem and clears them before teardown.
  void set_context(DebugContext context) {
    m_context = context;
  }

  /// Render all active debug windows. Call each frame.
  /// @param delta_time  Frame delta in seconds.
  void update(float delta_time);

  // --- Toggles ---

  void toggle_performance();
  void toggle_system_info();
  void toggle_profiler();
  void toggle_log();
  void toggle_opengl_state();
  void toggle_sprite_inspector();
  void toggle_world_inspector();
  void toggle_visualizers();
  void toggle_scx_script_inspector();
  void toggle_audio_inspector();
  void toggle_scenarios();
  void toggle_area_vm();
  void toggle_runtime_overview();
  void toggle_interface();
  void toggle_startup_trace();

 private:
  // --- Window renderers ---
  void show_menu_bar();
  void show_performance(float delta_time);
  void show_system_info();
  void show_profiler();
  void show_log();
  void show_opengl_state();
  void show_world_inspector();
  void show_visualizers();

  /// Global target selector shared by every scenario-runtime inspector.
  void show_runtime_target_selector();
  /// Refreshes the non-owning target view and explicitly invalidates all
  /// runtime-local selections when its resolved identity changes.
  void refresh_runtime_context();
  /// Compact read-only identity strip for scenario-scoped windows.
  void show_runtime_target_summary() const;

  void show_sprite_inspector(float delta_time);
  void show_sprite_resources_tab(ScenarioRuntime& runtime, SceneDebugView* scene_view);
  void show_sprite_instances_tab(
      ScenarioRuntime& runtime, SceneDebugView* scene_view, float delta_time);
  void show_sprite_frames_tab(ScenarioRuntime& runtime);
  static void show_sprite_queue_tab(const SpriteRenderDebugState& state);
  void show_scx_script_inspector();
  void show_audio_inspector();
  void show_scenarios();
  void show_area_vm();
  void show_runtime_overview();
  void show_interface();
  void show_startup_trace();
  static void show_script_command(Script::ScriptInstance& instance,
      Script::RuntimeScriptCommand& command,
      std::size_t command_index,
      bool is_root);

  SDL_Window* m_window{nullptr};
  /// Non-owning wiring to every subsystem the debug windows inspect.
  DebugContext m_context{};

  // Visibility toggles
  bool m_show_performance{false};  // FPS window hidden by default; F3 re-opens it.
  bool m_show_system_info{false};
  bool m_show_profiler{false};
  bool m_show_log{false};
  bool m_show_opengl_state{false};
  bool m_show_world_inspector{false};
  bool m_show_visualizers{false};
  bool m_show_sprite_inspector{false};
  bool m_show_scx_script_inspector{false};
  bool m_show_audio_inspector{false};
  bool m_show_scenarios{false};
  bool m_show_area_vm{false};
  bool m_show_runtime_overview{false};
  bool m_show_interface{false};
  bool m_show_startup_trace{false};

  // Log window state
  bool m_log_auto_scroll{true};
  /// Client-side severity/category/text filtering for the log viewer.
  Debug::LogFilter m_log_filter{};
  char m_log_filter_text[128]{};
  /// Combo indices: severity (trace..error) and category (0 = all).
  int m_log_min_level_index{2};
  int m_log_category_index{0};

  // Internal frame counter for periodic queries
  std::uint64_t m_frame_count{0};

  // --- Shared SCX runtime inspector target ---
  DebugRuntimeContext m_runtime_context;
  std::uint64_t m_applied_runtime_selection_epoch{0};

  // --- Sprite inspector state ---
  std::size_t m_sprite_selected_resource{0};
  Sprite::SpriteHandle m_sprite_selected_handle;
  bool m_sprite_play_frames{false};
  float m_sprite_play_rate{8.0F};
  float m_sprite_play_accumulator{0.0F};

  // --- SCX Script Inspector state ---
  std::size_t m_script_selected_instance{0};
  std::optional<std::size_t> m_script_selected_source;
  float m_script_fixed_delta{1.0F};

  // --- Scenarios view state ---
  std::uint32_t m_scenarios_selected_scene_id{0};

  // --- Startup trace view state ---
  char m_startup_trace_filter[64]{};
};

}  // namespace App::Debug
