#pragma once

#include <glad/glad.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// NOLINTNEXTLINE(misc-include-cleaner) — glm umbrella include, see ModelViewerScene.hpp.
#include <glm/glm.hpp>

#include "Core/Buffers.hpp"
#include "Core/Shader.hpp"
#include "Core/Sprite/SpriteFrame.hpp"
#include "Core/Sprite/SpriteInstance.hpp"
#include "Core/Sprite/SpritePool.hpp"
#include "Core/Sprite/SpriteRenderMode.hpp"
#include "Core/Sprite/SpriteResource.hpp"
#include "Core/Texture.hpp"
#include "Core/VertexArray.hpp"

namespace App::Sprite {

/// Compact per-batch renderer key: the packed texture identity plus the
/// render mode. Reserved bits mirror the original runtime's bucket flags
/// (0x0040 second UV set, 0x0800 doubled fog range) for future use.
struct SpritePipelineKey {
  std::uint32_t texture_id{0};
  SpriteRenderMode render_mode{SpriteRenderMode::k_default};
  bool second_uv{false};
  bool doubled_fog_range{false};

  friend constexpr bool operator==(const SpritePipelineKey&, const SpritePipelineKey&) = default;
  friend constexpr auto operator<=>(const SpritePipelineKey&, const SpritePipelineKey&) = default;
};

/// CPU vertex of one sprite billboard corner in GL presentation space.
struct SpriteVertex {
  std::array<float, 3> position{0.0F, 0.0F, 0.0F};
  std::array<float, 2> uv{0.0F, 0.0F};
  std::array<float, 4> tint{1.0F, 1.0F, 1.0F, 1.0F};
};
static_assert(sizeof(SpriteVertex) == 36U);  // 3*4 + 2*4 + 4*4 bytes.

/// One batched draw of sprite vertices (two triangles, six vertices).
struct SpriteDrawCommand {
  std::uint32_t first_vertex{0};
  std::uint32_t vertex_count{0};
  SpritePipelineKey pipeline_key;
  std::size_t resource_index{0};
  std::int32_t material_index{0};
  SpriteHandle sprite;
};

/// Why a sprite produced no geometry this frame. Drives the inspector's
/// visibility diagnostics; never logged per frame.
enum class SpriteSkipReason : std::uint8_t {
  k_invalid_handle,
  k_detached,
  k_missing_resource,
  k_object_out_of_range,
  k_frame_out_of_range,
  k_point_out_of_range,
  k_texture_out_of_range,
  k_degenerate_dimensions,
  k_unsupported_path,
  k_behind_camera,
  k_outside_depth_range,
};

/// Human-readable name of a skip reason (inspector / tests).
[[nodiscard]] const char* skip_reason_name(SpriteSkipReason reason);

/// Per-frame sprite queue statistics, also consumed by the debug tools.
struct SpriteQueueStats {
  std::size_t attached{0};
  std::size_t visible{0};
  std::size_t drawn{0};
  std::size_t culled{0};
  std::size_t invalid{0};
  std::size_t batches{0};
  std::size_t draw_calls{0};
  /// Bounded history of skipped sprites (oldest entries drop first).
  std::vector<std::pair<SpriteHandle, SpriteSkipReason>> skipped;
};

/// Which part of the scene pass a draw belongs to.
enum class SpritePass : std::uint8_t { k_opaque, k_translucent };

/// Builds and draws the sprite queue: Runtime-native anchors are adapted into
/// GL presentation-space billboards, fed to one dynamic vertex buffer, and
/// drawn in batches keyed by pipeline state.
class SpriteRenderer {
 public:
  SpriteRenderer() = default;
  ~SpriteRenderer() = default;
  SpriteRenderer(const SpriteRenderer&) = delete;
  SpriteRenderer(SpriteRenderer&&) = delete;
  SpriteRenderer& operator=(const SpriteRenderer&) = delete;
  SpriteRenderer& operator=(SpriteRenderer&&) = delete;

