#include "Core/WorldScene.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Shader.hpp"
#include "Core/WorldCamera.hpp"
#include "Core/WorldPresentation.hpp"
#include "Core/WorldRenderer.hpp"

namespace App {

namespace {

constexpr float K_PRESENTATION_FRAMES_PER_SECOND{30.0F};

constexpr std::string_view K_FADE_VERTEX_SHADER{R"glsl(
#version 410 core

void main() {
  const vec2 positions[3] = vec2[3](
      vec2(-1.0, -1.0),
      vec2( 3.0, -1.0),
      vec2(-1.0,  3.0));
  gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl"};

constexpr std::string_view K_FADE_FRAGMENT_SHADER{R"glsl(
#version 410 core

uniform float u_alpha;
out vec4 frag_color;

void main() {
  frag_color = vec4(1.0, 1.0, 1.0, u_alpha);
}
)glsl"};

}  // namespace

/// Minimal full-screen presentation pass for Runtime's mode-2 white fade.
/// It deliberately owns no timing; WorldScene advances the recovered 30 Hz
/// effect at display rate and supplies only the current alpha.
class WorldFadeRenderer {
 public:
  static std::expected<std::unique_ptr<WorldFadeRenderer>, std::string> create() {
    auto shader{Shader::create(K_FADE_VERTEX_SHADER, K_FADE_FRAGMENT_SHADER)};
    if (!shader) {
      return std::expected<std::unique_ptr<WorldFadeRenderer>, std::string>{
          std::unexpect, shader.error()};
    }

    auto renderer{std::make_unique<WorldFadeRenderer>()};
    renderer->m_shader = std::make_unique<Shader>(std::move(shader).value());
    glGenVertexArrays(1, &renderer->m_vertex_array);
    if (renderer->m_vertex_array == 0U) {
      return std::expected<std::unique_ptr<WorldFadeRenderer>, std::string>{
          std::unexpect, "failed to create presentation fade vertex array"};
    }
    return std::expected<std::unique_ptr<WorldFadeRenderer>, std::string>{std::move(renderer)};
  }

  ~WorldFadeRenderer() {
    if (m_vertex_array != 0U) {
      glDeleteVertexArrays(1, &m_vertex_array);
    }
  }

  WorldFadeRenderer() = default;
  WorldFadeRenderer(const WorldFadeRenderer&) = delete;
  WorldFadeRenderer(WorldFadeRenderer&&) = delete;
  WorldFadeRenderer& operator=(const WorldFadeRenderer&) = delete;
  WorldFadeRenderer& operator=(WorldFadeRenderer&&) = delete;

  void render(const float alpha) const {
    if (m_shader == nullptr || m_vertex_array == 0U || alpha <= 0.0F) {
      return;
    }

    m_shader->bind();
    m_shader->set_uniform_float("u_alpha", std::clamp(alpha, 0.0F, 1.0F));

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(m_vertex_array);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    Shader::unbind();
  }

 private:
  std::unique_ptr<Shader> m_shader;
  GLuint m_vertex_array{0};
};

std::expected<std::unique_ptr<WorldScene>, std::string> WorldScene::create(
    ScenarioManager& scenarios, Interface::InterfaceManager& interfaces) {
  // The constructor is private; only the factory may build a scene.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  auto scene{std::unique_ptr<WorldScene>{new WorldScene(scenarios, interfaces)}};
  auto fade_renderer{WorldFadeRenderer::create()};
  if (!fade_renderer) {
    return std::expected<std::unique_ptr<WorldScene>, std::string>{
        std::unexpect, fade_renderer.error()};
  }
  scene->m_fade_renderer = std::move(fade_renderer).value();
  return std::expected<std::unique_ptr<WorldScene>, std::string>{std::move(scene)};
}

WorldScene::WorldScene(ScenarioManager& scenarios, Interface::InterfaceManager& interfaces)
    : m_scenarios(&scenarios),
      m_interfaces(interfaces) {}

WorldScene::~WorldScene() = default;

void WorldScene::consume_fade_commands(const WorldSceneContext* const context) {
  if (m_scenarios == nullptr) {
    return;
  }

  while (std::optional<WorldFadeCommand> command{m_scenarios->world_presentation().take_fade()}) {
    if (context == nullptr || command->scene_id != context->scene_id ||
        command->scene_generation != context->generation) {
      App::Log::debug(LogCategory::Renderer,
          "WorldScene: discarded stale presentation fade for scene={} generation={}",
          command->scene_id,
          command->scene_generation);
      continue;
    }

    if (command->mode != 2U) {
      App::Log::debug(LogCategory::Renderer,
          "WorldScene: presentation mode {} remains unsupported",
          command->mode);
      continue;
    }

    const float duration_frames{std::abs(static_cast<float>(command->duration_units))};
    m_white_fade_duration = duration_frames / K_PRESENTATION_FRAMES_PER_SECOND;
    m_white_fade_elapsed = 0.0F;
    m_white_fade_alpha = m_white_fade_duration > 0.0F ? 1.0F : 0.0F;

    App::Log::debug(LogCategory::Renderer,
        "World white fade — duration={} color={:#010x} arg={}",
        command->duration_units,
        command->color,
        command->operand_c);
  }
}

void WorldScene::update_white_fade(const float delta_time) {
  if (m_white_fade_alpha <= 0.0F) {
    return;
  }
  if (m_white_fade_duration <= 0.0F) {
    m_white_fade_alpha = 0.0F;
    return;
  }

  m_white_fade_elapsed += std::max(delta_time, 0.0F);
  const float amount{std::clamp(m_white_fade_elapsed / m_white_fade_duration, 0.0F, 1.0F)};
  // Runtime mode 2: 255 * (1 - elapsed/duration).
  m_white_fade_alpha = 1.0F - amount;
}

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

  consume_fade_commands(context);
  update_white_fade(delta_time);

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

  // Scenario execution happens after WorldScene::update() in the frame, so
  // consume newly-emitted fades again here. This lets opcode 0x77 cover the
  // very first world frame at alpha 1 instead of exposing one dark frame.
  const WorldSceneContext* context{
      m_scenarios != nullptr ? m_scenarios->active_world_context() : nullptr};
  consume_fade_commands(context);

  // A world context may be replaced between update and render; never
  // dereference a runtime cached by the renderer. If generation no longer
  // matches, skip one world frame and rebuild on the next update.
  if (m_world_renderer != nullptr && context != nullptr && m_world_observed &&
      context->scene_id == m_observed_scene_id && context->generation == m_observed_generation) {
    m_world_renderer->render(m_camera.camera(), context->runtime.get());
  }

  if (m_fade_renderer != nullptr && m_white_fade_alpha > 0.0F) {
    m_fade_renderer->render(m_white_fade_alpha);
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
