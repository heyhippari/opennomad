#include "ModelScene.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include <glad/glad.h>

// NOLINTBEGIN(misc-include-cleaner)
// glm follows a "single-include" convention — the umbrella headers are the
// canonical way to pull in the library, even though clang-tidy cannot trace
// individual symbols back to a direct sub-header.
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Framebuffer.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/Reflection.hpp"
#include "Core/Resources.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/RuntimePresentation.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/TextureCube.hpp"

namespace App {

namespace {

constexpr std::string_view K_VERTEX_SHADER_SOURCE{R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform mat3 u_normal_matrix;
uniform vec4 u_clip_plane;  // Cuts everything behind the plane (mirror pass).

out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;
out vec3 v_world_position;
out float gl_ClipDistance[1];

void main() {
    vec4 world = u_model * vec4(a_position, 1.0);
    gl_ClipDistance[0] = dot(world, u_clip_plane);
    v_normal = u_normal_matrix * a_normal;
    v_uv = a_uv;
    v_color = a_color;
    v_world_position = world.xyz;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)glsl"};

constexpr std::string_view K_FRAGMENT_SHADER_SOURCE{R"glsl(
#version 410 core
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;
in vec3 v_world_position;

uniform sampler2D u_texture0;
uniform vec3 u_light_direction;  // Legacy fallback when the model has no lights.
uniform float u_ambient;
uniform float u_lights_enabled;  // 0.0 renders the flat ambient tint only.
uniform float u_vertex_color;  // 1.0 for pre-lit meshes, 0.0 otherwise.
uniform float u_alpha_test;    // 1.0 discards fragments below the alpha threshold.
uniform float u_alpha_blend;   // 1.0 outputs the texture alpha (blended meshes).

struct LightData {
    vec4 position;   // xyz = world position, w = attenuation start.
    vec4 direction;  // xyz = unit spot direction, w = attenuation end.
    vec4 color;      // rgb = colour * intensity * scale.
    vec4 spot;       // x = cos hotspot, y = cos falloff, z = point-light flag.
};

layout(std140) uniform LightBlock {
    int u_light_count;
    LightData u_lights[255];
} light_block;

vec3 apply_lighting(vec3 normal, vec3 world_position) {
    if (u_lights_enabled < 0.5) {
        return vec3(u_ambient);
    }
    vec3 total = vec3(u_ambient);
    if (light_block.u_light_count <= 0) {
        total += (1.0 - u_ambient) * max(dot(normal, normalize(u_light_direction)), 0.0);
        return total;
    }
    for (int light_index = 0; light_index < light_block.u_light_count; ++light_index) {
        LightData light = light_block.u_lights[light_index];
        vec3 offset = light.position.xyz - world_position;
        float distance = length(offset);
        vec3 to_light = offset / max(distance, 0.0001);
        float diffuse = max(dot(normal, to_light), 0.0);
        float attenuation = 1.0;
        if (light.direction.w > light.position.w) {
            attenuation = clamp(
                (light.direction.w - distance) / (light.direction.w - light.position.w),
                0.0, 1.0);
        }
        float spot = 1.0;
        if (light.spot.z < 0.5) {
            float facing = dot(-to_light, light.direction.xyz);
            spot = smoothstep(light.spot.y, light.spot.x, facing);
        }
        total += light.color.rgb * (spot * attenuation * diffuse);
    }
    return total;
}

out vec4 frag_colour;

void main() {
    vec3 normal = normalize(v_normal);
    vec4 texel = texture(u_texture0, v_uv);
    vec3 colour = texel.rgb * mix(vec3(1.0), v_color.rgb, u_vertex_color);
    colour *= apply_lighting(normal, v_world_position);
    if (u_alpha_test > 0.5 && texel.a < 0.5) {
        discard;
    }
    frag_colour = vec4(colour, mix(1.0, texel.a, u_alpha_blend));
}
)glsl"};

/// True for blend modes that composite with the framebuffer instead of
/// writing depth-tested opaque fragments.
constexpr bool is_blended(const Omikron::BlendMode mode) {
  return mode != Omikron::BlendMode::k_opaque && mode != Omikron::BlendMode::k_alpha_test;
}

/// Vertex shader for environment-mapped meshes: like the main shader but
/// also passes the world-space position to the fragment stage.
constexpr std::string_view K_ENV_VERTEX_SHADER_SOURCE{R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform mat3 u_normal_matrix;
uniform vec4 u_clip_plane;

out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;
out vec3 v_world_position;
out float gl_ClipDistance[1];

void main() {
    vec4 world = u_model * vec4(a_position, 1.0);
    gl_ClipDistance[0] = dot(world, u_clip_plane);
    v_world_position = world.xyz;
    v_normal = u_normal_matrix * a_normal;
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)glsl"};

/// Chrome-like environment mapping: a fresnel blend of the diffuse surface
/// colour with the reflected sky cube map.
constexpr std::string_view K_ENV_FRAGMENT_SHADER_SOURCE{R"glsl(
#version 410 core
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;
in vec3 v_world_position;

uniform sampler2D u_texture0;
uniform samplerCube u_texture_cube;
uniform vec3 u_camera_position;
uniform vec3 u_light_direction;
uniform float u_ambient;
uniform float u_lights_enabled;  // 0.0 renders the flat ambient tint only.
uniform float u_vertex_color;

struct LightData {
    vec4 position;   // xyz = world position, w = attenuation start.
    vec4 direction;  // xyz = unit spot direction, w = attenuation end.
    vec4 color;      // rgb = colour * intensity * scale.
    vec4 spot;       // x = cos hotspot, y = cos falloff, z = point-light flag.
};

layout(std140) uniform LightBlock {
    int u_light_count;
    LightData u_lights[255];
} light_block;

vec3 apply_lighting(vec3 normal, vec3 world_position) {
    if (u_lights_enabled < 0.5) {
        return vec3(u_ambient);
    }
    vec3 total = vec3(u_ambient);
    if (light_block.u_light_count <= 0) {
        total += (1.0 - u_ambient) * max(dot(normal, normalize(u_light_direction)), 0.0);
        return total;
    }
    for (int light_index = 0; light_index < light_block.u_light_count; ++light_index) {
        LightData light = light_block.u_lights[light_index];
        vec3 offset = light.position.xyz - world_position;
        float distance = length(offset);
        vec3 to_light = offset / max(distance, 0.0001);
        float diffuse = max(dot(normal, to_light), 0.0);
        float attenuation = 1.0;
        if (light.direction.w > light.position.w) {
            attenuation = clamp(
                (light.direction.w - distance) / (light.direction.w - light.position.w),
                0.0, 1.0);
        }
        float spot = 1.0;
        if (light.spot.z < 0.5) {
            float facing = dot(-to_light, light.direction.xyz);
            spot = smoothstep(light.spot.y, light.spot.x, facing);
        }
        total += light.color.rgb * (spot * attenuation * diffuse);
    }
    return total;
}

out vec4 frag_colour;

void main() {
    vec3 normal = normalize(v_normal);
    vec4 texel = texture(u_texture0, v_uv);
    vec3 surface = texel.rgb * mix(vec3(1.0), v_color.rgb, u_vertex_color);
    surface *= apply_lighting(normal, v_world_position);

    vec3 view_direction = normalize(u_camera_position - v_world_position);
    vec3 reflection = reflect(-view_direction, normal);
    vec3 environment = texture(u_texture_cube, reflection).rgb;

    float fresnel = 0.04 + (0.96 * pow(1.0 - max(dot(normal, view_direction), 0.0), 5.0));
    frag_colour = vec4(mix(surface, environment, fresnel), 1.0);
}
)glsl"};

/// Mirrors share the main vertex shader; the fragment stage composites the
/// surface texture with the reflection buffer via screen-space UVs.
constexpr std::string_view K_MIRROR_FRAGMENT_SHADER_SOURCE{R"glsl(
#version 410 core
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;
in vec3 v_world_position;

uniform sampler2D u_texture0;   // Surface texture.
uniform sampler2D u_texture1;   // Reflection buffer.
uniform vec2 u_viewport_size;
uniform float u_mirror_mode;    // 0 = pure, 1 = additive, 2 = subtractive.
uniform vec3 u_light_direction;
uniform float u_ambient;
uniform float u_lights_enabled;  // 0.0 renders the flat ambient tint only.
uniform float u_vertex_color;

struct LightData {
    vec4 position;   // xyz = world position, w = attenuation start.
    vec4 direction;  // xyz = unit spot direction, w = attenuation end.
    vec4 color;      // rgb = colour * intensity * scale.
    vec4 spot;       // x = cos hotspot, y = cos falloff, z = point-light flag.
};

layout(std140) uniform LightBlock {
    int u_light_count;
    LightData u_lights[255];
} light_block;

vec3 apply_lighting(vec3 normal, vec3 world_position) {
    if (u_lights_enabled < 0.5) {
        return vec3(u_ambient);
    }
    vec3 total = vec3(u_ambient);
    if (light_block.u_light_count <= 0) {
        total += (1.0 - u_ambient) * max(dot(normal, normalize(u_light_direction)), 0.0);
        return total;
    }
    for (int light_index = 0; light_index < light_block.u_light_count; ++light_index) {
        LightData light = light_block.u_lights[light_index];
        vec3 offset = light.position.xyz - world_position;
        float distance = length(offset);
        vec3 to_light = offset / max(distance, 0.0001);
        float diffuse = max(dot(normal, to_light), 0.0);
        float attenuation = 1.0;
        if (light.direction.w > light.position.w) {
            attenuation = clamp(
                (light.direction.w - distance) / (light.direction.w - light.position.w),
                0.0, 1.0);
        }
        float spot = 1.0;
        if (light.spot.z < 0.5) {
            float facing = dot(-to_light, light.direction.xyz);
            spot = smoothstep(light.spot.y, light.spot.x, facing);
        }
        total += light.color.rgb * (spot * attenuation * diffuse);
    }
    return total;
}

out vec4 frag_colour;

void main() {
    vec3 normal = normalize(v_normal);
    vec4 surface = texture(u_texture0, v_uv);
    vec3 surface_rgb = surface.rgb * mix(vec3(1.0), v_color.rgb, u_vertex_color);
    surface_rgb *= apply_lighting(normal, v_world_position);

    vec2 screen_uv = gl_FragCoord.xy / u_viewport_size;
    vec3 reflection = texture(u_texture1, screen_uv).rgb;

    vec3 colour;
    if (u_mirror_mode < 0.5) {
        colour = reflection;                        // Pure mirror.
    } else if (u_mirror_mode < 1.5) {
        colour = surface_rgb + reflection;          // Additive gloss.
    } else {
        colour = surface_rgb * (1.0 - reflection);  // Subtractive.
    }
    frag_colour = vec4(colour, 1.0);
}
)glsl"};

/// Debug overlay vertex shader: coloured points and lines sharing one MVP.
constexpr std::string_view K_OVERLAY_VERTEX_SHADER_SOURCE{R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;

uniform mat4 u_mvp;

out vec4 v_color;

void main() {
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_position, 1.0);
    gl_PointSize = 12.0;
}
)glsl"};

/// Debug overlay fragment shader: round point sprites for markers, flat
/// colour for lines. u_point_mode must be 0.0 during line draws because
/// gl_PointCoord is undefined outside point rasterisation.
constexpr std::string_view K_OVERLAY_FRAGMENT_SHADER_SOURCE{R"glsl(
#version 410 core
in vec4 v_color;

uniform float u_point_mode;  // 1.0 = round sprite, 0.0 = flat line.

out vec4 frag_colour;

void main() {
    if (u_point_mode > 0.5) {
        vec2 offset = gl_PointCoord - vec2(0.5);
        float distance_squared = dot(offset, offset);
        if (distance_squared > 0.25) {
            discard;
        }
        float edge = 1.0 - smoothstep(0.20, 0.25, distance_squared);
        frag_colour = vec4(v_color.rgb, v_color.a * edge);
    } else {
        frag_colour = v_color;
    }
}
)glsl"};

/// Mirrors composite by the raw flags: unlike alpha blending, the additive
/// and subtractive modifiers apply to k_mirror on their own.
constexpr float mirror_mode(const std::uint32_t flags) {
  if (Omikron::has_flag(flags, Omikron::MeshFlags::k_subtractive)) {
    return 2.0F;
  }
  if (Omikron::has_flag(flags, Omikron::MeshFlags::k_additive)) {
    return 1.0F;
  }
  return 0.0F;
}

/// Maximum lights in the std140 block; mirrors the GLSL array size. 255 keeps
/// the block within the GL 4.1 minimum MAX_UNIFORM_BLOCK_SIZE (16 KB):
/// 16 + 255 * 64 = 16336 bytes; 256 would exceed it.
constexpr std::size_t K_MAX_LIGHTS{255};
/// Uniform-block binding point shared by the scene's shaders.
constexpr GLuint K_LIGHT_BLOCK_BINDING{0};

/// One GPU light, matching the std140 LightData struct in the shaders.
struct RenderLight {
  std::array<GLfloat, 4> position{0.0F, 0.0F, 0.0F, 0.0F};   ///< xyz, attenuation start.
  std::array<GLfloat, 4> direction{0.0F, 0.0F, 0.0F, 0.0F};  ///< xyz, attenuation end.
  std::array<GLfloat, 4> color{0.0F, 0.0F, 0.0F, 0.0F};      ///< rgb = colour * intensity * scale.
  std::array<GLfloat, 4> spot{0.0F, 0.0F, 0.0F, 0.0F};  ///< hotspot cos, falloff cos, point flag.
};
static_assert(sizeof(RenderLight) == 64U);

/// std140 payload: the light count followed by K_MAX_LIGHTS lights.
struct LightBlockData {
  std::int32_t light_count{0};
  std::array<GLfloat, 3> padding{};  // std140 pads the int to a vec4 boundary.
  std::array<RenderLight, K_MAX_LIGHTS> lights{};
};
static_assert(offsetof(LightBlockData, lights) == 16U);
static_assert(sizeof(LightBlockData) == 16U + (K_MAX_LIGHTS * 64U));

/// Reads a whole file into memory using SDL.
std::expected<std::vector<std::byte>, std::string> read_file(const std::filesystem::path& path) {
  // Windows ignores filename case; mirror that on case-sensitive filesystems
  // so e.g. a requested "AKG_FNM.3dt" finds "akg_fnm.3DT" on disk.
  const std::filesystem::path resolved_path{Resources::resolve_case_insensitive(path)};
  std::size_t size{0};
  void* raw{SDL_LoadFile(resolved_path.string().c_str(), &size)};
  if (raw == nullptr) {
    return std::expected<std::vector<std::byte>, std::string>{std::unexpect, SDL_GetError()};
  }

  std::vector<std::byte> bytes(size);
  if (size > 0) {
    std::memcpy(bytes.data(), raw, size);
  }
  SDL_free(raw);
  return bytes;
}

/// Backdrop framing: the camera sits farther from the Anekbah level centre
/// than from a small character model.
constexpr float K_BACKDROP_CAMERA_DISTANCE{40.0F};
constexpr float K_BACKDROP_CAMERA_HEIGHT{20.0F};
/// Distance in front of the camera where spawned sprites are placed.
constexpr float K_SPRITE_CAMERA_FOCUS_DISTANCE{6.0F};

}  // namespace

std::expected<ModelScene::DecodedModel, std::string> ModelScene::load_decoded_model(
    const std::filesystem::path& model_path) {
  APP_PROFILE_FUNCTION();

  auto model_file{read_file(model_path)};
  if (!model_file) {
    return std::expected<DecodedModel, std::string>{std::unexpect,
        fmt::format("Failed to open '{}': {}. Copy the game's MESHES folder next to the "
                    "executable.",
            model_path.string(),
            model_file.error())};
  }

  auto model{Omikron::Model3DO::load(std::span<const std::byte>{model_file.value()})};
  if (!model) {
    return std::expected<DecodedModel, std::string>{std::unexpect,
        fmt::format("Failed to decode '{}': {}", model_path.string(), model.error())};
  }

  auto groups{Omikron::Model3DO::build_static_geometry(model.value())};
  if (!groups) {
    return std::expected<DecodedModel, std::string>{std::unexpect,
        fmt::format("Failed to build geometry for '{}': {}", model_path.string(), groups.error())};
  }
  if (groups->empty()) {
    App::Log::warn(LogCategory::Renderer,
        "Model '{}' contains no visible geometry; skipping GPU mesh creation",
        model_path.string());
  }

  // The .3DT texture sidecar sits next to the model.
  const std::filesystem::path texture_path{
      std::filesystem::path{model_path}.replace_extension(".3dt")};
  auto texture_file{read_file(texture_path)};
  if (!texture_file) {
    return std::expected<DecodedModel, std::string>{std::unexpect,
        fmt::format("Failed to open '{}': {}. The .3DT file must sit next to the .3DO.",
            texture_path.string(),
            texture_file.error())};
  }

  auto images{Omikron::Texture3DT::load(
      std::span<const std::byte>{texture_file.value()}, model->materials)};
  if (!images) {
    return std::expected<DecodedModel, std::string>{std::unexpect,
        fmt::format("Failed to decode '{}': {}", texture_path.string(), images.error())};
  }

  return DecodedModel{.model = std::move(model).value(),
      .images = std::move(images).value(),
      .groups = std::move(groups).value(),
      .display_name = model_path.string()};
}

std::expected<std::unique_ptr<ModelScene>, std::string> ModelScene::create() {
  APP_PROFILE_FUNCTION();

  // const std::filesystem::path model_path{Resources::game_data_path("MESHES/PERSOS/HO1_FN.3DO")};
  const std::filesystem::path model_path{Resources::game_data_path("MESHES/DECORS/Anekbah.3DO")};

  auto decoded{load_decoded_model(model_path)};
  if (!decoded) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{std::unexpect, decoded.error()};
  }

