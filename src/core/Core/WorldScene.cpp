#include "Core/WorldScene.hpp"

#include <expected>
#include <memory>
#include <string>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Scenario/ScenarioManager.hpp"

namespace App {

std::expected<std::unique_ptr<WorldScene>, std::string> WorldScene::create(
    ScenarioManager& scenarios, Interface::InterfaceManager& interfaces) {
  // The constructor is private; only the factory may build a scene.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<WorldScene>{new WorldScene(scenarios, interfaces)};
}

WorldScene::WorldScene(ScenarioManager& scenarios, Interface::InterfaceManager& interfaces)
    : m_scenarios(&scenarios), m_interfaces(interfaces) {}

void WorldScene::update(const float delta_time, const Input::InputManager& input) {
  APP_PROFILE_FUNCTION();

  // Observe the active world context identity/generation so a future
  // WorldRenderer can react to slot swaps without dangling references. This
  // is presentation-only: no script is activated or ticked here.
  if (m_scenarios != nullptr) {
    const WorldSceneContext* context{m_scenarios->active_world_context()};
    if (context != nullptr) {
      if (!m_world_observed || context->scene_id != m_observed_scene_id ||
          context->generation != m_observed_generation) {
        App::Log::debug(LogCategory::Scenario,
            "WorldScene: active world context scene={} generation={}",
            context->scene_id,
            context->generation);
        m_observed_scene_id = context->scene_id;
        m_observed_generation = context->generation;
        m_world_observed = true;
      }
    }
  }

  m_interfaces.update(delta_time, input);
}

void WorldScene::render() {
  APP_PROFILE_FUNCTION();

  // World presentation is a future WorldRenderer; today the framebuffer is
  // already cleared by the renderer's begin_frame. The interface presentation
  // layer draws over the (blank) world — for interface 29 the 640x480 bump
  // background covers it, which is intentional.
  m_interfaces.render(m_width, m_height);
}

void WorldScene::resize(const int width, const int height) {
  m_width = width;
  m_height = height;
}

}  // namespace App
