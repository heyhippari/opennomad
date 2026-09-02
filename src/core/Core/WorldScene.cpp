#include "Core/WorldScene.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Character/CtlController.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/SceneDebugView.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Interface/RuntimeText.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Renderer.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/RuntimePresentation.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Shader.hpp"
#include "Core/WorldCamera.hpp"
#include "Core/WorldColorPipeline.hpp"
#include "Core/WorldPresentation.hpp"
#include "Core/WorldRenderer.hpp"

namespace App {

namespace {

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

uniform vec3 u_color;
uniform float u_alpha;
out vec4 frag_color;

void main() {
  frag_color = vec4(u_color, u_alpha);
}
)glsl"};

constexpr std::string_view K_LETTERBOX_VERTEX_SHADER{R"glsl(
#version 410 core

void main() {
  const vec2 positions[3] = vec2[3](
      vec2(-1.0, -1.0),
      vec2( 3.0, -1.0),
      vec2(-1.0,  3.0));
  gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}
)glsl"};

constexpr std::string_view K_LETTERBOX_FRAGMENT_SHADER{R"glsl(
#version 410 core

uniform float u_bar_height;
uniform float u_viewport_height;
out vec4 frag_color;

void main() {
  if (gl_FragCoord.y >= u_bar_height &&
      gl_FragCoord.y < u_viewport_height - u_bar_height) {
    discard;
  }
  frag_color = vec4(0.0, 0.0, 0.0, 1.0);
}
)glsl"};

void restore_capability(const GLenum capability, const bool enabled) {
  if (enabled) {
    glEnable(capability);
  } else {
    glDisable(capability);
  }
}

}  // namespace

/// Minimal full-screen presentation pass for Runtime's authored-colour fades.
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

  void render(const std::uint32_t color, const float alpha) const {
    if (m_shader == nullptr || m_vertex_array == 0U || alpha <= 0.0F) {
      return;
    }

    m_shader->bind();
    constexpr float k_byte_to_float{1.0F / 255.0F};
    const std::array<GLfloat, 3> rgb{static_cast<GLfloat>((color >> 16U) & 0xFFU) * k_byte_to_float,
        static_cast<GLfloat>((color >> 8U) & 0xFFU) * k_byte_to_float,
        static_cast<GLfloat>(color & 0xFFU) * k_byte_to_float};
    m_shader->set_uniform_vec3("u_color", std::span<const GLfloat, 3>{rgb});
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

/// Opaque, depth-independent presentation overlay for the cinematic mask.
/// Timing and resolution-independent geometry remain owned by WorldScene.
class WorldLetterboxRenderer {
 public:
  static std::expected<std::unique_ptr<WorldLetterboxRenderer>, std::string> create() {
    auto shader{Shader::create(K_LETTERBOX_VERTEX_SHADER, K_LETTERBOX_FRAGMENT_SHADER)};
    if (!shader) {
      return std::expected<std::unique_ptr<WorldLetterboxRenderer>, std::string>{
          std::unexpect, shader.error()};
    }

    auto renderer{std::make_unique<WorldLetterboxRenderer>()};
    renderer->m_shader = std::make_unique<Shader>(std::move(shader).value());
    glGenVertexArrays(1, &renderer->m_vertex_array);
    if (renderer->m_vertex_array == 0U) {
      return std::expected<std::unique_ptr<WorldLetterboxRenderer>, std::string>{
          std::unexpect, "failed to create cinematic letterbox vertex array"};
    }
    return std::expected<std::unique_ptr<WorldLetterboxRenderer>, std::string>{std::move(renderer)};
  }

  ~WorldLetterboxRenderer() {
    if (m_vertex_array != 0U) {
      glDeleteVertexArrays(1, &m_vertex_array);
    }
  }

  WorldLetterboxRenderer() = default;
  WorldLetterboxRenderer(const WorldLetterboxRenderer&) = delete;
  WorldLetterboxRenderer(WorldLetterboxRenderer&&) = delete;
  WorldLetterboxRenderer& operator=(const WorldLetterboxRenderer&) = delete;
  WorldLetterboxRenderer& operator=(WorldLetterboxRenderer&&) = delete;

  void render(const float bar_height, const float viewport_height) const {
    if (m_shader == nullptr || m_vertex_array == 0U || bar_height <= 0.0F ||
        viewport_height <= 0.0F) {
      return;
    }

    const bool depth_test_enabled{glIsEnabled(GL_DEPTH_TEST) == GL_TRUE};
    const bool blend_enabled{glIsEnabled(GL_BLEND) == GL_TRUE};
    const bool cull_enabled{glIsEnabled(GL_CULL_FACE) == GL_TRUE};
    const bool scissor_enabled{glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE};
    const bool stencil_enabled{glIsEnabled(GL_STENCIL_TEST) == GL_TRUE};
    GLboolean depth_write_enabled{GL_TRUE};
    GLint previous_program{0};
    GLint previous_vertex_array{0};
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write_enabled);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vertex_array);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);

    m_shader->bind();
    m_shader->set_uniform_float("u_bar_height", bar_height);
    m_shader->set_uniform_float("u_viewport_height", viewport_height);
    glBindVertexArray(m_vertex_array);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(static_cast<GLuint>(previous_vertex_array));
    glUseProgram(static_cast<GLuint>(previous_program));
    glDepthMask(depth_write_enabled);
    restore_capability(GL_DEPTH_TEST, depth_test_enabled);
    restore_capability(GL_BLEND, blend_enabled);
    restore_capability(GL_CULL_FACE, cull_enabled);
    restore_capability(GL_SCISSOR_TEST, scissor_enabled);
    restore_capability(GL_STENCIL_TEST, stencil_enabled);
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
  auto letterbox_renderer{WorldLetterboxRenderer::create()};
  if (!letterbox_renderer) {
    return std::expected<std::unique_ptr<WorldScene>, std::string>{
        std::unexpect, letterbox_renderer.error()};
  }
  scene->m_letterbox_renderer = std::move(letterbox_renderer).value();
  auto color_pipeline{WorldColorPipeline::create()};
  if (!color_pipeline) {
    return std::expected<std::unique_ptr<WorldScene>, std::string>{
        std::unexpect, "world color pipeline: " + std::move(color_pipeline).error()};
  }
  scene->m_color_pipeline = std::move(color_pipeline).value();
  return std::expected<std::unique_ptr<WorldScene>, std::string>{std::move(scene)};
}

