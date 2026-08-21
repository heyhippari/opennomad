#include "SpriteRenderer.hpp"

#include <glad/glad.h>

// NOLINTBEGIN(misc-include-cleaner)
// glm follows a "single-include" convention — the umbrella headers are the
// canonical way to pull in the library, even though clang-tidy cannot trace
// individual symbols back to a direct sub-header (see ModelViewerScene.cpp).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/RuntimePresentation.hpp"

namespace App::Sprite {

namespace {

constexpr std::string_view K_SPRITE_VERTEX_SHADER_SOURCE{R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_tint;
uniform mat4 u_mvp;
out vec2 v_uv;
out vec4 v_tint;
void main() {
  gl_Position = u_mvp * vec4(a_position, 1.0);
  v_uv = a_uv;
  v_tint = a_tint;
}
)glsl"};

constexpr std::string_view K_SPRITE_FRAGMENT_SHADER_SOURCE{R"glsl(
#version 410 core
in vec2 v_uv;
in vec4 v_tint;
uniform sampler2D u_texture0;
uniform float u_cutout;      // 1 = discard fragments below the alpha threshold.
uniform float u_grayscale;   // 1 = Rec.601 luminance conversion.
uniform float u_fog_enabled; // 1 = linear fog between u_fog_start/end.
uniform float u_fog_start;
uniform float u_fog_end;
uniform vec4 u_fog_color;
out vec4 fragment_color;
void main() {
  vec4 texel = texture(u_texture0, v_uv) * v_tint;
  if (u_cutout > 0.5 && texel.a < 0.5) {
    discard;
  }
  vec3 color = texel.rgb;
  if (u_grayscale > 0.5) {
    // Original luminance weights: Y = (299R + 587G + 114B) / 1000.
    color = vec3(dot(color, vec3(0.299, 0.587, 0.114)));
  }
  if (u_fog_enabled > 0.5) {
    // Provisional linear fog on window-space depth; the exact Runtime
    // distance-fade equation is not reconstructed yet.
    float depth = gl_FragCoord.z / gl_FragCoord.w;
    float fog_factor = clamp(
        (u_fog_end - depth) / max(u_fog_end - u_fog_start, 0.001), 0.0, 1.0);
    color = mix(u_fog_color.rgb, color, fog_factor);
  }
  fragment_color = vec4(color, texel.a);
}
)glsl"};

/// Two triangles per sprite quad.
constexpr std::size_t K_VERTICES_PER_SPRITE{6};
/// Initial dynamic-buffer capacity: the Runtime pool size, 6 vertices each.
constexpr std::size_t K_INITIAL_VERTEX_CAPACITY{2048U * K_VERTICES_PER_SPRITE};

/// Maps a GL-free blend factor to its OpenGL constant.
[[nodiscard]] GLenum gl_blend_factor(const BlendFactor factor) {
  switch (factor) {
    case BlendFactor::k_zero:
      return GL_ZERO;
    case BlendFactor::k_one:
      return GL_ONE;
    case BlendFactor::k_source_alpha:
      return GL_SRC_ALPHA;
    case BlendFactor::k_one_minus_source_alpha:
      return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::k_one_minus_source_color:
      return GL_ONE_MINUS_SRC_COLOR;
    default:
      return GL_ONE;
  }
}

/// Maps a typed frame-resolution failure to a queue skip reason.
[[nodiscard]] constexpr SpriteSkipReason reason_for(const SpriteFrameError::Kind kind) {
  switch (kind) {
    case SpriteFrameError::Kind::k_object_out_of_range:
      return SpriteSkipReason::k_object_out_of_range;
    case SpriteFrameError::Kind::k_frame_out_of_range:
      return SpriteSkipReason::k_frame_out_of_range;
    case SpriteFrameError::Kind::k_point_out_of_range:
      return SpriteSkipReason::k_point_out_of_range;
    case SpriteFrameError::Kind::k_texture_out_of_range:
      return SpriteSkipReason::k_texture_out_of_range;
    case SpriteFrameError::Kind::k_degenerate_dimensions:
      return SpriteSkipReason::k_degenerate_dimensions;
    default:
      return SpriteSkipReason::k_frame_out_of_range;
  }
}

/// Packs the resource and material identity into the 32-bit texture key.
/// Embedded effect models carry a single material; indices above 0xFF are
/// rejected as unsupported.
[[nodiscard]] std::uint32_t pack_texture_id(
    const std::size_t resource_index, const std::int32_t material_index) {
  return (static_cast<std::uint32_t>(resource_index) << 8U) |
         (static_cast<std::uint32_t>(material_index) & 0xFFU);
}

}  // namespace

const char* skip_reason_name(const SpriteSkipReason reason) {
  switch (reason) {
    case SpriteSkipReason::k_invalid_handle:
      return "Invalid handle";
    case SpriteSkipReason::k_detached:
      return "Detached";
    case SpriteSkipReason::k_missing_resource:
      return "Missing resource";
    case SpriteSkipReason::k_object_out_of_range:
      return "Object out of range";
    case SpriteSkipReason::k_frame_out_of_range:
      return "Frame out of range";
    case SpriteSkipReason::k_point_out_of_range:
      return "Point index out of range";
    case SpriteSkipReason::k_texture_out_of_range:
      return "Texture index out of range";
    case SpriteSkipReason::k_degenerate_dimensions:
      return "Degenerate dimensions";
    case SpriteSkipReason::k_unsupported_path:
      return "Unsupported material path";
    case SpriteSkipReason::k_behind_camera:
      return "Behind camera";
    case SpriteSkipReason::k_outside_depth_range:
      return "Outside depth range";
    default:
      return "Unknown";
  }
}

std::expected<void, std::string> SpriteRenderer::initialize() {
  APP_PROFILE_FUNCTION();

  auto shader{Shader::create(K_SPRITE_VERTEX_SHADER_SOURCE, K_SPRITE_FRAGMENT_SHADER_SOURCE)};
  if (!shader) {
    return std::expected<void, std::string>{std::unexpect, std::move(shader).error()};
  }
  m_shader = std::make_unique<Shader>(std::move(shader).value());
  m_vertex_array = std::make_unique<VertexArray>();

  const std::vector<std::byte> initial{K_INITIAL_VERTEX_CAPACITY * sizeof(SpriteVertex)};
  m_vertex_buffer = std::make_unique<VertexBuffer>(initial, GL_DYNAMIC_DRAW);
  m_vertex_buffer_capacity = K_INITIAL_VERTEX_CAPACITY;

  m_vertex_array->bind();
  m_vertex_buffer->bind();

  const GLsizei stride{static_cast<GLsizei>(sizeof(SpriteVertex))};
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
  // Required by the GL API (see Mesh.cpp).
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,
      3,
      GL_FLOAT,
      GL_FALSE,
      stride,
      reinterpret_cast<const void*>(offsetof(SpriteVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(
      1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SpriteVertex, uv)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2,
      4,
      GL_FLOAT,
      GL_FALSE,
      stride,
      reinterpret_cast<const void*>(offsetof(SpriteVertex, tint)));
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)

  VertexArray::unbind();
  VertexBuffer::unbind();
  return {};
}

bool SpriteRenderer::valid() const {
  return m_shader != nullptr && m_shader->program_id() != 0U;
}

void SpriteRenderer::build_queue(const SpritePool& pool,
    const std::span<const SpriteResource* const> resources,
    const glm::vec3& eye,
    const glm::vec3& forward,
    const glm::vec3& right,
    const glm::vec3& up,
    const float near_plane,
    const float far_plane) {
  APP_PROFILE_SCOPE("SpriteQueueBuild");

  m_vertices.clear();
  m_commands.clear();
  m_vertices_uploaded = false;
  m_stats = SpriteQueueStats{};
  m_stats.attached = pool.attached_count();

  const auto record_skip = [this](const SpriteHandle handle, const SpriteSkipReason reason) {
    if (m_stats.skipped.size() < m_skip_history_limit) {
      m_stats.skipped.emplace_back(handle, reason);
    }
  };

  for (std::optional<SpriteHandle> current{pool.render_list_head()}; current.has_value();
      current = pool.render_list_next(*current)) {
    const SpriteInstance* instance{pool.find(*current)};
    if (instance == nullptr) {
      record_skip(*current, SpriteSkipReason::k_invalid_handle);
      continue;
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    // std::span has no at(); the bounds are checked explicitly in the same
    // expression (short-circuit evaluation keeps the indexing safe).
    const bool missing{(instance->resource_index >= resources.size()) ||
                       (resources[instance->resource_index] == nullptr)};
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    if (missing) {
      m_stats.invalid += 1;
      record_skip(instance->handle, SpriteSkipReason::k_missing_resource);
      continue;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const SpriteResource& resource{*resources[instance->resource_index]};

    const auto resolved{resource.resolve_frame(instance->object_index,
        instance->frame_index,
        instance->texture_offset_u,
        instance->texture_offset_v)};
    if (!resolved) {
      m_stats.invalid += 1;
      record_skip(instance->handle, reason_for(resolved.error().kind));
      continue;
    }
    const SpriteFrame& frame{*resolved};
    if (frame.texture_index > 0xFF) {
      m_stats.invalid += 1;
      record_skip(instance->handle, SpriteSkipReason::k_unsupported_path);
      continue;
    }

    // Sprite instances remain Runtime-native through gameplay. Convert their
    // anchor exactly once at the GL billboard construction boundary.
    const std::array<float, 3> presentation_position{
        Runtime::Presentation::to_gl(instance->position)};
    const glm::vec3 position{glm::make_vec3(presentation_position.data())};
    const glm::vec3 offset{position - eye};
    const float depth{glm::dot(offset, forward)};
    if (depth <= 0.0F) {
      m_stats.culled += 1;
      record_skip(instance->handle, SpriteSkipReason::k_behind_camera);
      continue;
    }
    if (depth < near_plane || depth > far_plane) {
      m_stats.culled += 1;
      record_skip(instance->handle, SpriteSkipReason::k_outside_depth_range);
      continue;
    }

    // Camera-facing billboard: scale in the camera plane, rotate around the
    // billboard centre, then place in world space. Corner order (-,-), (+,-),
    // (+,+), (-,+) winds CCW as seen from the camera.
    const float half_width{frame.width * 0.5F * instance->scale_x};
    const float half_height{frame.height * 0.5F * instance->scale_y};
    const float cos_r{std::cos(instance->rotation)};
    const float sin_r{std::sin(instance->rotation)};
    const auto corner = [&](const float local_x, const float local_y) {
      const float rotated_x{(local_x * cos_r) - (local_y * sin_r)};
      const float rotated_y{(local_x * sin_r) + (local_y * cos_r)};
      return position + (right * rotated_x) + (up * rotated_y);
    };

    const std::uint32_t first_vertex{static_cast<std::uint32_t>(m_vertices.size())};
    const std::array<float, 4> tint{
        instance->tint.at(0), instance->tint.at(1), instance->tint.at(2), 1.0F};
    const auto emit = [&](const glm::vec3& world, const std::array<float, 2>& uv) {
      SpriteVertex vertex{.uv = uv, .tint = tint};
      std::copy_n(glm::value_ptr(world), 3, vertex.position.begin());
      m_vertices.push_back(vertex);
    };
    const std::array<float, 2> uv00{frame.uv0};
    const std::array<float, 2> uv10{frame.uv1.at(0), frame.uv0.at(1)};
    const std::array<float, 2> uv11{frame.uv1};
    const std::array<float, 2> uv01{frame.uv0.at(0), frame.uv1.at(1)};

    emit(corner(-half_width, -half_height), uv00);
    emit(corner(half_width, -half_height), uv10);
    emit(corner(half_width, half_height), uv11);

    emit(corner(-half_width, -half_height), uv00);
    emit(corner(half_width, half_height), uv11);
    emit(corner(-half_width, half_height), uv01);

    m_commands.push_back(SpriteDrawCommand{.first_vertex = first_vertex,
        .vertex_count = static_cast<std::uint32_t>(K_VERTICES_PER_SPRITE),
        .pipeline_key = SpritePipelineKey{.texture_id = pack_texture_id(
                                              instance->resource_index, frame.texture_index),
            .render_mode = instance->render_mode},
        .resource_index = instance->resource_index,
        .material_index = frame.texture_index,
        .sprite = instance->handle});
    m_stats.visible += 1;
    m_stats.drawn += 1;
  }

  // Group opaque commands first, then translucent, each stably sorted by
  // pipeline key. The stable sort preserves the scene-list insertion order
  // inside a batch, matching the original runtime's behaviour.
  std::ranges::stable_sort(
      m_commands, [](const SpriteDrawCommand& lhs, const SpriteDrawCommand& rhs) {
        const bool lhs_blend{render_state(lhs.pipeline_key.render_mode).blend_enabled};
        const bool rhs_blend{render_state(rhs.pipeline_key.render_mode).blend_enabled};
        if (lhs_blend != rhs_blend) {
          return !lhs_blend;  // Opaque commands sort first.
        }
        return lhs.pipeline_key < rhs.pipeline_key;
      });

  m_stats.draw_calls = m_commands.size();
  const SpriteDrawCommand* last_opaque{nullptr};
  const SpriteDrawCommand* last_translucent{nullptr};
  for (const SpriteDrawCommand& command : m_commands) {
    const bool blended{render_state(command.pipeline_key.render_mode).blend_enabled};
    const SpriteDrawCommand*& last{blended ? last_translucent : last_opaque};
    if (last == nullptr || command.pipeline_key != last->pipeline_key) {
      m_stats.batches += 1;
      last = &command;
    }
  }
}

void SpriteRenderer::draw_pass(const SpritePass pass,
    const glm::mat4& view,
    const glm::mat4& projection,
    const std::vector<std::vector<Texture2D>>& textures) {
  if (!valid() || m_commands.empty()) {
    return;
  }
  APP_PROFILE_SCOPE(pass == SpritePass::k_opaque ? "SpriteDrawOpaque" : "SpriteDrawTranslucent");
  draw_commands(pass, view, projection, textures);
}

void SpriteRenderer::draw_commands(const SpritePass pass,
    const glm::mat4& view,
    const glm::mat4& projection,
    const std::vector<std::vector<Texture2D>>& textures) {
  upload_vertices();

  m_vertex_array->bind();
  m_shader->bind();

  const glm::mat4 mvp{projection * view};
  m_shader->set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{glm::value_ptr(mvp), 16});
  m_shader->set_uniform_int("u_texture0", 0);
  m_shader->set_uniform_float("u_grayscale", m_grayscale ? 1.0F : 0.0F);
  const bool fog_enabled{m_fog_end > m_fog_start};
  m_shader->set_uniform_float("u_fog_start", m_fog_start);
  m_shader->set_uniform_float("u_fog_end", m_fog_end);
  m_shader->set_uniform_vec4("u_fog_color", std::span<const GLfloat, 4>{m_fog_color.data(), 4});

  // Billboards face the camera exactly; culling only risks losing quads to
  // float error, so it is disabled for the sprite pass.
  glDisable(GL_CULL_FACE);
  m_current_blend_source = 0;
  m_current_blend_destination = 0;

  GLuint bound_texture_id{0};
  for (const SpriteDrawCommand& command : m_commands) {
    const SpriteRenderState state{render_state(command.pipeline_key.render_mode)};
    const bool translucent{state.blend_enabled};
    if (translucent != (pass == SpritePass::k_translucent)) {
      continue;
    }

    apply_blend_function(state);
    m_shader->set_uniform_float("u_cutout", state.cutout ? 1.0F : 0.0F);
    m_shader->set_uniform_float("u_fog_enabled", (fog_enabled && state.fogged) ? 1.0F : 0.0F);

    GLuint texture_id{0};
    if (command.resource_index < textures.size() &&
        static_cast<std::size_t>(command.material_index) <
            textures.at(command.resource_index).size()) {
      const Texture2D& texture{
          textures.at(command.resource_index).at(static_cast<std::size_t>(command.material_index))};
      texture_id = texture.id();
      if (texture_id != bound_texture_id) {
        texture.bind(0);
        bound_texture_id = texture_id;
      }
    }
    if (texture_id == 0U) {
      App::Log::warn(LogCategory::Renderer,
          "Sprite {} references a missing GPU texture (resource {}, material {})",
          command.sprite.index,
          command.resource_index,
          command.material_index);
      continue;
    }

    glDrawArrays(GL_TRIANGLES,
        static_cast<GLint>(command.first_vertex),
        static_cast<GLsizei>(command.vertex_count));
  }

  glEnable(GL_CULL_FACE);
  VertexArray::unbind();
  Shader::unbind();
}

void SpriteRenderer::apply_blend_function(const SpriteRenderState& state) {
  if (!state.blend_enabled) {
    return;  // The surrounding opaque pass disables blending itself.
  }
  const GLenum source{gl_blend_factor(state.source_factor)};
  const GLenum destination{gl_blend_factor(state.destination_factor)};
  if (source != m_current_blend_source || destination != m_current_blend_destination) {
    glBlendFunc(source, destination);
    m_current_blend_source = source;
    m_current_blend_destination = destination;
  }
}

void SpriteRenderer::upload_vertices() {
  if (m_vertices.empty() || m_vertices_uploaded) {
    return;
  }
  APP_PROFILE_SCOPE("SpriteBufferUpload");

  if (m_vertices.size() > m_vertex_buffer_capacity) {
    m_vertex_buffer_capacity = m_vertices.size() + (m_vertices.size() / 2U);
  }
  m_vertex_buffer->upload(std::as_bytes(std::span<const SpriteVertex>{m_vertices}));
  m_vertices_uploaded = true;
}

void SpriteRenderer::set_grayscale(const bool enabled) {
  m_grayscale = enabled;
}

void SpriteRenderer::set_fog(const float start, const float end, const std::array<float, 3> color) {
  m_fog_start = start;
  m_fog_end = end;
  m_fog_color = color;
}

const SpriteQueueStats& SpriteRenderer::queue_stats() const {
  return m_stats;
}

const std::vector<SpriteDrawCommand>& SpriteRenderer::commands() const {
  return m_commands;
}

const std::vector<SpriteVertex>& SpriteRenderer::vertices() const {
  return m_vertices;
}

}  // namespace App::Sprite

// NOLINTEND(misc-include-cleaner)
