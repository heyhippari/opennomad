#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

#include "Core/Debug/SceneDebugView.hpp"
#include "Core/Interface/InterfacePresenter.hpp"
#include "Core/Scene.hpp"
#include "Core/WorldCamera.hpp"
#include "Core/WorldPresentation.hpp"

namespace App {

class ScenarioManager;
struct WorldSceneContext;
class WorldFadeRenderer;
class WorldLetterboxRenderer;
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
  void render() override;
  void resize(int width, int height) override;

  [[nodiscard]] std::optional<Debug::WorldRenderDebugState>
  world_render_debug_state() const override;

  private:
  WorldScene(ScenarioManager& scenarios, Interface::InterfaceManager& interfaces);

  void consume_fade_commands(const WorldSceneContext* context);
  void consume_letterbox_commands(const WorldSceneContext* context);
  void update_white_fade(float delta_time);

  ScenarioManager* m_scenarios{nullptr};
  Interface::InterfacePresenter m_interfaces;
  std::unique_ptr<WorldRenderer> m_world_renderer;
  std::unique_ptr<WorldFadeRenderer> m_fade_renderer;
  std::unique_ptr<WorldLetterboxRenderer> m_letterbox_renderer;
  WorldCameraSystem m_camera;
  WorldLetterboxState m_letterbox;
  float m_white_fade_alpha{0.0F};
  float m_white_fade_elapsed{0.0F};
  float m_white_fade_duration{0.0F};
  int m_width{640};
  int m_height{480};

  /// Last observed active-world identity; used to detect that the active
  /// context changed or the same slot was recycled with a new generation.
  std::uint32_t m_observed_scene_id{0};
  std::uint32_t m_observed_generation{0};
  bool m_world_observed{false};
};

}  // namespace App