WorldScene::WorldScene(ScenarioManager& scenarios, Interface::InterfaceManager& interfaces)
    : m_scenarios(&scenarios),
      m_interfaces(interfaces) {
  m_camera.set_attachment_pose_provider(
      [this](const std::int16_t character_id) -> std::optional<WorldCameraAttachmentPose> {
        if (m_scenarios == nullptr) {
          return std::nullopt;
        }
        const WorldSceneContext* const context{m_scenarios->active_world_context()};
        ScenarioRuntime* const runtime{
            context == nullptr || context->runtime == nullptr ? nullptr : context->runtime.get()};
        const Character::RuntimeCharacter* const character{
            runtime == nullptr ? nullptr : runtime->character_runtime().find(character_id)};
        if (character == nullptr) {
          return std::nullopt;
        }
        return WorldCameraAttachmentPose{.translation = character->transform.translation,
            .principal_orientation = character->principal_orientation()};
      });

  // Runtime publishes the scene's currently selected named 3DO camera through
  // global 0x009103D4 after structured script execution. Controller mode 13
  // consumes that live record continuously. Resolve it from the exact active
  // world rather than from the controlled-character owner.
  m_camera.set_controller_pose_provider([this]() -> std::optional<WorldCameraPose> {
    if (m_scenarios == nullptr) {
      return std::nullopt;
    }
    const WorldSceneContext* context{m_scenarios->active_world_context()};
    const ScenarioRuntime* runtime{
        context == nullptr || context->runtime == nullptr ? nullptr : context->runtime.get()};
    const Omikron::CameraRecord* source{
        runtime == nullptr ? nullptr : runtime->selected_structured_camera()};
    if (source == nullptr) {
      return std::nullopt;
    }
    return WorldCameraPose{.eye = source->eye,
        .target = source->target,
        .roll_degrees = source->roll_degrees,
        .horizontal_fov_degrees = source->horizontal_fov_degrees};
  });
}

WorldScene::~WorldScene() = default;