  auto scene{create_from_geometry(
      decoded->groups, decoded->model, decoded->images, decoded->display_name)};
  if (!scene) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(scene).error()};
  }
  ModelScene& scene_ref{*scene.value()};
  // Standalone model view has no scenario: attach an empty runtime so the
  // sprite/script accessors stay valid (empty pool, null script runtime).
  scene_ref.m_owned_runtime = std::make_unique<ScenarioRuntime>();
  scene_ref.m_runtime = scene_ref.m_owned_runtime.get();
  return scene;
}

std::expected<std::unique_ptr<ModelScene>, std::string> ModelScene::create_from_scx() {
  APP_PROFILE_FUNCTION();

  const std::filesystem::path scx_path{Resources::game_data_path("SCPTDATA/aventure.SCX")};

  auto scx_file{read_file(scx_path)};
  if (!scx_file) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{std::unexpect,
        fmt::format("Failed to open '{}': {}. Copy the game's SCPTDATA folder next to the "
                    "executable.",
            scx_path.string(),
            scx_file.error())};
  }

  auto scx{Omikron::SCX::load(std::span<const std::byte>{scx_file.value()})};
  if (!scx) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, fmt::format("Failed to parse '{}': {}", scx_path.string(), scx.error())};
  }
  App::Log::debug(LogCategory::Renderer,
      "'{}': {} sprites, {} waves, {} embedded models",
      scx_path.string(),
      scx->sprites.size(),
      scx->waves.size(),
      scx->models.size());

  // The static backdrop is the Anekbah level; the embedded effect models
  // become sprite resources rendered as camera-facing billboards.
  const std::filesystem::path backdrop_path{Resources::game_data_path("MESHES/DECORS/Anekbah.3DO")};
  auto backdrop{load_decoded_model(backdrop_path)};
  if (!backdrop) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{std::unexpect,
        fmt::format("Failed to load the backdrop for the sprite scene: {}", backdrop.error())};
  }

  auto scene{create_from_geometry(
      backdrop->groups, backdrop->model, backdrop->images, backdrop->display_name)};
  if (!scene) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(scene).error()};
  }
  ModelScene& scene_ref{*scene.value()};

  // The level is far larger than a character model; frame it from further out.
  scene_ref.m_camera.set_position(scene_ref.m_model_center.at(0),
      scene_ref.m_model_center.at(1) + K_BACKDROP_CAMERA_HEIGHT,
      scene_ref.m_model_center.at(2) + K_BACKDROP_CAMERA_DISTANCE);
  scene_ref.m_camera.look_at(scene_ref.m_model_center.at(0),
      scene_ref.m_model_center.at(1) + K_BACKDROP_CAMERA_HEIGHT,
      scene_ref.m_model_center.at(2));

  if (auto result{scene_ref.initialize_sprite_renderer()}; !result) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(result).error()};
  }

  auto runtime{std::make_unique<ScenarioRuntime>()};
  if (auto result{runtime->initialize(std::move(scx).value(),
          std::span<const std::byte>{scx_file.value()},
          scx_path.string(),
          /*audio=*/nullptr,
          /*activate_startup_scripts=*/true)};
      !result) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(result).error()};
  }
  runtime->set_world_anchor(Runtime::Presentation::to_gl(scene_ref.m_model_center));
  scene_ref.m_owned_runtime = std::move(runtime);
  scene_ref.m_runtime = scene_ref.m_owned_runtime.get();

  return scene;
}

