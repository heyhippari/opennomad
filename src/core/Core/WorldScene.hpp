#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Core/Debug/SceneDebugView.hpp"
#include "Core/Interface/DialogTextLayout.hpp"
#include "Core/Interface/InterfacePresenter.hpp"
#include "Core/Scene.hpp"
#include "Core/WorldCamera.hpp"
#include "Core/WorldPresentation.hpp"

namespace App {

class ScenarioManager;
struct WorldSceneContext;
class WorldFadeRenderer;
class WorldLetterboxRenderer;
class WorldColorPipeline;
class WorldRenderer;

namespace Interface {
class InterfaceManager;
}

/// Stable normal runtime presentation scene, installed once after the startup
/// videos/splash and kept active for the whole session. It composes the world
/// renderer/scripted camera with the generic I2D interface presentation layer.
///
/// WorldScene observes runtime state owned by ScenarioManager and
/// InterfaceManager. It does NOT own a ScenarioRuntime, does NOT execute
/// scripts, and does NOT update AudioSystem.
class WorldScene final : public Scene, public Debug::SceneDebugView {
 public:
  static std::expected<std::unique_ptr<WorldScene>, std::string> create(
      ScenarioManager& scenarios, Interface::InterfaceManager& interfaces);

  ~WorldScene() override;

  WorldScene(const WorldScene&) = delete;
  WorldScene(WorldScene&&) = delete;
  WorldScene& operator=(const WorldScene&) = delete;
  WorldScene& operator=(WorldScene&&) = delete;

  void update(float delta_time, const Input::InputManager& input) override;
  void post_scenario_update(float delta_time) override;
  void render() override;
  void resize(int width, int height) override;

  [[nodiscard]] std::optional<Debug::WorldRenderDebugState> world_render_debug_state()
      const override;
  [[nodiscard]] std::optional<Debug::SpriteRenderDebugState> sprite_render_debug_state()
      const override;
  [[nodiscard]] std::optional<std::array<float, 3>> sprite_debug_focus_position() const override;
  [[nodiscard]] bool sprite_grayscale_supported() const override;
  [[nodiscard]] bool sprite_grayscale_enabled() const override;
  void set_sprite_grayscale_enabled(bool enabled) override;
  [[nodiscard]] bool geometry_wireframe_supported() const override {
    return true;
  }
  [[nodiscard]] bool geometry_wireframe_enabled() const override;
  void set_geometry_wireframe_enabled(bool enabled) override;

 private:
  WorldScene(ScenarioManager& scenarios, Interface::InterfaceManager& interfaces);

  void synchronize_presentation_reset();
  [[nodiscard]] WorldSceneContext* synchronize_world_context();
  void consume_camera_commands(const WorldSceneContext* context);
  void consume_fade_commands();
  void consume_letterbox_commands();
  void consume_object_presentation_commands(const WorldSceneContext* context);
  void service_camera_completion(const WorldSceneContext* context);
  void service_structured_camera_release(const WorldSceneContext* context);
  void update_audio_listener(const WorldSceneContext* context);
  [[nodiscard]] bool update_dialog_input(float delta_time, const Input::InputManager& input);

  ScenarioManager* m_scenarios{nullptr};
  Interface::InterfacePresenter m_interfaces;
  struct AttachedWorldRenderer {
    std::uint32_t scene_id{0};
    std::uint32_t generation{0};
    std::unique_ptr<WorldRenderer> renderer;
  };
  std::vector<AttachedWorldRenderer> m_attached_world_renderers;
  WorldRenderer* m_world_renderer{nullptr};
  std::unique_ptr<WorldFadeRenderer> m_fade_renderer;
  std::unique_ptr<WorldLetterboxRenderer> m_letterbox_renderer;
  std::unique_ptr<WorldColorPipeline> m_color_pipeline;
  WorldCameraSystem m_camera;
  /// Session-global overlays. World-context changes must not reset these;
  /// only m_presentation_reset_observer may apply the explicit session epoch.
  WorldFadeState m_fade;
  WorldLetterboxState m_letterbox;
  WorldPresentationResetObserver m_presentation_reset_observer;
  WorldTextState m_world_text;
  WorldUvPhaseState m_uv_phases;
  int m_width{640};
  int m_height{480};

  /// Last observed active-world identity; used to detect that the active
  /// context changed or the same slot was recycled with a new generation.
  std::uint32_t m_observed_scene_id{0};
  std::uint32_t m_observed_generation{0};
  bool m_world_observed{false};
  bool m_geometry_wireframe_enabled{false};
  std::uint64_t m_observed_dialog_generation{0};
  std::size_t m_selected_dialog_choice{0};
  bool m_dialog_observed{false};
  Interface::DialogScrollState m_dialog_scroll;
  std::string m_color_pipeline_error;
};

}  // namespace App
