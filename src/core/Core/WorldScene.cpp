#include "Core/WorldScene.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/SceneDebugView.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/RuntimePresentation.hpp"
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

std::optional<Debug::WorldRenderDebugState> WorldScene::world_render_debug_state() const {
  Debug::WorldRenderDebugState state;

  state.renderer_ready = m_world_renderer != nullptr;
  if (m_world_renderer != nullptr) {
    state.group_count = m_world_renderer->group_count();
    state.material_count = m_world_renderer->material_count();
    state.mirror_group_count = m_world_renderer->mirror_group_count();
    state.uv_scroll_u_group_count = m_world_renderer->uv_scroll_u_group_count();
    state.uv_scroll_v_group_count = m_world_renderer->uv_scroll_v_group_count();
    state.environment_group_count = m_world_renderer->environment_group_count();
    state.bounds_center = m_world_renderer->bounds().center;
    state.bounds_radius = m_world_renderer->bounds().radius;
  }

  const WorldSceneContext* world_context{
      m_scenarios != nullptr ? m_scenarios->active_world_context() : nullptr};
  if (world_context != nullptr && world_context->decor_model.has_value()) {
    const Omikron::Model3DOData& model{world_context->decor_model.value()};

    state.root_mesh_id = model.header.root_mesh_id;
    if (model.root_mesh_index >= 0) {
      state.root_mesh_index = static_cast<std::size_t>(model.root_mesh_index);
    }

    state.mesh_hierarchy.reserve(model.meshes.size());
    for (std::size_t index{0}; index < model.meshes.size(); ++index) {
      const Omikron::MeshDescriptor& mesh{model.meshes.at(index)};

      const auto vec3 = [](const Omikron::Vec3& value) {
        return std::array<float, 3>{value.x, value.y, value.z};
      };

      const bool reachable{
          index < model.hierarchy_reachable.size() && model.hierarchy_reachable.at(index) != 0U};
      const Omikron::Model3DOData::RuntimeObjectState object{
          index < model.runtime_objects.size() ? model.runtime_objects.at(index)
                                               : Omikron::Model3DOData::RuntimeObjectState{}};

      state.mesh_hierarchy.push_back(Debug::WorldMeshHierarchyDebugState{.mesh_id = mesh.mesh_id,
          .name = mesh.name,
          .parent_id = mesh.parent_id,
          .first_child_id = mesh.first_child_id,
          .next_sibling_id = mesh.next_sibling_id,
          .reachable = reachable,
          .root = std::cmp_equal(model.root_mesh_index, index),
          .position = vec3(mesh.position),
          .bone_position = vec3(mesh.bone_position),
          .runtime_local_offset = vec3(object.local_offset),
          .runtime_local_matrix = object.local_matrix.values,
          .runtime_world_translation = vec3(object.world_translation),
          .runtime_world_matrix = object.world_matrix.values});
    }
  }

  if (world_context != nullptr && world_context->runtime != nullptr) {
    for (const Character::RuntimeCharacter& character :
        world_context->runtime->character_runtime().characters()) {
      const Runtime::Vec3 render_position{
          Runtime::Presentation::to_gl(character.transform.translation)};
      Runtime::Vec3 bounds_center{character.transform.translation};
      std::size_t group_count{0};
      float bounds_radius{0.0F};
      if (character.model_resource != nullptr) {
        bounds_center = Runtime::transform_point(
            character.model_resource->bounds_center, character.transform);
        group_count = character.model_resource->groups.size();
        bounds_radius = character.model_resource->bounds_radius;
      }
      state.runtime_characters.push_back(Debug::RuntimeCharacterDebugState{
          .instance_id = character.instance_id,
          .character_id = character.character_id,
          .area_id = character.area_id,
          .active = character.active,
          .area_present = character.area_present,
          .loaded = character.loaded(),
          .renderable = character.renderable(),
          .serialized_position = character.serialized_area_position,
          .runtime_position = {character.transform.translation.x,
              character.transform.translation.y,
              character.transform.translation.z},
          .render_position = {render_position.x, render_position.y, render_position.z},
          .serialized_orientation_units = character.serialized_orientation_units,
          .runtime_orientation_degrees = character.runtime_orientation_degrees,
          .definition_name = character.definition_name,
          .model_resource = character.model_resource_name,
          .model_group_count = group_count,
          .runtime_bounds_center = {bounds_center.x, bounds_center.y, bounds_center.z},
          .bounds_radius = bounds_radius});
    }
  }

  state.camera_has_pose = m_camera.has_pose();
  state.camera_scripted = m_camera.has_scripted_pose();
  state.camera_transitioning = m_camera.transitioning();
  state.camera_id = m_camera.active_camera_id();

  if (state.camera_has_pose) {
    const WorldCameraPose& pose{m_camera.pose()};
    const auto vec3 = [](const Runtime::Vec3& value) {
      return std::array<float, 3>{value.x, value.y, value.z};
    };
    state.camera_runtime_eye = vec3(pose.eye);
    state.camera_runtime_target = vec3(pose.target);
    state.camera_render_eye = vec3(Runtime::Presentation::to_gl(pose.eye));
    state.camera_render_target = vec3(Runtime::Presentation::to_gl(pose.target));
    state.camera_roll_degrees = pose.roll_degrees;
    state.camera_horizontal_fov_degrees = pose.horizontal_fov_degrees;
    state.camera_vertical_fov_4_3_degrees =
        Runtime::horizontal_4_3_to_vertical_fov(pose.horizontal_fov_degrees);
    state.camera_near_inches = m_camera.camera().get_near_plane();
    state.camera_far_inches = m_camera.camera().get_far_plane();
    if (m_camera.last_command().has_value()) {
      state.camera_serialized_eye = m_camera.last_command()->serialized_eye;
      state.camera_serialized_target = m_camera.last_command()->serialized_target;
    }
  }

  return state;
}

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
          // WorldRenderer bounds are presentation-local. Convert the centre
          // back through the involutive B basis for Runtime-native fallback state.
          m_camera.set_fallback_pose(
              Runtime::Presentation::to_gl(m_world_renderer->bounds().center),
              m_world_renderer->bounds().radius);
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
          "World camera {} — duration={} flags={} roll={}deg hFov={}deg",
          command->camera_id,
          command->duration_units,
          command->flags,
          command->roll_degrees,
          command->horizontal_fov_degrees);
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
      const Runtime::Matrix3& view{m_camera.runtime_view().world_to_camera.matrix};
      Audio::AudioListenerState listener;
      // The software spatializer is metre-based (speed of sound is m/s), so
      // inches convert exactly here at the audio boundary. Orientation stays
      // in Runtime's native basis; matrix column 2 is forward and -column 1 is up.
      listener.position = Audio::Vec3{Runtime::inches_to_metres(pose.eye.x),
          Runtime::inches_to_metres(pose.eye.y),
          Runtime::inches_to_metres(pose.eye.z)};
      listener.velocity = Audio::Vec3{0.0F, 0.0F, 0.0F};
      listener.forward = Audio::Vec3{view.at(0, 2), view.at(1, 2), view.at(2, 2)};
      listener.up = Audio::Vec3{-view.at(0, 1), -view.at(1, 1), -view.at(2, 1)};
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
