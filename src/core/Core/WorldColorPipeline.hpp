#pragma once

#include <glad/glad.h>

#include <array>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <string>

#include "Core/Framebuffer.hpp"
#include "Core/Shader.hpp"

namespace App {

inline constexpr FramebufferDescription k_linear_scene_target_description{
    .color_encoding = TextureColorEncoding::k_linear,
    .color_storage = TextureStorageFormat::k_rgba16_float,
    .depth_stencil = DepthStencilFormat::k_depth24_stencil8};

inline constexpr FramebufferDescription k_legacy_accumulator_target_description{
    .color_encoding = TextureColorEncoding::k_legacy_encoded,
    .color_storage = TextureStorageFormat::k_rgba16_unorm,
    .depth_stencil = DepthStencilFormat::k_depth24_stencil8};

enum class LegacyBlendOperator : unsigned char {
  k_alpha,
  k_additive,
  k_darken,
  k_subtractive,
};

struct LegacyBlendCompositorStats {
  std::size_t stages{0};
  std::size_t source_draws{0};
  std::size_t composites{0};
};

/// Scene-linear HDR target manager and portable two-pass legacy blend
/// compositor. The encoded RGBA16 target is transient operator state only;
/// canonical scene color always remains in one of the RGBA16F targets.
class WorldColorPipeline {
 public:
  static std::expected<std::unique_ptr<WorldColorPipeline>, std::string> create();

  ~WorldColorPipeline();
  WorldColorPipeline(const WorldColorPipeline&) = delete;
  WorldColorPipeline(WorldColorPipeline&&) = delete;
  WorldColorPipeline& operator=(const WorldColorPipeline&) = delete;
  WorldColorPipeline& operator=(WorldColorPipeline&&) = delete;

  std::expected<void, std::string> ensure_targets(int width, int height);
  void begin_scene(std::array<float, 4> encoded_clear_color);
  void composite_legacy_stage(LegacyBlendOperator blend_operator,
      std::size_t source_draws,
      const std::function<void()>& draw_sources);
  void bind_current_scene() const;
  void present_linear() const;

  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] bool current_scene_is_a() const;
  [[nodiscard]] const LegacyBlendCompositorStats& stats() const;

 private:
  class Targets;
  WorldColorPipeline(Shader compositor_shader, Shader display_shader, GLuint vertex_array);
  void draw_fullscreen(const Shader& shader) const;

  Shader m_compositor_shader;
  Shader m_display_shader;
  GLuint m_vertex_array{0};
  std::unique_ptr<Targets> m_targets;
  bool m_current_scene_a{true};
  LegacyBlendCompositorStats m_stats;
};

}  // namespace App