std::expected<std::unique_ptr<ModelScene>, std::string> ModelScene::create_from_scenario_manager(
    ScenarioManager* manager) {
  APP_PROFILE_FUNCTION();

  if (manager == nullptr) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, "ScenarioManager is null"};
  }

  // Get the scene-independent gameplay runtime (sprite pool, script runtime)
  // built by ScenarioManager when the gameplay-mode scenario was installed.
  ScenarioRuntime* runtime{manager->gameplay_runtime()};
  if (runtime == nullptr) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, "Gameplay runtime not built"};
  }

  // The static backdrop is the Anekbah level; the embedded effect models
  // become sprite resources rendered as camera-facing billboards.
  const std::filesystem::path backdrop_path{Resources::game_data_path("MESHES/DECORS/Anekbah.3DO")};
  auto backdrop{load_decoded_model(backdrop_path)};
  if (!backdrop) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{std::unexpect,
        fmt::format("Failed to load the backdrop for the sprite scene: {}", backdrop.error())};
  }

  auto scene{create_from_geometry(
      backdrop->groups, backdrop->model, backdrop->images, backdrop->display_name)};
  if (!scene) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(scene).error()};
  }
  ModelScene& scene_ref{*scene.value()};

  // The level is far larger than a character model; frame it from further out.
  scene_ref.m_camera.set_position(scene_ref.m_model_center.at(0),
      scene_ref.m_model_center.at(1) + K_BACKDROP_CAMERA_HEIGHT,
      scene_ref.m_model_center.at(2) + K_BACKDROP_CAMERA_DISTANCE);
  scene_ref.m_camera.look_at(scene_ref.m_model_center.at(0),
      scene_ref.m_model_center.at(1) + K_BACKDROP_CAMERA_HEIGHT,
      scene_ref.m_model_center.at(2));

  // Initialize sprites from the shared gameplay runtime and wire it in.
  if (auto result{scene_ref.initialize_sprite_renderer()}; !result) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(result).error()};
  }
  runtime->set_world_anchor(Runtime::Presentation::to_gl(scene_ref.m_model_center));
  scene_ref.m_runtime = runtime;

  return scene;
}

