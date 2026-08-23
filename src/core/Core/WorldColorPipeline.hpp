#pragma once

#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <string>

#include <glad/glad.h>

#include "Core/Framebuffer.hpp"
#include "Core/Shader.hpp"

namespace App {

inline constexpr FramebufferDescription k_legacy_encoded_target_description{
    .color_encoding = TextureColorEncoding::k_legacy_encoded,
    .color_storage = TextureStorageFormat::k_rgba16_unorm,
    .depth_stencil = DepthStencilFormat::k_depth24_stencil8};

inline constexpr FramebufferDescription k_linear_scene_target_description{
    .color_encoding = TextureColorEncoding::k_linear,
    .color_storage = TextureStorageFormat::k_rgba16_float,
    // Retained only so depth-aware modern developer overlays can be composed
    // after legacy decode without entering the compatibility target.
    .depth_stencil = DepthStencilFormat::k_depth24_stencil8};

/// Owns the explicit boundary between Runtime-compatible encoded composition,
/// modern linear scene color, and manually encoded SDR display output.
class WorldColorPipeline {
 public:
  static std::expected<std::unique_ptr<WorldColorPipeline>, std::string> create();

  ~WorldColorPipeline();
  WorldColorPipeline(const WorldColorPipeline&) = delete;
  WorldColorPipeline(WorldColorPipeline&&) = delete;
  WorldColorPipeline& operator=(const WorldColorPipeline&) = delete;
  WorldColorPipeline& operator=(WorldColorPipeline&&) = delete;

  /// Creates or transactionally replaces both targets when their drawable
  /// pixel dimensions change.
  std::expected<void, std::string> ensure_targets(int width, int height);

  /// Starts the encoded legacy composition and clears color/depth/stencil.
  void begin_legacy(std::array<float, 4> clear_color) const;

  /// Decodes the completed legacy frame to RGBA16F linear scene color and
  /// leaves that target bound for modern overlays/post-processing.
  void resolve_legacy_to_linear() const;

  /// Manually encodes linear scene color to the default SDR framebuffer.
  void present_linear() const;

  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;

 private:
  WorldColorPipeline(Shader transfer_shader, GLuint vertex_array);
  void draw_transfer(const Texture2D& source, bool decode) const;

  Shader m_transfer_shader;
  GLuint m_vertex_array{0};
  std::optional<Framebuffer> m_legacy_target;
  std::optional<Framebuffer> m_linear_target;
};

}  // namespace App