  /// Compiles the sprite shader and creates the vertex array and the
  /// dynamic vertex buffer. Requires a current GL context.
  [[nodiscard]] std::expected<void, std::string> initialize();

  /// True after a successful initialize().
  [[nodiscard]] bool valid() const;

  /// Rebuilds the per-frame draw queue from the attached sprites of `pool`.
  /// resources parallels the pool's resource indices. eye/forward/right/up
  /// are the camera basis; near/far bound the depth test.
  void build_queue(const SpritePool& pool,
      std::span<const SpriteResource* const> resources,
      const glm::vec3& eye,
      const glm::vec3& forward,
      const glm::vec3& right,
      const glm::vec3& up,
      float near_plane,
      float far_plane);

  /// Draws one pass of the previously built queue. The caller owns the
  /// surrounding pass state (blend enable and depth mask); this sets blend
  /// functions, cutout and fog uniforms per batch and restores culling.
  void draw_pass(SpritePass pass,
      const glm::mat4& view,
      const glm::mat4& projection,
      const std::vector<std::vector<GameColorTexture>>& textures);

  /// Draws modes 0, 1 and 8 into the current scene-linear target.
  void draw_modern_opaque(const glm::mat4& view,
      const glm::mat4& projection,
      const std::vector<std::vector<GameColorTexture>>& textures);
  /// Ascending Runtime bucket keys present in the legacy portion of the queue.
  [[nodiscard]] std::vector<std::uint16_t> legacy_buckets() const;
  [[nodiscard]] std::size_t legacy_bucket_draw_count(std::uint16_t bucket) const;
  /// Emits one bucket as encoded operator sources. The compositor owns blend state.
  void draw_legacy_bucket(std::uint16_t bucket,
      const glm::mat4& view,
      const glm::mat4& projection,
      const std::vector<std::vector<GameColorTexture>>& textures);

  /// Renderer-wide grayscale (Rec.601 luminance) for the sprite pass.
  void set_grayscale(bool enabled);
  /// Linear fog bounds and colour; fog is disabled while end <= start.
  /// Translucent sprites are never fogged (original runtime behaviour).
  void set_fog(float start, float end, std::array<float, 3> color);

  [[nodiscard]] const SpriteQueueStats& queue_stats() const;
  [[nodiscard]] const std::vector<SpriteDrawCommand>& commands() const;
  [[nodiscard]] const std::vector<SpriteVertex>& vertices() const;

 private:
  /// Uploads the accumulated vertices, growing the buffer when needed.
  void upload_vertices();
  /// Draws the commands of one pass in batch order.
  void draw_commands(SpritePass pass,
      const glm::mat4& view,
      const glm::mat4& projection,
      const std::vector<std::vector<GameColorTexture>>& textures,
      std::uint16_t only_bucket,
      bool compositor_owned_blend);
  /// Applies the blend function of one command, caching the current one.
  void apply_blend_function(const SpriteRenderState& state);

  std::unique_ptr<Shader> m_modern_shader;
  std::unique_ptr<Shader> m_legacy_shader;
  std::unique_ptr<VertexArray> m_vertex_array;
  std::unique_ptr<VertexBuffer> m_vertex_buffer;

  std::vector<SpriteVertex> m_vertices;
  std::vector<SpriteDrawCommand> m_commands;
  SpriteQueueStats m_stats;
  std::size_t m_skip_history_limit{128};

  bool m_grayscale{false};
  float m_fog_start{0.0F};
  float m_fog_end{0.0F};
  std::array<float, 3> m_fog_color{1.0F, 1.0F, 1.0F};

  std::size_t m_vertex_buffer_capacity{0};
  /// True once the current queue's vertices are on the GPU (both passes
  /// share the same upload).
  bool m_vertices_uploaded{false};
  GLenum m_current_blend_source{0};
  GLenum m_current_blend_destination{0};
};

}  // namespace App::Sprite