std::expected<std::unique_ptr<ModelScene>, std::string> ModelScene::create_from_geometry(
    const std::vector<Omikron::MaterialGroup>& groups,
    const Omikron::Model3DOData& model,
    const std::vector<Omikron::Texture3DTImage>& images,
    const std::string_view display_name) {
  APP_PROFILE_FUNCTION();

  App::Log::debug(LogCategory::Renderer, "Building render-ready scene for '{}'", display_name);

  // Model3DO geometry is authoritative Runtime-native data. This legacy
  // source mirror uses the same presentation adapter as the active viewer.
  std::vector<Omikron::MaterialGroup> presentation_groups{groups};
  for (Omikron::MaterialGroup& group : presentation_groups) {
    for (Vertex& vertex : group.vertices) {
      vertex = Runtime::Presentation::to_gl(vertex);
    }
  }

  std::size_t vertex_count{0};
  float min_x{std::numeric_limits<float>::max()};
  float min_y{std::numeric_limits<float>::max()};
  float min_z{std::numeric_limits<float>::max()};
  float max_x{std::numeric_limits<float>::lowest()};
  float max_y{std::numeric_limits<float>::lowest()};
  float max_z{std::numeric_limits<float>::lowest()};
  for (const Omikron::MaterialGroup& group : presentation_groups) {
    vertex_count += group.vertices.size();
    for (const Vertex& vertex : group.vertices) {
      min_x = std::min(min_x, vertex.position.at(0));
      min_y = std::min(min_y, vertex.position.at(1));
      min_z = std::min(min_z, vertex.position.at(2));
      max_x = std::max(max_x, vertex.position.at(0));
      max_y = std::max(max_y, vertex.position.at(1));
      max_z = std::max(max_z, vertex.position.at(2));
    }
  }
  App::Log::info(LogCategory::Renderer,
      "Loaded '{}': {} meshes, {} vertices, {} materials",
      display_name,
      model.meshes.size(),
      vertex_count,
      model.materials.size());
  App::Log::debug(LogCategory::Renderer,
      "Model bounds: x [{}, {}], y [{}, {}], z [{}, {}]",
      min_x,
      max_x,
      min_y,
      max_y,
      min_z,
      max_z);

  // The geometry stays in world space; the camera frames the model at
  // startup and the player then flies around it freely.
  const std::array<float, 3> model_center{
      (min_x + max_x) / 2.0F, (min_y + max_y) / 2.0F, (min_z + max_z) / 2.0F};

  // Sanity check: the stored normals should agree with the geometric winding
  // of the emitted triangles (dot(cross(e1, e2), normal) > 0).
  std::size_t face_count{0};
  std::size_t normal_mismatches{0};
  for (const Omikron::MaterialGroup& group : presentation_groups) {
    for (std::size_t index{0}; index + 2U < group.indices.size(); index += 3U) {
      const std::array<float, 3>& first{group.vertices.at(group.indices.at(index)).position};
      const std::array<float, 3>& second{group.vertices.at(group.indices.at(index + 1U)).position};
      const std::array<float, 3>& third{group.vertices.at(group.indices.at(index + 2U)).position};
      const float edge1_x{second.at(0) - first.at(0)};
      const float edge1_y{second.at(1) - first.at(1)};
      const float edge1_z{second.at(2) - first.at(2)};
      const float edge2_x{third.at(0) - first.at(0)};
      const float edge2_y{third.at(1) - first.at(1)};
      const float edge2_z{third.at(2) - first.at(2)};
      const float normal_x{(edge1_y * edge2_z) - (edge1_z * edge2_y)};
      const float normal_y{(edge1_z * edge2_x) - (edge1_x * edge2_z)};
      const float normal_z{(edge1_x * edge2_y) - (edge1_y * edge2_x)};
      const std::array<float, 3>& stored{group.vertices.at(group.indices.at(index)).normal};
      if (((normal_x * stored.at(0)) + (normal_y * stored.at(1)) + (normal_z * stored.at(2))) <
          0.0F) {
        ++normal_mismatches;
      }
      ++face_count;
    }
  }
  App::Log::debug(LogCategory::Renderer,
      "Normal consistency: {} of {} faces have outward normals",
      face_count - normal_mismatches,
      face_count);

  // Upload one texture per material.
  std::vector<Texture2D> textures;
  textures.reserve(images.size());
  for (std::size_t index{0}; index < images.size(); ++index) {
    const Omikron::Texture3DTImage& image{images.at(index)};
    if (image.width == 0U || image.height == 0U) {
      App::Log::warn(LogCategory::Renderer,
          "Texture of material '{}' has zero size; using a white fallback",
          model.materials.at(index).name);
      static constexpr std::array<std::uint8_t, 4> k_white_pixel{255, 255, 255, 255};
      auto fallback{Texture2D::create(1,
          1,
          std::span<const std::uint8_t>{k_white_pixel},
          k_retail_texture_policy.encoding,
          k_retail_texture_policy.filter)};
      if (!fallback) {
        return std::expected<std::unique_ptr<ModelScene>, std::string>{
            std::unexpect, std::move(fallback).error()};
      }
      textures.push_back(std::move(fallback).value());
      continue;
    }
    auto texture{Texture2D::create(static_cast<int>(image.width),
        static_cast<int>(image.height),
        std::span<const std::uint8_t>{image.rgba8},
        k_retail_texture_policy.encoding,
        k_retail_texture_policy.filter)};
    if (!texture) {
      return std::expected<std::unique_ptr<ModelScene>, std::string>{
          std::unexpect, std::move(texture).error()};
    }
    textures.push_back(std::move(texture).value());
  }

  auto shader{Shader::create(K_VERTEX_SHADER_SOURCE, K_FRAGMENT_SHADER_SOURCE)};
  if (!shader) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(shader).error()};
  }
  auto mirror_shader{Shader::create(K_VERTEX_SHADER_SOURCE, K_MIRROR_FRAGMENT_SHADER_SOURCE)};
  if (!mirror_shader) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(mirror_shader).error()};
  }
  auto env_shader{Shader::create(K_ENV_VERTEX_SHADER_SOURCE, K_ENV_FRAGMENT_SHADER_SOURCE)};
  if (!env_shader) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(env_shader).error()};
  }
  auto overlay_shader{
      Shader::create(K_OVERLAY_VERTEX_SHADER_SOURCE, K_OVERLAY_FRAGMENT_SHADER_SOURCE)};
  if (!overlay_shader) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(overlay_shader).error()};
  }
  auto mirror_framebuffer{Framebuffer::create(k_mirror_resolution,
      k_mirror_resolution,
      FramebufferDescription{.color_encoding = TextureColorEncoding::k_legacy_encoded,
          .color_storage = TextureStorageFormat::k_rgba8_unorm,
          .depth_stencil = DepthStencilFormat::k_depth24})};
  if (!mirror_framebuffer) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(mirror_framebuffer).error()};
  }
  auto sky_cubemap{
      TextureCube::create(k_sky_cubemap_size, generate_sky_cubemap(k_sky_cubemap_size), false)};
  if (!sky_cubemap) {
    return std::expected<std::unique_ptr<ModelScene>, std::string>{
        std::unexpect, std::move(sky_cubemap).error()};
  }

  // Pack the model's explicit lights into the std140 block consumed by all
  // three fragment shaders. Models without lights keep the legacy
  // directional fallback (u_light_count == 0).
  const std::size_t light_count{std::min(model.lights.size(), K_MAX_LIGHTS)};
  if (model.lights.size() > K_MAX_LIGHTS) {
    App::Log::warn(LogCategory::Renderer,
        "Model has {} lights; only the first {} fit the uniform block",
        model.lights.size(),
        K_MAX_LIGHTS);
  }
  LightBlockData light_block;
  light_block.light_count = static_cast<std::int32_t>(light_count);

  // CPU-side geometry for the light debug overlay, generated alongside the
  // GPU block: [markers][spot lines][attenuation wireframes].
  std::vector<OverlayVertex> overlay_vertices;
  std::size_t overlay_marker_count{0};
  std::size_t overlay_line_count{0};
  std::size_t overlay_sphere_count{0};

  const auto append_overlay_vertex = [&overlay_vertices](const std::array<float, 3>& position,
                                         const std::array<float, 4>& color) {
    overlay_vertices.push_back(OverlayVertex{.position = position, .color = color});
  };

  // Appends a wireframe sphere of line segments centred on `center`.
  const auto append_attenuation_sphere = [&overlay_sphere_count, &append_overlay_vertex](
                                             const std::array<float, 3>& center,
                                             const float radius,
                                             const std::array<float, 4>& color) {
    if (radius <= 0.0F) {
      return;
    }
    constexpr int k_ring_count{4};
    constexpr int k_segment_count{12};
    constexpr int k_meridian_count{6};
    constexpr float k_two_pi{6.2831853F};

    // Latitude rings (the poles are degenerate and skipped).
    for (int ring_index{1}; ring_index < k_ring_count; ++ring_index) {
      const float latitude{std::numbers::pi_v<float> * static_cast<float>(ring_index) /
                           static_cast<float>(k_ring_count)};
      const float ring_y{radius * std::cos(latitude)};
      const float ring_radius{radius * std::sin(latitude)};
      for (int segment_index{0}; segment_index < k_segment_count; ++segment_index) {
        const float angle0{
            k_two_pi * static_cast<float>(segment_index) / static_cast<float>(k_segment_count)};
        const float angle1{
            k_two_pi * static_cast<float>(segment_index + 1) / static_cast<float>(k_segment_count)};
        append_overlay_vertex({center.at(0) + (ring_radius * std::cos(angle0)),
                                  center.at(1) + ring_y,
                                  center.at(2) + (ring_radius * std::sin(angle0))},
            color);
        append_overlay_vertex({center.at(0) + (ring_radius * std::cos(angle1)),
                                  center.at(1) + ring_y,
                                  center.at(2) + (ring_radius * std::sin(angle1))},
            color);
        overlay_sphere_count += 2U;
      }
    }
    // Meridians from pole to pole.
    for (int meridian_index{0}; meridian_index < k_meridian_count; ++meridian_index) {
      const float angle{
          k_two_pi * static_cast<float>(meridian_index) / static_cast<float>(k_meridian_count)};
      const float cos_angle{std::cos(angle)};
      const float sin_angle{std::sin(angle)};
      for (int segment_index{0}; segment_index < k_segment_count; ++segment_index) {
        const float latitude0{std::numbers::pi_v<float> * static_cast<float>(segment_index) /
                              static_cast<float>(k_segment_count)};
        const float latitude1{std::numbers::pi_v<float> * static_cast<float>(segment_index + 1) /
                              static_cast<float>(k_segment_count)};
        append_overlay_vertex({center.at(0) + (radius * std::sin(latitude0) * cos_angle),
                                  center.at(1) + (radius * std::cos(latitude0)),
                                  center.at(2) + (radius * std::sin(latitude0) * sin_angle)},
            color);
        append_overlay_vertex({center.at(0) + (radius * std::sin(latitude1) * cos_angle),
                                  center.at(1) + (radius * std::cos(latitude1)),
                                  center.at(2) + (radius * std::sin(latitude1) * sin_angle)},
            color);
        overlay_sphere_count += 2U;
      }
    }
  };

  for (std::size_t index{0}; index < light_count; ++index) {
    const Omikron::Light& light{model.lights.at(index)};
    const Omikron::Vec3 direction{Runtime::Presentation::to_gl(light.direction())};
    const Omikron::Vec3 position_gl{Runtime::Presentation::to_gl(light.points.at(0))};
    const Omikron::Vec3 target_gl{Runtime::Presentation::to_gl(light.points.at(1))};
    const std::array<float, 4> color{light.color_rgba()};
    RenderLight& gpu{light_block.lights.at(index)};
    gpu.position = {position_gl.x, position_gl.y, position_gl.z, light.attenuation_start};
    gpu.direction = {direction.x, direction.y, direction.z, light.attenuation_end};
    gpu.color = {color.at(0) * light.intensity * k_light_intensity_scale,
        color.at(1) * light.intensity * k_light_intensity_scale,
        color.at(2) * light.intensity * k_light_intensity_scale,
        0.0F};
    const bool has_direction{
        (direction.x != 0.0F) || (direction.y != 0.0F) || (direction.z != 0.0F)};
    gpu.spot = {k_spot_hotspot_cos, k_spot_falloff_cos, has_direction ? 0.0F : 1.0F, 0.0F};

    // Debug overlay: a marker at the light, a line to the spot target and
    // wireframe spheres at the attenuation radii.
    const std::array<float, 3> position{position_gl.x, position_gl.y, position_gl.z};
    const std::array<float, 4> marker_color{color.at(0), color.at(1), color.at(2), 1.0F};
    append_overlay_vertex(position, marker_color);
    overlay_marker_count += 1U;
    if (has_direction) {
      const std::array<float, 3> target{target_gl.x, target_gl.y, target_gl.z};
      append_overlay_vertex(position, marker_color);
      append_overlay_vertex(target, marker_color);
      overlay_line_count += 2U;
    }
    const std::array<float, 4> sphere_color{color.at(0), color.at(1), color.at(2), 0.5F};
    append_attenuation_sphere(position, light.attenuation_start, sphere_color);
    append_attenuation_sphere(position, light.attenuation_end, sphere_color);
  }
  App::Log::debug(LogCategory::Renderer,
      "Loaded {} explicit lights ({} mesh lights baked into vertex colours)",
      light_count,
      model.header.lights_unknown1);
  // NOLINTNEXTLINE(misc-const-correctness) — move-only, must stay mutable for the move below.
  UniformBuffer light_buffer{std::as_bytes(std::span<const LightBlockData>{&light_block, 1})};

  // Share the block with every shader through a common binding point.
  light_buffer.bind_base(K_LIGHT_BLOCK_BINDING);
  shader->set_uniform_block_binding("LightBlock", K_LIGHT_BLOCK_BINDING);
  mirror_shader->set_uniform_block_binding("LightBlock", K_LIGHT_BLOCK_BINDING);
  env_shader->set_uniform_block_binding("LightBlock", K_LIGHT_BLOCK_BINDING);

  // The constructor is private; only the factory may build a scene.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<ModelScene>{new ModelScene(presentation_groups,
      std::move(textures),
      std::move(shader).value(),
      std::move(mirror_shader).value(),
      std::move(env_shader).value(),
      std::move(overlay_shader).value(),
      std::move(mirror_framebuffer).value(),
      std::move(sky_cubemap).value(),
      std::move(light_buffer),
      std::move(overlay_vertices),
      overlay_marker_count,
      overlay_line_count,
      overlay_sphere_count,
      model_center)};
}