std::optional<Debug::WorldRenderDebugState> WorldScene::world_render_debug_state() const {
  Debug::WorldRenderDebugState state;

  state.renderer_ready = m_world_renderer != nullptr;
  if (m_color_pipeline != nullptr) {
    state.color_pipeline_ready = m_color_pipeline->width() > 0;
    state.current_scene_a = m_color_pipeline->current_scene_is_a();
    const LegacyBlendCompositorStats& stats{m_color_pipeline->stats()};
    state.legacy_stages = stats.stages;
    state.legacy_source_draws = stats.source_draws;
    state.legacy_composites = stats.composites;
  }
  state.uv_phase_u = static_cast<float>(m_uv_phases.u_phase());
  state.uv_phase_v = static_cast<float>(m_uv_phases.v_phase());
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

      std::vector<std::int32_t> material_ids;
      const auto add_material = [&material_ids](const std::int32_t material_id) {
        if (!std::ranges::contains(material_ids, material_id)) {
          material_ids.push_back(material_id);
        }
      };
      if (index < model.polygons.size()) {
        const Omikron::MeshPolygons& polygons{model.polygons.at(index)};
        for (const Omikron::Triangle& triangle : polygons.triangles) {
          add_material(triangle.material_id);
        }
        for (const Omikron::Rectangle& rectangle : polygons.rectangles) {
          add_material(rectangle.material_id);
        }
      }
      std::vector<std::string> materials;
      materials.reserve(material_ids.size());
      for (const std::int32_t material_id : material_ids) {
        std::string material{std::to_string(material_id)};
        if (material_id >= 0 && static_cast<std::size_t>(material_id) < model.materials.size()) {
          material += ": " + model.materials.at(static_cast<std::size_t>(material_id)).name;
        } else {
          material += ": <invalid>";
        }
        materials.push_back(std::move(material));
      }

      state.mesh_hierarchy.push_back(Debug::WorldMeshHierarchyDebugState{.descriptor_index = index,
          .mesh_id = mesh.mesh_id,
          .script_id = mesh.script_id,
          .flags = mesh.flags,
          .mover_flags = mesh.mover_flags,
          .name = mesh.name,
          .parent_id = mesh.parent_id,
          .first_child_id = mesh.first_child_id,
          .next_sibling_id = mesh.next_sibling_id,
          .reachable = reachable,
          .root = std::cmp_equal(model.root_mesh_index, index),
          .top_level = mesh.parent_id == -1,
          .vertex_count = mesh.vertex_count,
          .triangle_count = mesh.triangle_count,
          .rectangle_count = mesh.rectangle_count,
          .materials = std::move(materials),
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
      const Character::ModelResource* const model_resource{character.model_resource.get()};
      const Runtime::Vec3 render_position{
          Runtime::Presentation::to_gl(character.transform.translation)};
      Runtime::Vec3 bounds_center{character.transform.translation};
      std::size_t group_count{0};
      float bounds_radius{0.0F};
      if (model_resource != nullptr) {
        group_count = character.posed_groups.size();
        Runtime::Vec3 minimum{.x = std::numeric_limits<float>::max(),
            .y = std::numeric_limits<float>::max(),
            .z = std::numeric_limits<float>::max()};
        Runtime::Vec3 maximum{.x = std::numeric_limits<float>::lowest(),
            .y = std::numeric_limits<float>::lowest(),
            .z = std::numeric_limits<float>::lowest()};
        bool has_vertices{false};
        for (const Omikron::MaterialGroup& group : character.posed_groups) {
          for (const Vertex& vertex : group.vertices) {
            const Runtime::Vec3 world{
                Runtime::transform_point(Runtime::Vec3{.x = vertex.position.at(0),
                                             .y = vertex.position.at(1),
                                             .z = vertex.position.at(2)},
                    character.presentation_transform())};
            minimum.x = std::min(minimum.x, world.x);
            minimum.y = std::min(minimum.y, world.y);
            minimum.z = std::min(minimum.z, world.z);
            maximum.x = std::max(maximum.x, world.x);
            maximum.y = std::max(maximum.y, world.y);
            maximum.z = std::max(maximum.z, world.z);
            has_vertices = true;
          }
        }
        if (has_vertices) {
          bounds_center = {.x = (minimum.x + maximum.x) * 0.5F,
              .y = (minimum.y + maximum.y) * 0.5F,
              .z = (minimum.z + maximum.z) * 0.5F};
          const float extent_x{maximum.x - minimum.x};
          const float extent_y{maximum.y - minimum.y};
          const float extent_z{maximum.z - minimum.z};
          bounds_radius = 0.5F * std::sqrt((extent_x * extent_x) + (extent_y * extent_y) +
                                           (extent_z * extent_z));
        } else {
          bounds_center = Runtime::transform_point(
              model_resource->bounds_center, character.presentation_transform());
          bounds_radius = model_resource->bounds_radius;
        }
      }
      const Character::BodyAnimationPlayback& animation{character.body_animation};
      std::uint32_t selected_mesh_id{0};
      std::uint32_t selected_script_id{0};
      std::uint32_t selected_triangle_count{0};
      std::uint32_t selected_rectangle_count{0};
      bool selected_is_root{false};
      bool selected_is_actor_object{false};
      std::optional<std::size_t> hierarchy_root_index;
      std::string hierarchy_root_name;
      std::optional<std::size_t> actor_object_index;
      std::string actor_object_name;
      std::uint32_t actor_object_triangle_count{0};
      std::uint32_t actor_object_rectangle_count{0};
      if (model_resource != nullptr &&
          animation.selected_object_index < model_resource->model.meshes.size()) {
        const Omikron::MeshDescriptor& selected{
            model_resource->model.meshes.at(animation.selected_object_index)};
        selected_mesh_id = selected.mesh_id;
        selected_script_id = selected.script_id;
        selected_triangle_count = selected.triangle_count;
        selected_rectangle_count = selected.rectangle_count;
        selected_is_root =
            std::cmp_equal(animation.selected_object_index, model_resource->model.root_mesh_index);
        selected_is_actor_object =
            model_resource->actor_object_index == animation.selected_object_index;
        if (model_resource->model.root_mesh_index >= 0) {
          hierarchy_root_index = static_cast<std::size_t>(model_resource->model.root_mesh_index);
          hierarchy_root_name = model_resource->model.meshes.at(hierarchy_root_index.value()).name;
        }
        actor_object_index = model_resource->actor_object_index;
        if (actor_object_index.has_value()) {
          const Omikron::MeshDescriptor& actor_object{
              model_resource->model.meshes.at(actor_object_index.value())};
          actor_object_name = actor_object.name;
          actor_object_triangle_count = actor_object.triangle_count;
          actor_object_rectangle_count = actor_object.rectangle_count;
        }
      }
      Debug::RuntimeCharacterDebugState debug_character{.body_identity = character.body_identity,
          .instance_id = character.instance_id,
          .character_id = character.character_id,
          .area_id = character.area_id,
          .active = character.active,
          .area_present = character.area_present,
          .loaded = character.loaded(),
          .renderable = character.renderable(),
          .ordinary_actor_service_generation = character.ordinary_actor_service_generation,
          .physical_support_object_index = std::nullopt,
          .physical_support_object_name = {},
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
          .bounds_radius = bounds_radius,
          .body_animation_active = animation.active,
          .body_animation_completed = animation.completed,
          .selected_object_index = animation.selected_object_index,
          .selected_object = animation.selected_object_name,
          .selected_mesh_id = selected_mesh_id,
          .selected_script_id = selected_script_id,
          .selected_triangle_count = selected_triangle_count,
          .selected_rectangle_count = selected_rectangle_count,
          .selected_is_root = selected_is_root,
          .selected_is_actor_object = selected_is_actor_object,
          .hierarchy_root_index = hierarchy_root_index,
          .hierarchy_root_name = hierarchy_root_name,
          .actor_object_index = actor_object_index,
          .actor_object_name = actor_object_name,
          .actor_object_triangle_count = actor_object_triangle_count,
          .actor_object_rectangle_count = actor_object_rectangle_count,
          .animation_descriptor_index = animation.animation_descriptor_index,
          .animation_name = animation.animation_name,
          .animation_id = animation.animation_id,
          .animation_max_frame = animation.max_frame_index,
          .animation_previous_progress = animation.previous_progress,
          .animation_current_progress = animation.current_progress,
          .animation_execution_count = animation.execution_count,
          .animation_execution_limit = animation.execution_limit,
          .path_index = animation.path_index,
          .path_name = animation.path_name,
          .subpath_index = animation.subpath_index,
          .subpath_name = animation.subpath_name,
          .sampled_path_position = {animation.sampled_path_position.x,
              animation.sampled_path_position.y,
              animation.sampled_path_position.z},
          .authored_offset = {animation.authored_offset.x,
              animation.authored_offset.y,
              animation.authored_offset.z},
          .final_anchor = {animation.final_anchor.x,
              animation.final_anchor.y,
              animation.final_anchor.z},
          .root_motion_delta = {animation.root_motion_delta.x,
              animation.root_motion_delta.y,
              animation.root_motion_delta.z},
          .logical_actor_delta = {animation.logical_actor_delta.x,
              animation.logical_actor_delta.y,
              animation.logical_actor_delta.z},
          .accumulated_visual_translation = {animation.accumulated_visual_translation.x,
              animation.accumulated_visual_translation.y,
              animation.accumulated_visual_translation.z},
          .accumulated_logical_actor_translation =
              {animation.accumulated_logical_actor_translation.x,
                  animation.accumulated_logical_actor_translation.y,
                  animation.accumulated_logical_actor_translation.z},
          .object_poses = {},
          .cin_sfx = std::nullopt,
          .pose_owner = {},
          .has_controller = false,
          .ctl_control_set = {},
          .ctl_enabled = false,
          .ctl_direct_control = false,
          .ctl_move_id = std::nullopt,
          .ctl_move_name = {},
          .ctl_state_id = std::nullopt,
          .ctl_animation_key = {},
          .ctl_previous_progress = 0.0F,
          .ctl_current_progress = 0.0F,
          .ctl_effective_end = 0.0F,
          .ctl_input_mask = 0U,
          .ctl_transition_pending = false,
          .ctl_pending_ticks = 0U,
          .ctl_callback_queue_size = 0U,
          .ctl_restart_count = 0U,
          .ctl_markers_fired = 0U};
      debug_character.physical_candidate_translation = {
          character.physical_motion.candidate_translation.x,
          character.physical_motion.candidate_translation.y,
          character.physical_motion.candidate_translation.z};
      debug_character.physical_accepted_translation = {
          character.physical_motion.accepted_translation.x,
          character.physical_motion.accepted_translation.y,
          character.physical_motion.accepted_translation.z};
      debug_character.physical_state_initialized = character.physical_motion.initialized;
      const Character::PhysicalMotionState& physical{character.physical_motion};
      debug_character.physical_vertical_velocity = physical.vertical_velocity;
      debug_character.physical_gravity_delta_per_tick = physical.gravity_velocity_delta_per_tick;
      debug_character.physical_support_valid = physical.support.valid;
      debug_character.physical_support_object_index = physical.support.object_index;
      debug_character.physical_support_point = {
          physical.support.point.x, physical.support.point.y, physical.support.point.z};
      debug_character.physical_support_normal = {
          physical.support.normal.x, physical.support.normal.y, physical.support.normal.z};
      debug_character.physical_support_clearance = physical.support.clearance;
      debug_character.physical_support_gap = physical.support.gap;
      debug_character.physical_support_walkable = physical.support.walkable;
      debug_character.physical_grounded = physical.support.grounded;
      debug_character.physical_support_special_deferred = physical.support.special_deferred;
      debug_character.physical_small_step_snapped_this_tick =
          physical.support.small_step_snapped_this_tick;
      debug_character.physical_fall_stage = physical.fall_stage;
      debug_character.physical_accumulated_fall_travel = physical.accumulated_fall_travel;
      debug_character.physical_maximum_support_gap = physical.maximum_support_gap;
      if (physical.support.object_index.has_value() && world_context->decor_model.has_value() &&
          physical.support.object_index.value() < world_context->decor_model->meshes.size()) {
        debug_character.physical_support_object_name =
            world_context->decor_model->meshes.at(physical.support.object_index.value()).name;
      }
      const CinSfxPlayback* cin_sfx{nullptr};
      for (const CinSfxPlayback& playback : world_context->runtime->cin_sfx_playbacks()) {
        if (playback.character_body_identity == character.body_identity &&
            playback.animation_index == animation.animation_descriptor_index &&
            (cin_sfx == nullptr ||
                playback.last_service_sequence > cin_sfx->last_service_sequence)) {
          cin_sfx = &playback;
        }
      }
      if (cin_sfx != nullptr) {
        const auto channel_debug = [](const CinSfxChannelPlayback& channel) {
          return Debug::CinSfxChannelDebugState{.enabled = channel.enabled,
              .active = channel.active,
              .in_window = channel.in_window,
              .definition_id = channel.definition_id,
              .definition_name = channel.definition_name,
              .object_reference = channel.object_reference,
              .resolved_object_index = channel.resolved_object_index,
              .resolved_object_name = channel.resolved_object_name,
              .resolved_object_script_id = channel.resolved_object_script_id,
              .start = channel.start,
              .end = channel.end,
              .elapsed = channel.elapsed,
              .cached_position = {channel.cached_position.x,
                  channel.cached_position.y,
                  channel.cached_position.z},
              .emissions_this_execution = channel.emissions_this_execution,
              .attachment_missing = channel.attachment_missing};
        };
        debug_character.cin_sfx =
            Debug::CinSfxPlaybackDebugState{.script_instance_id = cin_sfx->script_instance_id,
                .animation_index = cin_sfx->animation_index,
                .animation_id = cin_sfx->animation_id,
                .animation_name = cin_sfx->animation_name,
                .association_record_index = cin_sfx->association_record_index,
                .association_id = cin_sfx->association_id,
                .body_previous_progress = cin_sfx->body_previous_progress,
                .body_current_progress = cin_sfx->body_current_progress,
                .channels = {channel_debug(cin_sfx->channels.at(0)),
                    channel_debug(cin_sfx->channels.at(1))}};
      }
      switch (character.pose_owner) {
        case Character::PoseOwner::k_model_defaults:
          debug_character.pose_owner = "model defaults";
          break;
        case Character::PoseOwner::k_script_animation:
          debug_character.pose_owner = "scripted body animation";
          break;
        case Character::PoseOwner::k_ctl_controller:
          debug_character.pose_owner = "CTL controller";
          break;
      }
      debug_character.ctl_enabled = character.controller_enabled;
      if (character.ctl_controller.has_value()) {
        const Character::CtlController& controller{character.ctl_controller.value()};
        debug_character.has_controller = true;
        debug_character.ctl_control_set = controller.resource_name();
        debug_character.ctl_direct_control = controller.direct_control_active();
        if (controller.current_move() != nullptr) {
          debug_character.ctl_move_id = controller.current_move()->move_id;
          debug_character.ctl_move_name = controller.current_move()->name;
        }
        if (controller.current_state() != nullptr) {
          debug_character.ctl_state_id = controller.current_state()->state_id;
          debug_character.ctl_animation_key = controller.current_state()->animation_key;
        }
        debug_character.ctl_previous_progress = controller.previous_progress();
        debug_character.ctl_current_progress = controller.current_progress();
        debug_character.ctl_effective_end = controller.effective_animation_end();
        debug_character.ctl_input_mask = controller.current_input();
        debug_character.ctl_transition_pending = controller.transition_pending();
        debug_character.ctl_pending_ticks = controller.pending_ticks();
        debug_character.ctl_callback_queue_size = controller.callback_queue_size();
        debug_character.ctl_restart_count = controller.same_state_restart_count();
        debug_character.ctl_markers_fired = controller.markers_fired_this_execution();
      }
      if (model_resource != nullptr) {
        const Omikron::Model3DOData& model{model_resource->model};
        for (std::size_t index{0};
            index < character.object_poses.size() && index < model.meshes.size() &&
            index < character.runtime_objects.size();
            ++index) {
          const Character::BodyAnimationObjectPose& pose{character.object_poses.at(index)};
          const Omikron::Model3DOData::RuntimeObjectState& object{
              character.runtime_objects.at(index)};
          const std::optional<Runtime::Transform> presentation{
              character.object_world_transform(index)};
          debug_character.object_poses.push_back(Debug::RuntimeCharacterObjectPoseDebugState{
              .object_name = model.meshes.at(index).name,
              .script_id = model.meshes.at(index).script_id,
              .channel_bound = pose.channel_id.has_value(),
              .channel_id = pose.channel_id.value_or(0),
              .channel_name = pose.channel_name,
              .quaternion = {pose.current_quaternion.w,
                  pose.current_quaternion.x,
                  pose.current_quaternion.y,
                  pose.current_quaternion.z},
              .local_offset = {object.local_offset.x, object.local_offset.y, object.local_offset.z},
              .model_translation = {object.world_translation.x,
                  object.world_translation.y,
                  object.world_translation.z},
              .presentation_translation = presentation.has_value()
                                              ? std::array<float, 3>{presentation->translation.x,
                                                    presentation->translation.y,
                                                    presentation->translation.z}
                                              : std::array<float, 3>{},
              .local_matrix = object.animation_matrix.value_or(object.local_matrix).values,
              .world_matrix = object.world_matrix.values});
        }
      }
      state.runtime_characters.push_back(std::move(debug_character));
    }
  }

  state.camera_has_pose = m_camera.has_pose();
  state.camera_scripted = m_camera.has_scripted_pose();
  state.camera_transitioning = m_camera.transitioning();
  state.camera_id = m_camera.active_camera_id();
  state.letterbox_requested = m_letterbox.requested();
  state.letterbox_amount = m_letterbox.amount();
  state.letterbox_transitioning = m_letterbox.transitioning();
  state.viewport_width = m_width;
  state.viewport_height = m_height;
  state.letterbox_target_bar_height = WorldLetterboxState::target_bar_height(
      static_cast<float>(m_width), static_cast<float>(m_height));
  state.letterbox_current_bar_height =
      m_letterbox.current_bar_height(static_cast<float>(m_width), static_cast<float>(m_height));

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

std::optional<Debug::SpriteRenderDebugState> WorldScene::sprite_render_debug_state() const {
  if (m_world_renderer == nullptr) {
    return std::nullopt;
  }
  return m_world_renderer->sprite_render_debug_state();
}

std::optional<std::array<float, 3>> WorldScene::sprite_debug_focus_position() const {
  if (!m_camera.has_pose()) {
    return std::nullopt;
  }
  const Runtime::Vec3 target{m_camera.pose().target};
  return std::array<float, 3>{target.x, target.y, target.z};
}

bool WorldScene::sprite_grayscale_supported() const {
  return m_world_renderer != nullptr;
}

bool WorldScene::sprite_grayscale_enabled() const {
  return m_world_renderer != nullptr && m_world_renderer->sprite_grayscale();
}

void WorldScene::set_sprite_grayscale_enabled(const bool enabled) {
  if (m_world_renderer != nullptr) {
    m_world_renderer->set_sprite_grayscale(enabled);
  }
}

bool WorldScene::geometry_wireframe_enabled() const {
  return m_geometry_wireframe_enabled;
}

void WorldScene::set_geometry_wireframe_enabled(const bool enabled) {
  m_geometry_wireframe_enabled = enabled;
  if (m_world_renderer != nullptr) {
    m_world_renderer->set_geometry_wireframe(enabled);
  }
}

void WorldScene::synchronize_presentation_reset() {
  if (m_scenarios == nullptr) {
    return;
  }
  if (m_presentation_reset_observer.synchronize(
          m_scenarios->world_presentation().reset_generation(), m_fade, m_letterbox)) {
    App::Log::debug(LogCategory::Renderer,
        "WorldScene: reset session-global fade and letterbox presentation for epoch {}",
        m_presentation_reset_observer.observed_generation());
  }
}

void WorldScene::consume_fade_commands() {
  if (m_scenarios == nullptr) {
    return;
  }

  while (std::optional<WorldFadeCommand> command{m_scenarios->world_presentation().take_fade()}) {
    if (!m_fade.apply_command(command.value())) {
      App::Log::debug(LogCategory::Renderer,
          "WorldScene: ignored presentation fade mode {} while mode {} is active",
          command->mode,
          m_fade.mode());
      continue;
    }
    App::Log::debug(LogCategory::Renderer,
        "presentation fade-{} color=#{:06X} duration={} delay={}",
        command->mode == 1U ? "in" : "out",
        command->color & 0x00FFFFFFU,
        command->duration_units,
        command->delay_units);
  }
}

void WorldScene::consume_letterbox_commands() {
  if (m_scenarios == nullptr) {
    return;
  }

  while (std::optional<WorldLetterboxCommand> command{
      m_scenarios->world_presentation().take_letterbox()}) {
    static_cast<void>(m_letterbox.apply_command(*command));
  }
}

void WorldScene::consume_object_presentation_commands(const WorldSceneContext* const context) {
  if (m_scenarios == nullptr || context == nullptr) {
    return;
  }
  while (std::optional<WorldVoiceOverCommand> command{
      m_scenarios->world_presentation().take_voice_over()}) {
    if (command->scene_id != context->scene_id ||
        command->scene_generation != context->generation) {
      App::Log::debug(LogCategory::Audio,
          "WorldScene: discarded stale OBJECTS voice-over for scene={} generation={}",
          command->scene_id,
          command->scene_generation);
      continue;
    }
    Audio::AudioSystem* const audio{
        context->runtime == nullptr ? nullptr : context->runtime->audio_system()};
    if (audio == nullptr) {
      App::Log::warn(LogCategory::Audio,
          "OBJECTS {} voice-over '{}' requested without audio",
          command->object_id,
          command->audio_path);
      continue;
    }
    if (auto played{audio->play_voice_over(command->audio_path)}; !played) {
      App::Log::warn(LogCategory::Audio,
          "OBJECTS {} voice-over '{}' unavailable: {}",
          command->object_id,
          command->audio_path,
          played.error());
    }
  }
  while (std::optional<WorldTextCommand> command{
      m_scenarios->world_presentation().take_world_text()}) {
    const bool applied{
        m_world_text.apply_command(*command, context->scene_id, context->generation)};
    if (!applied) {
      App::Log::debug(LogCategory::Renderer,
          "WorldScene: discarded stale OBJECTS world text for scene={} generation={}",
          command->scene_id,
          command->scene_generation);
    }
  }
}

bool WorldScene::update_dialog_input(const float delta_time, const Input::InputManager& input) {
  if (m_scenarios == nullptr) {
    return false;
  }
  Dialog::DialogRuntime& runtime{m_scenarios->dialog_runtime()};
  if (!runtime.active()) {
    m_dialog_observed = false;
    m_selected_dialog_choice = 0;
    m_dialog_scroll.reset();
    return false;
  }

  const auto presentation{runtime.presentation()};
  if (!presentation.has_value()) {
    return true;
  }
  if (!m_dialog_observed || m_observed_dialog_generation != runtime.generation()) {
    m_dialog_observed = true;
    m_observed_dialog_generation = runtime.generation();
    m_selected_dialog_choice = 0;
    static_cast<void>(m_dialog_scroll.observe_generation(runtime.generation()));
    App::Log::debug(LogCategory::Scenario,
        "Dialog node {}: face='{}' line cameras={}/{} response cameras={}/{}",
        presentation->node_id,
        presentation->face_motion_resource,
        presentation->line_cameras.authored_ids.at(0),
        presentation->line_cameras.authored_ids.at(1),
        presentation->response_cameras.authored_ids.at(0),
        presentation->response_cameras.authored_ids.at(1));
    return true;  // Arm input on the frame after a new presentation appears.
  }

  if (presentation->state == Dialog::DialogState::k_waiting_for_choice) {
    if (!presentation->choices.empty()) {
      if (input.is_action_pressed(Input::Action::k_menu_up)) {
        m_selected_dialog_choice = m_selected_dialog_choice == 0U
                                       ? presentation->choices.size() - 1U
                                       : m_selected_dialog_choice - 1U;
      }
      if (input.is_action_pressed(Input::Action::k_menu_down)) {
        m_selected_dialog_choice = (m_selected_dialog_choice + 1U) % presentation->choices.size();
      }
      if (input.is_action_pressed(Input::Action::k_menu_confirm)) {
        const std::size_t slot{presentation->choices.at(m_selected_dialog_choice).slot};
        if (auto selected{runtime.select_choice(slot)}; !selected) {
          App::Log::error(LogCategory::Scenario, "Dialog response failed: {}", selected.error());
        }
      }
    }
    return true;
  }

  m_dialog_scroll.update(delta_time,
      input.get_action_value(Input::Action::k_menu_up) > 0.5F,
      input.get_action_value(Input::Action::k_menu_down) > 0.5F);

  if (input.is_action_pressed(Input::Action::k_menu_confirm)) {
    if (auto acknowledged{runtime.acknowledge_line()}; !acknowledged) {
      App::Log::error(LogCategory::Scenario, "Dialog advance failed: {}", acknowledged.error());
    }
  }
  return true;
}

void WorldScene::update(const float delta_time, const Input::InputManager& input) {
  APP_PROFILE_FUNCTION();

  // Session reset is observed before any freshly queued commands are drained.
  // Active-world changes below intentionally do not touch these global states.
  synchronize_presentation_reset();

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
        m_world_text.reset();
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
          m_world_renderer->set_geometry_wireframe(m_geometry_wireframe_enabled);
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
      m_world_text.reset();
      m_world_observed = false;
    }
  }

  // Camera commands remain scoped to the exact active world generation.
  if (m_scenarios != nullptr) {
    while (std::optional<WorldCameraCommand> command{
        m_scenarios->world_presentation().take_camera()}) {
      if (context == nullptr || command->scene_id != context->scene_id ||
          command->scene_generation != context->generation) {
        if (command->kind == WorldCameraCommandKind::k_controller_mode) {
          App::Log::debug(LogCategory::Renderer,
              "WorldScene: discarded stale camera controller {} for scene={} generation={}",
              command->controller_mode,
              command->scene_id,
              command->scene_generation);
        } else {
          App::Log::debug(LogCategory::Renderer,
              "WorldScene: discarded stale camera {} for scene={} generation={}",
              command->camera_id,
              command->scene_id,
              command->scene_generation);
        }
        continue;
      }
      m_camera.apply_command(command.value());
      if (command->kind == WorldCameraCommandKind::k_controller_mode) {
        App::Log::debug(LogCategory::Renderer,
            "World camera controller {} — duration={}",
            command->controller_mode,
            command->duration_units);
        continue;
      }
      if (command->source_area_id == 222) {
        if (command->camera_id == 4290U) {
          App::Log::info(LogCategory::Scenario, "AREA 222 sequence: 4290 applied");
        } else if (command->camera_id == 4291U || command->camera_id == 4292U) {
          App::Log::info(
              LogCategory::Scenario, "AREA 222 sequence: {} started", command->camera_id);
        }
      }
      App::Log::debug(LogCategory::Renderer,
          "World camera {} — duration={} flags={} roll={}deg hFov={}deg selectors=({}, {}) "
          "participants=({}, {})",
          command->camera_id,
          command->duration_units,
          command->flags,
          command->roll_degrees,
          command->horizontal_fov_degrees,
          command->target_attachment_selector,
          command->eye_attachment_selector,
          command->attachment_participants.participant_a_character_id,
          command->attachment_participants.participant_b_character_id);
    }
  }

  consume_fade_commands();
  consume_letterbox_commands();
  consume_object_presentation_commands(context);
  m_fade.update(delta_time);
  m_letterbox.update(delta_time);
  m_world_text.update(delta_time);
  m_uv_phases.update(delta_time);

  m_camera.update(delta_time);
  if (std::optional<WorldCameraOperationCompletion> completed{m_camera.take_completed_operation()};
      completed.has_value() && m_scenarios != nullptr) {
    if (completed->source_area_id == 222 &&
        (completed->camera_id == 4291U || completed->camera_id == 4292U)) {
      App::Log::info(
          LogCategory::Scenario, "AREA 222 sequence: {} completed", completed->camera_id);
    }
    m_scenarios->world_presentation().enqueue_camera_completion(completed.value());
  }

  // Runtime's structured camera is frame-published: ownership of controller
  // mode 13 logically releases to mode 0 (automatic player camera; the actual
  // follow mathematics are Phase 4.3) only when no structured script
  // republished a camera this frame AND the legacy [Preferences]
  // autocameraplayer gate is enabled AND a current player character exists.
  // The renderer keeps the last valid pose as a presentation fallback inside
  // release_structured_controller.
  if (m_camera.active_controller_mode() == 13U) {
    const ScenarioRuntime* const runtime{
        context == nullptr || context->runtime == nullptr ? nullptr : context->runtime.get()};
    const bool structured_published{
        runtime != nullptr && runtime->selected_structured_camera() != nullptr};
    const bool player_character_exists{
        m_scenarios != nullptr && m_scenarios->controlled_character().has_value()};
    if (m_camera.should_release_structured_controller(
            structured_published, player_character_exists)) {
      m_camera.release_structured_controller();
      App::Log::debug(LogCategory::Renderer,
          "structured camera source ended — camera controller released to mode 0");
    }
  }

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

  if (update_dialog_input(delta_time, input)) {
    m_interfaces.update_without_input(delta_time);
  } else {
    m_interfaces.update(delta_time, input);
  }
}

