#include "Core/WorldScene.hpp"

#include <cmath>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/WorldCamera.hpp"
#include "Core/WorldPresentation.hpp"
#include "Core/WorldRenderer.hpp"

namespace App {

std::expected<std::unique_ptr<WorldScene>, std::string> WorldScene::create(
    ScenarioManager& scenarios, Interface::InterfaceManager& interfaces) {
  // The constructor is private; only the factory may build a scene.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<WorldScene>{new WorldScene(scenarios, interfaces)};
}

WorldScene::WorldScene(ScenarioManager& scenarios, Interface::InterfaceManager& interfaces)
    : m_scenarios(&scenarios),
      m_interfaces(interfaces) {}

WorldScene::~WorldScene() = default;

void WorldScene::update(const float delta_time, const Input::InputManager& input) {
  APP_PROFILE_FUNCTION();

  WorldSceneContext* context{nullptr};
  if (m_scenarios != nullptr) {
    context = m_scenarios->active_world_context();
    if (context != nullptr) {
      if (!m_world_observed || context->scene_id != m_observed_scene_id ||
          context->generation != m_observed_generation) {
        App::Log::debug(LogCategory::Scenario,
            "WorldScene: active world context scene={} generation={}",
            context->scene_id,
            context->generation);

        m_camera.reset();
        auto renderer{WorldRenderer::create(*context)};
        if (!renderer) {
          App::Log::error(LogCategory::Renderer,
              "WorldScene: failed to build scene {} generation {}: {}",
              context->scene_id,
              context->generation,
              renderer.error());
          m_world_renderer.reset();
        } else {
          m_world_renderer = std::move(renderer).value();
          m_camera.set_fallback_pose(
              m_world_renderer->bounds().center, m_world_renderer->bounds().radius);
        }

        m_observed_scene_id = context->scene_id;
        m_observed_generation = context->generation;
        m_world_observed = true;
      }
    } else if (m_world_observed) {
      m_world_renderer.reset();
      m_camera.reset();
      m_world_observed = false;
    }
  }

  // Drain every command, but only consume commands belonging to the exact
  // active world generation. This makes stale commands harmless if a context
  // is recycled between AREA execution and presentation.
  if (m_scenarios != nullptr) {
    while (std::optional<WorldCameraCommand> command{
        m_scenarios->world_presentation().take_camera()}) {
      if (context == nullptr || command->scene_id != context->scene_id ||
          command->scene_generation != context->generation) {
        App::Log::debug(LogCategory::Renderer,
            "WorldScene: discarded stale camera {} for scene={} generation={}",
            command->camera_id,
            command->scene_id,
            command->scene_generation);
        continue;
      }
      m_camera.apply_command(command.value());
      App::Log::debug(LogCategory::Renderer,
          "World camera {} — duration={} flags={} focal={}",
          command->camera_id,
          command->duration_units,
          command->flags,
          command->focal_parameter);
    }
  }

  m_camera.update(delta_time);

  // The world camera is also the listener for scenario-owned spatial audio.
  if (context != nullptr && context->runtime != nullptr && m_camera.has_pose()) {
    Audio::AudioSystem* audio{context->runtime->audio_system()};
    if (audio != nullptr) {
      const WorldCameraPose& pose{m_camera.pose()};
      const float delta_x{pose.target.at(0) - pose.eye.at(0)};
      const float delta_y{pose.target.at(1) - pose.eye.at(1)};
      const float delta_z{pose.target.at(2) - pose.eye.at(2)};
      const float length{
          std::sqrt((delta_x * delta_x) + (delta_y * delta_y) + (delta_z * delta_z))};
      const float inverse_length{length > 0.0001F ? 1.0F / length : 0.0F};
      Audio::AudioListenerState listener;
      listener.position = Audio::Vec3{pose.eye.at(0), pose.eye.at(1), pose.eye.at(2)};
      listener.velocity = Audio::Vec3{0.0F, 0.0F, 0.0F};
      listener.forward = length > 0.0001F ? Audio::Vec3{delta_x * inverse_length,
                                                delta_y * inverse_length,
                                                delta_z * inverse_length}
                                          : Audio::Vec3{0.0F, 0.0F, -1.0F};
      listener.up = Audio::Vec3{0.0F, 1.0F, 0.0F};
      audio->set_listener(listener);
    }
  }

  m_interfaces.update(delta_time, input);
}

void WorldScene::render() {
  APP_PROFILE_FUNCTION();

  if (m_world_renderer != nullptr && m_scenarios != nullptr) {
    // Scenario execution happens after WorldScene::update() in the frame. A
    // world context may therefore be replaced between update and render;
    // never dereference a runtime cached by the renderer. If generation no
    // longer matches, skip one world frame and rebuild on the next update.
    const WorldSceneContext* context{m_scenarios->active_world_context()};
    if (context != nullptr && m_world_observed && context->scene_id == m_observed_scene_id &&
        context->generation == m_observed_generation) {
      m_world_renderer->render(m_camera.camera(), context->runtime.get());
    }
  }

  // I2D is always the final scene layer. Interface 29's full-screen bump
  // background therefore covers the world while the main menu is active,
  // exactly as the stable WorldScene architecture intends.
  m_interfaces.render(m_width, m_height);
}

void WorldScene::resize(const int width, const int height) {
  m_width = width;
  m_height = height;
  if (width > 0 && height > 0) {
    m_camera.set_aspect_ratio(static_cast<float>(width) / static_cast<float>(height));
  }
}

}  // namespace App