ModelScene::ModelScene(const std::vector<Omikron::MaterialGroup>& groups,
    std::vector<Texture2D> textures,
    Shader shader,
    Shader mirror_shader,
    Shader env_shader,
    Shader overlay_shader,
    Framebuffer mirror_framebuffer,
    TextureCube sky_cubemap,
    UniformBuffer light_buffer,
    std::vector<OverlayVertex> overlay_vertices,
    std::size_t overlay_marker_count,
    std::size_t overlay_line_count,
    std::size_t overlay_sphere_count,
    std::array<float, 3> model_center)
    : m_shader(std::move(shader)),
      m_mirror_shader(std::move(mirror_shader)),
      m_env_shader(std::move(env_shader)),
      m_overlay_shader(std::move(overlay_shader)),
      m_mirror_framebuffer(std::move(mirror_framebuffer)),
      m_sky_cubemap(std::move(sky_cubemap)),
      m_light_buffer(std::move(light_buffer)),
      m_overlay_buffer(std::as_bytes(std::span{overlay_vertices})),
      m_overlay_marker_count(overlay_marker_count),
      m_overlay_line_count(overlay_line_count),
      m_overlay_sphere_count(overlay_sphere_count),
      m_sprite_overlay_buffer(std::span<const std::byte>{}),
      m_textures(std::move(textures)),
      m_model_center(model_center) {
  APP_PROFILE_FUNCTION();

  // Overlay vertex layout: location 0 = position, location 1 = RGBA colour.
  m_overlay_array.bind();
  m_overlay_buffer.bind();
  const GLsizei overlay_stride{static_cast<GLsizei>(sizeof(OverlayVertex))};
  glEnableVertexAttribArray(0);
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
  // Required by the GL API.
  glVertexAttribPointer(0,
      3,
      GL_FLOAT,
      GL_FALSE,
      overlay_stride,
      reinterpret_cast<const void*>(offsetof(OverlayVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,
      4,
      GL_FLOAT,
      GL_FALSE,
      overlay_stride,
      reinterpret_cast<const void*>(offsetof(OverlayVertex, color)));
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
  VertexArray::unbind();
  VertexBuffer::unbind();

  // The sprite overlay reuses the same vertex layout over its own dynamic
  // buffer; per-frame outlines upload through VertexBuffer::upload.
  m_sprite_overlay_array.bind();
  m_sprite_overlay_buffer.bind();
  const GLsizei sprite_overlay_stride{static_cast<GLsizei>(sizeof(OverlayVertex))};
  glEnableVertexAttribArray(0);
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
  // Required by the GL API.
  glVertexAttribPointer(0,
      3,
      GL_FLOAT,
      GL_FALSE,
      sprite_overlay_stride,
      reinterpret_cast<const void*>(offsetof(OverlayVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,
      4,
      GL_FLOAT,
      GL_FALSE,
      sprite_overlay_stride,
      reinterpret_cast<const void*>(offsetof(OverlayVertex, color)));
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
  VertexArray::unbind();
  VertexBuffer::unbind();

  for (std::size_t index{0}; index < groups.size(); ++index) {
    const Omikron::MaterialGroup& group{groups.at(index)};
    m_meshes.emplace_back(group.vertices, group.indices);
    m_group_material_ids.push_back(group.material_id);
    m_group_flags.push_back(group.flags);
    m_group_modes.push_back(Omikron::blend_mode(group.flags));

    // Reflection plane of mirror meshes, from the first face's corners.
    if (Omikron::has_flag(group.flags, Omikron::MeshFlags::k_mirror) &&
        group.indices.size() >= 3U) {
      const glm::vec3 first{glm::make_vec3(group.vertices.at(group.indices.at(0)).position.data())};
      const glm::vec3 second{
          glm::make_vec3(group.vertices.at(group.indices.at(1)).position.data())};
      const glm::vec3 third{glm::make_vec3(group.vertices.at(group.indices.at(2)).position.data())};
      const glm::vec4 plane{plane_from_points(first, second, third)};
      std::array<float, 4> plane_storage{};
      std::copy_n(glm::value_ptr(plane), 4, plane_storage.begin());
      m_mirrors.push_back(MirrorSurface{.plane = plane_storage});
    }

    // Bounding-box centre of the group's vertices; blended meshes are
    // sorted by their distance from the eye along this point.
    std::array<float, 3> min{std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    std::array<float, 3> max{std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};
    for (const Vertex& vertex : group.vertices) {
      for (std::size_t axis{0}; axis < 3U; ++axis) {
        min.at(axis) = std::min(min.at(axis), vertex.position.at(axis));
        max.at(axis) = std::max(max.at(axis), vertex.position.at(axis));
      }
    }
    std::array<float, 3> center{0.0F, 0.0F, 0.0F};
    if (!group.vertices.empty()) {
      for (std::size_t axis{0}; axis < 3U; ++axis) {
        center.at(axis) = (min.at(axis) + max.at(axis)) / 2.0F;
      }
    }
    m_group_centers.push_back(center);
  }

  // The camera sits in front of the model's centre and looks at its torso.
  m_camera.set_position(m_model_center.at(0),
      m_model_center.at(1) + k_camera_height,
      m_model_center.at(2) + k_camera_distance);
  m_camera.look_at(
      m_model_center.at(0), m_model_center.at(1) + k_camera_height, m_model_center.at(2));
}

void ModelScene::draw_group(const std::size_t index) {
  m_shader.bind();

  const std::size_t material_index{static_cast<std::size_t>(m_group_material_ids.at(index))};
  const bool vertex_lit{
      Omikron::has_flag(m_group_flags.at(index), Omikron::MeshFlags::k_vertex_lit)};
  m_shader.set_uniform_float("u_vertex_color", vertex_lit ? 1.0F : 0.0F);

  const Omikron::BlendMode mode{m_group_modes.at(index)};
  const bool alpha_test{mode == Omikron::BlendMode::k_alpha_test};
  m_shader.set_uniform_float("u_alpha_test", alpha_test ? 1.0F : 0.0F);
  m_shader.set_uniform_float("u_alpha_blend", is_blended(mode) ? 1.0F : 0.0F);

  const Texture2D& texture{m_textures.at(material_index)};
  texture.bind(0);
  m_shader.set_uniform_int("u_texture0", 0);
  m_meshes.at(index).draw();
}

void ModelScene::draw_mirror_group(const std::size_t index,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::mat4& model) {
  const std::size_t material_index{static_cast<std::size_t>(m_group_material_ids.at(index))};
  const bool vertex_lit{
      Omikron::has_flag(m_group_flags.at(index), Omikron::MeshFlags::k_vertex_lit)};

  const glm::mat4 mvp{projection * view * model};
  const glm::mat3 normal_matrix{glm::transpose(glm::inverse(glm::mat3{model}))};

  m_mirror_shader.bind();
  m_mirror_shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  m_mirror_shader.set_uniform_mat3(
      "u_normal_matrix", std::span<const GLfloat, 9>{glm::value_ptr(normal_matrix), 9});
  m_mirror_shader.set_uniform_float("u_vertex_color", vertex_lit ? 1.0F : 0.0F);
  m_mirror_shader.set_uniform_float("u_mirror_mode", mirror_mode(m_group_flags.at(index)));
  m_mirror_shader.set_uniform_vec3(
      "u_light_direction", std::span<const GLfloat, 3>{k_light_direction});
  m_mirror_shader.set_uniform_float("u_ambient", k_ambient_strength);
  m_mirror_shader.set_uniform_float("u_lights_enabled", m_lights_enabled ? 1.0F : 0.0F);
  const std::array<GLfloat, 2> viewport_size{
      static_cast<GLfloat>(m_viewport_width), static_cast<GLfloat>(m_viewport_height)};
  m_mirror_shader.set_uniform_vec2("u_viewport_size", std::span<const GLfloat, 2>{viewport_size});

  const Texture2D& surface{m_textures.at(material_index)};
  surface.bind(0);
  m_mirror_shader.set_uniform_int("u_texture0", 0);
  m_mirror_framebuffer.color_texture().bind(1);
  m_mirror_shader.set_uniform_int("u_texture1", 1);

  m_meshes.at(index).draw();
}

void ModelScene::draw_env_group(const std::size_t index,
    const glm::vec3& eye,
    const glm::vec4& clip_plane,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::mat4& model) {
  const std::size_t material_index{static_cast<std::size_t>(m_group_material_ids.at(index))};
  const bool vertex_lit{
      Omikron::has_flag(m_group_flags.at(index), Omikron::MeshFlags::k_vertex_lit)};

  const glm::mat4 mvp{projection * view * model};
  const glm::mat3 normal_matrix{glm::transpose(glm::inverse(glm::mat3{model}))};

  m_env_shader.bind();
  m_env_shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  m_env_shader.set_uniform_mat4("u_model", std::span<const GLfloat, 16>{glm::value_ptr(model), 16});
  m_env_shader.set_uniform_mat3(
      "u_normal_matrix", std::span<const GLfloat, 9>{glm::value_ptr(normal_matrix), 9});
  m_env_shader.set_uniform_vec4(
      "u_clip_plane", std::span<const GLfloat, 4>{glm::value_ptr(clip_plane), 4});
  m_env_shader.set_uniform_vec3(
      "u_camera_position", std::span<const GLfloat, 3>{glm::value_ptr(eye), 3});
  m_env_shader.set_uniform_vec3(
      "u_light_direction", std::span<const GLfloat, 3>{k_light_direction});
  m_env_shader.set_uniform_float("u_ambient", k_ambient_strength);
  m_env_shader.set_uniform_float("u_lights_enabled", m_lights_enabled ? 1.0F : 0.0F);
  m_env_shader.set_uniform_float("u_vertex_color", vertex_lit ? 1.0F : 0.0F);

  const Texture2D& surface{m_textures.at(material_index)};
  surface.bind(0);
  m_env_shader.set_uniform_int("u_texture0", 0);
  m_sky_cubemap.bind(1);
  m_env_shader.set_uniform_int("u_texture_cube", 1);

  m_meshes.at(index).draw();
}

void ModelScene::update(const float delta_time, const Input::InputManager& input) {
  APP_PROFILE_FUNCTION();

  m_camera_controller.update(input, delta_time);

  if (input.is_action_pressed(Input::Action::k_toggle_lights)) {
    m_lights_enabled = !m_lights_enabled;
    App::Log::debug(LogCategory::Renderer,
        "Lights {}",
        m_lights_enabled ? "enabled" : "disabled (ambient only)");
  }

  // Frame ordering for audio (see the milestone notes): advance transforms
  // (camera above) → set the listener. Script ticking is owned by
  // ScenarioEngine and AudioSystem::update() runs exactly once per application
  // frame (in Application), never here.
  Audio::AudioSystem* audio{m_runtime == nullptr ? nullptr : m_runtime->audio_system()};
  if (audio != nullptr) {
    const std::span<const float, 3> eye{m_camera.get_position()};
    const std::span<const float, 16> view{m_camera.get_view_matrix()};
    // View-matrix convention: the third column is the camera's backward
    // (-Z) axis and the second column is its up (+Y) axis (rotation-only
    // view matrix, so columns are world-space basis vectors).
    Audio::AudioListenerState listener;
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const std::array<float, 3> runtime_eye{
        Runtime::Presentation::to_gl(std::array<float, 3>{eye[0], eye[1], eye[2]})};
    const std::array<float, 3> runtime_forward{
        Runtime::Presentation::to_gl(std::array<float, 3>{-view[8], -view[9], -view[10]})};
    const std::array<float, 3> runtime_up{
        Runtime::Presentation::to_gl(std::array<float, 3>{view[4], view[5], view[6]})};
    listener.position = Audio::Vec3{Runtime::inches_to_metres(runtime_eye.at(0)),
        Runtime::inches_to_metres(runtime_eye.at(1)),
        Runtime::inches_to_metres(runtime_eye.at(2))};
    listener.velocity = Audio::Vec3{0.0F, 0.0F, 0.0F};  // No camera velocity yet.
    listener.forward = runtime_forward;
    listener.up = runtime_up;
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    audio->set_listener(listener);
  }
}

void ModelScene::render() {
  APP_PROFILE_FUNCTION();

  // The geometry lives in world space; the camera flies around it.
  const glm::mat4 model{1.0F};
  const glm::mat4 view{glm::make_mat4(m_camera.get_view_matrix().data())};
  const glm::mat4 projection{glm::make_mat4(m_camera.get_projection_matrix().data())};
  const glm::vec3 eye{glm::make_vec3(m_camera.get_position().data())};

  // Reflections first: draw the scene through each mirror plane into the
  // shared reflection buffer before compositing the mirror surfaces.
  for (const MirrorSurface& mirror : m_mirrors) {
    render_reflection(mirror, view, projection, model, eye);
  }

  render_scene(view, projection, model, eye, glm::make_vec4(k_no_clip_plane.data()), true);

  // Expose the sprite queue statistics to the debug performance window.
  const Sprite::SpriteQueueStats& sprite_stats{m_sprite_renderer.queue_stats()};
  Debug::Metrics::get().set_sprite_counters(Debug::SpriteCounters{
      .live = (m_runtime != nullptr) ? m_runtime->sprite_pool().live_count() : 0U,
      .attached = sprite_stats.attached,
      .visible = sprite_stats.visible,
      .drawn = sprite_stats.drawn,
      .culled = sprite_stats.culled,
      .invalid = sprite_stats.invalid,
      .batches = sprite_stats.batches,
      .draw_calls = sprite_stats.draw_calls});

  if (m_light_overlay_enabled) {
    render_light_overlay(view, projection);
  }
  if (m_sprite_overlay_enabled) {
    render_sprite_overlay(view, projection);
  }

  Texture2D::unbind();
  TextureCube::unbind();
  Shader::unbind();
}

void ModelScene::render_reflection(const MirrorSurface& mirror,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::mat4& model,
    const glm::vec3& eye) {
  // Flip the plane so its positive side contains the eye; the clip keeps
  // only that side visible.
  glm::vec4 plane{glm::make_vec4(mirror.plane.data())};
  if (glm::dot(plane, glm::vec4{eye, 1.0F}) < 0.0F) {
    plane = -plane;
  }

  const glm::mat4 reflected_view{reflected_view_matrix(view, plane)};
  const glm::vec3 reflected_eye{reflect_point(eye, plane)};

  m_mirror_framebuffer.bind();
  glViewport(0, 0, k_mirror_resolution, k_mirror_resolution);
  glClearColor(k_mirror_clear_color.at(0),
      k_mirror_clear_color.at(1),
      k_mirror_clear_color.at(2),
      k_mirror_clear_color.at(3));
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_CLIP_DISTANCE0);
  // The reflected view flips handedness; flip the winding with it.
  glFrontFace(GL_CW);

  render_scene(reflected_view, projection, model, reflected_eye, plane, false);

  glDisable(GL_CLIP_DISTANCE0);
  glFrontFace(GL_CCW);
  Framebuffer::unbind();
  glViewport(0, 0, m_viewport_width, m_viewport_height);
}

void ModelScene::render_light_overlay(const glm::mat4& view, const glm::mat4& projection) {
  APP_PROFILE_FUNCTION();

  if (m_overlay_marker_count == 0U && m_overlay_line_count == 0U && m_overlay_sphere_count == 0U) {
    return;
  }

  const glm::mat4 mvp{projection * view};

  m_overlay_shader.bind();
  m_overlay_shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  m_overlay_array.bind();

  // Depth-tested but depth-non-writing: markers respect the geometry in
  // front of them without perturbing the depth buffer.
  glEnable(GL_PROGRAM_POINT_SIZE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);

  const GLint line_first{static_cast<GLint>(m_overlay_marker_count)};
  const GLint sphere_first{static_cast<GLint>(m_overlay_marker_count + m_overlay_line_count)};

  if (m_overlay_marker_count > 0U) {
    m_overlay_shader.set_uniform_float("u_point_mode", 1.0F);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(m_overlay_marker_count));
  }
  if (m_overlay_line_count > 0U || m_overlay_sphere_count > 0U) {
    m_overlay_shader.set_uniform_float("u_point_mode", 0.0F);
  }
  if (m_overlay_line_count > 0U) {
    glDrawArrays(GL_LINES, line_first, static_cast<GLsizei>(m_overlay_line_count));
  }
  if (m_overlay_sphere_count > 0U) {
    glDrawArrays(GL_LINES, sphere_first, static_cast<GLsizei>(m_overlay_sphere_count));
  }

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glDisable(GL_PROGRAM_POINT_SIZE);
  VertexArray::unbind();
  Shader::unbind();
}

namespace {

/// Debug overlay colour per sprite render mode: a fixed palette with
/// distinct hues so the modes stay visually separable at a glance.
std::array<float, 4> sprite_overlay_color(const Sprite::SpriteRenderMode mode) {
  switch (mode) {
    case Sprite::SpriteRenderMode::k_default:
      return {0.90F, 0.90F, 0.90F, 1.0F};
    case Sprite::SpriteRenderMode::k_cutout:
      return {0.20F, 0.90F, 1.00F, 1.0F};
    case Sprite::SpriteRenderMode::k_alpha:
      return {0.35F, 0.60F, 1.00F, 1.0F};
    case Sprite::SpriteRenderMode::k_alpha_cutout:
      return {0.55F, 0.35F, 1.00F, 1.0F};
    case Sprite::SpriteRenderMode::k_additive:
      return {1.00F, 0.75F, 0.10F, 1.0F};
    case Sprite::SpriteRenderMode::k_additive_cutout:
      return {1.00F, 0.45F, 0.10F, 1.0F};
    case Sprite::SpriteRenderMode::k_darken:
      return {0.15F, 0.15F, 0.50F, 1.0F};
    case Sprite::SpriteRenderMode::k_darken_cutout:
      return {0.10F, 0.40F, 0.25F, 1.0F};
    case Sprite::SpriteRenderMode::k_alternate_cutout:
      return {0.85F, 0.30F, 0.80F, 1.0F};
    default:
      return {0.90F, 0.90F, 0.90F, 1.0F};
  }
}

}  // namespace

void ModelScene::render_sprite_overlay(const glm::mat4& view, const glm::mat4& projection) {
  APP_PROFILE_FUNCTION();

  const std::vector<Sprite::SpriteDrawCommand>& commands{m_sprite_renderer.commands()};
  const std::vector<Sprite::SpriteVertex>& vertices{m_sprite_renderer.vertices()};
  if (commands.empty()) {
    return;
  }

  // One outlined quad per drawn billboard: 4 line segments × 2 vertices.
  std::vector<OverlayVertex> outline;
  outline.reserve(commands.size() * 8U);
  for (const Sprite::SpriteDrawCommand& command : commands) {
    const std::array<float, 4> color{sprite_overlay_color(command.pipeline_key.render_mode)};
    // Unique corners in emission order: 0=(-,-), 1=(+,-), 2=(+,+), 5=(-,+).
    const std::array<std::uint32_t, 4> corners{command.first_vertex,
        command.first_vertex + 1U,
        command.first_vertex + 2U,
        command.first_vertex + 5U};
    for (std::size_t edge{0}; edge < corners.size(); ++edge) {
      const Sprite::SpriteVertex& from{vertices.at(corners.at(edge))};
      const Sprite::SpriteVertex& to{vertices.at(corners.at((edge + 1U) % corners.size()))};
      outline.push_back(OverlayVertex{.position = from.position, .color = color});
      outline.push_back(OverlayVertex{.position = to.position, .color = color});
    }
  }

  m_sprite_overlay_buffer.upload(std::as_bytes(std::span{outline}));

  const glm::mat4 mvp{projection * view};

  m_overlay_shader.bind();
  m_overlay_shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  m_overlay_shader.set_uniform_float("u_point_mode", 0.0F);
  m_sprite_overlay_array.bind();

  // Same depth-tested, non-depth-writing style as the light overlay.
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);

  glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(outline.size()));

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  VertexArray::unbind();
  Shader::unbind();
}

void ModelScene::render_scene(const glm::mat4& view,
    const glm::mat4& projection,
    const glm::mat4& model,
    const glm::vec3& eye,
    const glm::vec4& clip_plane,
    const bool draw_mirrors) {
  // Build the sprite queue for the main pass only; mirror reflections skip
  // sprites this milestone (the reflection pass passes draw_mirrors=false).
  if (draw_mirrors && m_sprite_renderer.valid() && m_runtime != nullptr) {
    const ViewBasis basis{view_basis(view)};
    m_sprite_renderer.build_queue(m_runtime->sprite_pool(),
        m_runtime->sprite_resource_ptrs(),
        eye,
        basis.front,
        basis.right,
        basis.up,
        m_camera.get_near_plane(),
        m_camera.get_far_plane());
  }

  const glm::mat4 mvp{projection * view * model};
  const glm::mat3 normal_matrix{glm::transpose(glm::inverse(glm::mat3{model}))};

  m_shader.bind();
  m_shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  m_shader.set_uniform_mat4("u_model", std::span<const GLfloat, 16>{glm::value_ptr(model), 16});
  m_shader.set_uniform_mat3(
      "u_normal_matrix", std::span<const GLfloat, 9>{glm::value_ptr(normal_matrix), 9});
  m_shader.set_uniform_vec3("u_light_direction", std::span<const GLfloat, 3>{k_light_direction});
  m_shader.set_uniform_float("u_ambient", k_ambient_strength);
  m_shader.set_uniform_float("u_lights_enabled", m_lights_enabled ? 1.0F : 0.0F);
  m_shader.set_uniform_vec4(
      "u_clip_plane", std::span<const GLfloat, 4>{glm::value_ptr(clip_plane), 4});

  // Pass 1: opaque geometry writes depth without blending.
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  for (std::size_t index{0}; index < m_meshes.size(); ++index) {
    const std::uint32_t flags{m_group_flags.at(index)};

    const bool mirror{Omikron::has_flag(flags, Omikron::MeshFlags::k_mirror)};
    if (mirror) {
      // Mirrors are composited in the main pass only; the reflection pass
      // skips them (one bounce).
      if (draw_mirrors && !is_blended(m_group_modes.at(index))) {
        draw_mirror_group(index, view, projection, model);
      }
      continue;
    }
    if (Omikron::has_flag(flags, Omikron::MeshFlags::k_environment_mapped)) {
      draw_env_group(index, eye, clip_plane, view, projection, model);
      continue;
    }
    if (!is_blended(m_group_modes.at(index))) {
      draw_group(index);
    }
  }

  // Opaque/cutout sprites write depth like the opaque geometry.
  if (draw_mirrors && m_sprite_renderer.valid() && m_runtime != nullptr) {
    m_sprite_renderer.draw_pass(
        Sprite::SpritePass::k_opaque, view, projection, m_runtime->sprite_textures());
  }

  // Pass 2: blended geometry, far-to-near, without depth writes.
  std::vector<float> distance_squared(m_meshes.size(), 0.0F);
  std::vector<std::size_t> blended_order;
  for (std::size_t index{0}; index < m_meshes.size(); ++index) {
    const bool mirror{Omikron::has_flag(m_group_flags.at(index), Omikron::MeshFlags::k_mirror)};
    if (mirror && !draw_mirrors) {
      continue;
    }
    if (!is_blended(m_group_modes.at(index))) {
      continue;
    }
    const glm::vec4 world_center{
        model * glm::vec4{glm::make_vec3(m_group_centers.at(index).data()), 1.0F}};
    const glm::vec3 offset{glm::vec3{world_center} - eye};
    distance_squared.at(index) = glm::dot(offset, offset);
    blended_order.push_back(index);
  }
  std::ranges::sort(
      blended_order, std::ranges::greater{}, [&distance_squared](const std::size_t index) {
        return distance_squared.at(index);
      });

  glEnable(GL_BLEND);
  glDepthMask(GL_FALSE);
  for (const std::size_t index : blended_order) {
    switch (m_group_modes.at(index)) {
      case Omikron::BlendMode::k_additive:
        glBlendFunc(GL_ONE, GL_ONE);
        glBlendEquation(GL_FUNC_ADD);
        break;
      case Omikron::BlendMode::k_subtractive:
        glBlendFunc(GL_ONE, GL_ONE);
        glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        break;
      default:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        break;
    }
    if (Omikron::has_flag(m_group_flags.at(index), Omikron::MeshFlags::k_mirror)) {
      draw_mirror_group(index, view, projection, model);
    } else {
      draw_group(index);
    }
  }

  // Translucent sprites draw over the blended geometry, depth-tested but
  // not depth-writing, in their own stable batch order.
  if (draw_mirrors && m_sprite_renderer.valid() && m_runtime != nullptr) {
    m_sprite_renderer.draw_pass(
        Sprite::SpritePass::k_translucent, view, projection, m_runtime->sprite_textures());
  }
  glBlendEquation(GL_FUNC_ADD);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
}

void ModelScene::resize(const int width, const int height) {
  APP_PROFILE_FUNCTION();

  if (width <= 0 || height <= 0) {
    return;
  }

  m_viewport_width = width;
  m_viewport_height = height;

  const float aspect{static_cast<float>(width) / static_cast<float>(height)};
  if (aspect == m_aspect_ratio) {
    return;
  }

  m_aspect_ratio = aspect;
  m_camera.set_aspect_ratio(aspect);
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprite instances
// ─────────────────────────────────────────────────────────────────────────────

std::expected<void, std::string> ModelScene::initialize_sprite_renderer() {
  APP_PROFILE_FUNCTION();

  if (auto result{m_sprite_renderer.initialize()}; !result) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("Failed to initialise the sprite renderer: {}", result.error())};
  }
  return {};
}

