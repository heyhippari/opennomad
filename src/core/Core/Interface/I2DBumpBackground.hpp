#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Core/IntegerTexture.hpp"
#include "Core/Interface/I2DBumpEndpoint.hpp"
#include "Core/Interface/I2DBumpTimeline.hpp"
#include "Core/Interface/I2DPresentation.hpp"
#include "Core/Texture.hpp"

namespace App {

class Shader;
class VertexArray;

}  // namespace App

namespace App::Interface {

/// Fully GPU-rendered animated main-menu background reproducing Runtime's I2D
/// bump effect (source file C:\Omikron\Sources\omikron\I2D_Bump.c).
///
/// Runtime authority lives in the recovered 30 Hz endpoint state. OpenNomad
/// carries two scalar endpoints (tick N and tick N+1), interpolates between
/// them at the host/display frame rate, and evaluates the effect at native
/// resolution in a fullscreen fragment shader. The CPU owns only the effect
/// clock, the two scalar endpoints and a handful of uniforms per frame — no
/// per-pixel CPU loops, no per-tick texture uploads, and no compute shaders
/// (OpenGL 4.1 core only).
///
/// The pure-CPU `I2DBumpEffect` remains the reference/reverse-engineering
/// oracle and is no longer used by the production render path.
class I2DBumpBackground {
 public:
  /// Loads IMAGES/CLOUD.BMP as indexed height data, uploads it as an integer
  /// R8UI texture, generates the static signed-gradient field on the GPU and
  /// compiles the pipeline. Requires a current GL context. A missing,
  /// unreadable or unsupported (non-256x256, non-8-bit indexed) source is an
  /// error so the caller can degrade explicitly.
  [[nodiscard]] static std::expected<std::unique_ptr<I2DBumpBackground>, std::string> create();

  I2DBumpBackground(const I2DBumpBackground&) = delete;
  I2DBumpBackground(I2DBumpBackground&&) = delete;
  I2DBumpBackground& operator=(const I2DBumpBackground&) = delete;
  I2DBumpBackground& operator=(I2DBumpBackground&&) = delete;
  ~I2DBumpBackground();

  /// Advances the 30 Hz animation timeline by `delta_time` seconds. When one
  /// or more tick boundaries are crossed the two scalar endpoint states are
  /// advanced; no per-pixel work is performed. The final draw runs in
  /// render() every frame.
  void update(float delta_time);

  /// Regenerates the row/column warp endpoint textures when the current tick
  /// or viewport changed, then draws the interpolated background over the
  /// entire physical viewport (one fullscreen draw per displayed frame).
  void render(const I2DPresentationTransform& transform);

  // --- Diagnostics (driven into Debug::I2DCounters by the renderer) ---

  /// Number of authentic tick boundaries crossed by the most recent update().
  [[nodiscard]] std::uint64_t last_ticks() const {
    return m_last_ticks;
  }

  /// Index of the current endpoint tick (tick N).
  [[nodiscard]] std::uint64_t current_tick() const {
    return m_timeline.current_tick;
  }

  /// Fractional progress N -> N+1, in [0, 1).
  [[nodiscard]] float alpha() const {
    return m_timeline.alpha;
  }

  /// Number of warp-generation GPU passes run by the most recent render().
  [[nodiscard]] std::size_t last_warp_passes() const {
    return m_last_warp_passes;
  }

  /// Number of background draws issued by the most recent render().
  [[nodiscard]] std::size_t last_draw_calls() const {
    return m_last_draw_calls;
  }

  /// Dynamic bytes uploaded CPU -> GPU by the most recent update(). Always 0:
  /// the lit field no longer travels over the bus after initialization.
  [[nodiscard]] std::size_t last_upload_bytes() const {
    return 0;
  }

  [[nodiscard]] BumpAnimationMode presentation_mode() const {
    return m_mode;
  }

  [[nodiscard]] bool interpolated() const {
    return m_mode == BumpAnimationMode::k_interpolated;
  }

  /// Toggles between stepped (authentic 30 Hz) and interpolated presentation.
  void set_interpolated(const bool interpolated);

  /// Development parity check: rebuilds the CPU reference effect from the
  /// uploaded height indices, reads back the GPU gradient texture and compares
  /// every cell against the reference's signed gradients. Returns the number
  /// of mismatching cells (0 = exact). Debug tooling only; requires a current
  /// GL context and performs a GPU readback.
  [[nodiscard]] std::size_t debug_compare_gradient() const;

 private:
  /// An integer texture plus the framebuffer rendering into it.
  struct RenderTarget {
    IntegerTexture texture;
    GLuint framebuffer{0};
    int width{0};
    int height{0};

    RenderTarget() = default;
    RenderTarget(IntegerTexture texture_id, GLuint framebuffer_id, int w, int h)
        : texture(std::move(texture_id)),
          framebuffer(framebuffer_id),
          width(w),
          height(h) {}
    RenderTarget(RenderTarget&& other) noexcept;
    RenderTarget& operator=(RenderTarget&& other) noexcept;
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    void bind() const;
    static void unbind();
  };

  explicit I2DBumpBackground(std::vector<std::uint8_t> height_indices);

  [[nodiscard]] static std::expected<RenderTarget, std::string> create_render_target(
      int width, int height, IntegerFormat format);

  /// Uploads the R8UI height texture and generates the static RG8I gradient
  /// texture via one GPU preprocessing pass. Called once during create().
  [[nodiscard]] std::expected<void, std::string> build_static_resources();

  /// Regenerates the row/column warp endpoint pair textures for the current
  /// tick and viewport (two tiny GPU passes).
  void rebuild_warp_textures(const I2DPresentationTransform& transform);

  /// Raw CLOUD.BMP indices (row 0 = top); uploaded once as R8UI and kept for
  /// the development gradient parity check.
  std::vector<std::uint8_t> m_height_indices;

  /// 256x256 R8UI height texture (never changes after initialization).
  IntegerTexture m_height;
  /// 256x256 RG8I static signed gradient field (GPU-generated once).
  std::optional<RenderTarget> m_gradient;
  /// pixel_height x 1 RG8UI row warp endpoint pairs (R = tick N, G = N+1).
  std::optional<RenderTarget> m_row_warp;
  /// pixel_width x 1 RG8UI column warp endpoint pairs.
  std::optional<RenderTarget> m_column_warp;
  /// 64x1 RGBA8 sRGB palette ramp (static).
  std::optional<Texture2D> m_palette;

  std::unique_ptr<Shader> m_gradient_shader;
  std::unique_ptr<Shader> m_row_warp_shader;
  std::unique_ptr<Shader> m_col_warp_shader;
  std::unique_ptr<Shader> m_background_shader;
  std::unique_ptr<VertexArray> m_vertex_array;

  BumpAnimationMode m_mode{BumpAnimationMode::k_interpolated};
  BumpTimelineState m_timeline;
  I2DBumpEndpointState m_current;
  I2DBumpEndpointState m_next;
  /// Host seconds accumulated since the current endpoint boundary.
  double m_remainder_seconds{0.0};

  /// Dimensions the warp endpoint textures were last built for.
  int m_warp_width{0};
  int m_warp_height{0};
  /// Set when the tick or viewport changed and the warp endpoints must be
  /// regenerated before the next draw.
  bool m_warp_dirty{true};

  std::uint64_t m_last_ticks{0};
  std::size_t m_last_warp_passes{0};
  std::size_t m_last_draw_calls{0};
};

}  // namespace App::Interface