void WorldScene::render() {
  APP_PROFILE_FUNCTION();

  if (m_width <= 0 || m_height <= 0 || m_color_pipeline == nullptr) {
    return;
  }
  if (auto targets{m_color_pipeline->ensure_targets(m_width, m_height)}; !targets) {
    if (m_color_pipeline_error != targets.error()) {
      m_color_pipeline_error = targets.error();
      App::Log::error(
          LogCategory::Renderer, "World color pipeline unavailable: {}", targets.error());
    }
    return;
  }
  m_color_pipeline_error.clear();

  // Canonical world color begins and remains scene-linear RGBA16F. The encoded
  // clear setting is decoded once at this boundary.
  m_color_pipeline->begin_scene(Renderer::clear_color());

  // Scenario execution happens after WorldScene::update() in the frame, so
  // consume newly-emitted presentation commands again here. This lets opcode
  // 0x77 cover the first world frame and 0x84/0x85 reverse without an extra
  // display-frame delay. Render never advances either transition clock.
  synchronize_presentation_reset();
  const WorldSceneContext* context{
      m_scenarios != nullptr ? m_scenarios->active_world_context() : nullptr};
  consume_fade_commands();
  consume_letterbox_commands();
  consume_object_presentation_commands(context);

  // A world context may be replaced between update and render; never
  // dereference a runtime cached by the renderer. If generation no longer
  // matches, skip one world frame and rebuild on the next update.
  const bool world_renderable{m_world_renderer != nullptr && context != nullptr &&
                              m_world_observed && context->scene_id == m_observed_scene_id &&
                              context->generation == m_observed_generation};
  if (world_renderable) {
    m_world_renderer->render(m_camera.camera(),
        context->runtime.get(),
        static_cast<float>(m_uv_phases.u_phase()),
        static_cast<float>(m_uv_phases.v_phase()),
        *m_color_pipeline);
  }

  // OpenNomad-native world diagnostics are linear scene content and reuse the
  // same depth attachment as both scene ping-pong targets.
  m_color_pipeline->bind_current_scene();
  if (world_renderable) {
    m_world_renderer->render_debug_overlay(m_camera.camera(), context->runtime.get());
  }

  // Explicit SDR clamp and OETF. Presentation overlays below intentionally
  // remain outside HDR scene processing and compose in encoded display space.
  m_color_pipeline->present_linear();

  if (m_fade_renderer != nullptr && m_fade.alpha() > 0.0F) {
    m_fade_renderer->render(m_fade.color(), m_fade.alpha());
  }

  // The cinematic mask remains opaque during presentation fades, matching
  // Runtime's layer order: the whiteout affects the world, not the bars.
  if (m_letterbox_renderer != nullptr && m_letterbox.amount() > 0.0F) {
    m_letterbox_renderer->render(
        m_letterbox.current_bar_height(static_cast<float>(m_width), static_cast<float>(m_height)),
        static_cast<float>(m_height));
  }

  // I2D is always the final scene layer. Interface 29's full-screen bump
  // background therefore covers the world while the main menu is active,
  // exactly as the stable WorldScene architecture intends.
  m_interfaces.render(m_width, m_height);
  if (m_world_text.active()) {
    const Interface::RuntimeTextDocument* document{m_world_text.document()};
    if (document != nullptr) {
      m_interfaces.render_world_text(
          *document, m_world_text.presentation_time_ms(), m_width, m_height);
    }
  }
  if (m_scenarios != nullptr) {
    if (const auto dialog{m_scenarios->dialog_runtime().presentation()}; dialog.has_value()) {
      const std::size_t selected{dialog->choices.empty() ? 0U
                                                         : std::min(m_selected_dialog_choice,
                                                               dialog->choices.size() - 1U)};
      const float maximum_scroll{m_interfaces.render_dialog(
          dialog.value(), selected, m_dialog_scroll.offset(), m_width, m_height)};
      m_dialog_scroll.set_maximum(maximum_scroll);
    }
  }
}

void WorldScene::resize(const int width, const int height) {
  m_width = width;
  m_height = height;
  if (width > 0 && height > 0) {
    m_camera.set_aspect_ratio(static_cast<float>(width) / static_cast<float>(height));
  }
}

void WorldScene::post_scenario_update(const float /* delta_time */) {
  // Post-scenario update (after scenario frame). Currently no action needed;
  // world camera operations and presentation update already completed in update().
}

}  // namespace App