Script::ScriptRuntime* ModelScene::script_runtime() {
  return m_runtime == nullptr ? nullptr : m_runtime->script_runtime();
}

const Script::ScriptRuntime* ModelScene::script_runtime() const {
  return m_runtime == nullptr ? nullptr : m_runtime->script_runtime();
}

std::string_view ModelScene::script_scenario_name() const {
  return m_runtime == nullptr ? std::string_view{} : m_runtime->script_scenario_name();
}

std::expected<std::size_t, std::string> ModelScene::spawn_script_instance(
    const std::size_t source_script_index) {
  if (m_runtime == nullptr) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "script runtime is not initialised"};
  }
  return m_runtime->spawn_script_instance(source_script_index);
}

void ModelScene::set_audio_system(Audio::AudioSystem* const audio) {
  if (m_runtime != nullptr) {
    m_runtime->set_audio_system(audio);
  }
}

Audio::AudioSystem* ModelScene::audio_system() {
  return m_runtime == nullptr ? nullptr : m_runtime->audio_system();
}

const Audio::AudioSystem* ModelScene::audio_system() const {
  return m_runtime == nullptr ? nullptr : m_runtime->audio_system();
}

Sprite::SpritePool& ModelScene::sprite_pool() {
  return m_runtime->sprite_pool();
}

const Sprite::SpritePool& ModelScene::sprite_pool() const {
  return m_runtime->sprite_pool();
}

const Sprite::SpriteQueueStats& ModelScene::sprite_queue_stats() const {
  return m_sprite_renderer.queue_stats();
}

const std::vector<Sprite::SpriteDrawCommand>& ModelScene::sprite_commands() const {
  return m_sprite_renderer.commands();
}

std::size_t ModelScene::sprite_resource_count() const {
  return m_runtime->sprite_resource_count();
}

std::string_view ModelScene::sprite_resource_name(const std::size_t resource_index) const {
  return m_runtime->sprite_resource_name(resource_index);
}

const Sprite::SpriteResource* ModelScene::sprite_resource(const std::size_t resource_index) const {
  return m_runtime->sprite_resource(resource_index);
}

const Texture2D* ModelScene::sprite_texture(
    const std::size_t resource_index, const std::size_t material_index) const {
  return m_runtime->sprite_texture(resource_index, material_index);
}

std::expected<Sprite::SpriteHandle, std::string> ModelScene::spawn_sprite(
    const std::size_t resource_index,
    const std::size_t object_index,
    const std::array<float, 3> position) {
  return m_runtime->spawn_sprite(resource_index, object_index, position);
}

std::expected<void, std::string> ModelScene::attach_sprite(const Sprite::SpriteHandle handle) {
  return m_runtime->sprite_pool().attach(handle);
}

std::expected<void, std::string> ModelScene::detach_sprite(const Sprite::SpriteHandle handle) {
  return m_runtime->sprite_pool().detach(handle);
}

std::expected<void, std::string> ModelScene::destroy_sprite(const Sprite::SpriteHandle handle) {
  return m_runtime->sprite_pool().destroy(handle);
}

std::expected<void, std::string> ModelScene::set_sprite_frame(
    const Sprite::SpriteHandle handle, const std::uint16_t frame_index) {
  return m_runtime->sprite_pool().set_frame(handle, frame_index);
}

void ModelScene::set_sprite_render_mode(
    const Sprite::SpriteHandle handle, const Sprite::SpriteRenderMode mode) {
  m_runtime->sprite_pool().set_render_mode(handle, mode);
}

void ModelScene::set_sprite_type(const Sprite::SpriteHandle handle, const std::uint16_t type) {
  m_runtime->sprite_pool().set_type(handle, type);
}

void ModelScene::set_sprite_position(
    const Sprite::SpriteHandle handle, const std::array<float, 3> position) {
  m_runtime->sprite_pool().set_position(handle, position);
}

void ModelScene::set_sprite_scale(
    const Sprite::SpriteHandle handle, const float scale_x, const float scale_y) {
  m_runtime->sprite_pool().set_scale(handle, scale_x, scale_y);
}

void ModelScene::set_sprite_scale_x(const Sprite::SpriteHandle handle, const float scale_x) {
  m_runtime->sprite_pool().set_scale_x(handle, scale_x);
}

void ModelScene::set_sprite_scale_y(const Sprite::SpriteHandle handle, const float scale_y) {
  m_runtime->sprite_pool().set_scale_y(handle, scale_y);
}

void ModelScene::set_sprite_rotation(const Sprite::SpriteHandle handle, const float rotation) {
  m_runtime->sprite_pool().set_rotation(handle, rotation);
}

void ModelScene::set_sprite_tint(
    const Sprite::SpriteHandle handle, const std::array<float, 3> tint) {
  m_runtime->sprite_pool().set_tint(handle, tint);
}

void ModelScene::set_sprite_texture_offset(
    const Sprite::SpriteHandle handle, const float offset_u, const float offset_v) {
  m_runtime->sprite_pool().set_texture_offset(handle, offset_u, offset_v);
}

void ModelScene::set_sprite_diffuse_alpha(const Sprite::SpriteHandle handle, const float value) {
  m_runtime->sprite_pool().set_diffuse_alpha(handle, value);
}

void ModelScene::reset_sprite_to_defaults(const Sprite::SpriteHandle handle) {
  m_runtime->sprite_pool().reset_to_defaults(handle);
}

std::array<float, 3> ModelScene::camera_focus_position() const {
  const glm::mat4 view{glm::make_mat4(m_camera.get_view_matrix().data())};
  const glm::vec3 position{glm::make_vec3(m_camera.get_position().data())};
  const glm::vec3 forward{view_basis(view).front};
  const glm::vec3 target{position + (forward * K_SPRITE_CAMERA_FOCUS_DISTANCE)};
  std::array<float, 3> target_storage{};
  std::copy_n(glm::value_ptr(target), 3, target_storage.begin());
  return Runtime::Presentation::to_gl(target_storage);
}

void ModelScene::place_sprite_at_camera_focus(const Sprite::SpriteHandle handle) {
  m_runtime->sprite_pool().set_position(handle, camera_focus_position());
}

void ModelScene::set_sprite_grayscale(const bool enabled) {
  m_sprite_renderer.set_grayscale(enabled);
  m_sprite_grayscale_enabled = enabled;
}

bool ModelScene::sprite_grayscale() const {
  return m_sprite_grayscale_enabled;
}

bool ModelScene::light_overlay_enabled() const {
  return m_light_overlay_enabled;
}

void ModelScene::set_light_overlay_enabled(const bool enabled) {
  m_light_overlay_enabled = enabled;
}

bool ModelScene::sprite_overlay_enabled() const {
  return m_sprite_overlay_enabled;
}

void ModelScene::set_sprite_overlay_enabled(const bool enabled) {
  m_sprite_overlay_enabled = enabled;
}

}  // namespace App

// NOLINTEND(misc-include-cleaner)
