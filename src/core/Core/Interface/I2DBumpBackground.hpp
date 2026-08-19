#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <vector>

#include "Core/Interface/I2DBumpEffect.hpp"
#include "Core/Interface/I2DPresentation.hpp"
#include "Core/Texture.hpp"
#include "Core/TextureR8.hpp"

namespace App {

class Shader;
class VertexArray;

}  // namespace App

namespace App::Interface {

/// Animated main-menu background reproducing Runtime's I2D bump effect
/// (source file C:\Omikron\Sources\omikron\I2D_Bump.c).
///
/// The recovered 256x256 CLOUD.BMP height field and the 30 Hz light state
/// stay on the CPU (65,536 lighting pixels per effect update). The final
/// full-resolution warp, palette lookup and native-resolution rasterization
/// run on the GPU, so the effect fills the entire physical viewport at the
/// drawable resolution instead of upscaling a 640x480 CPU frame.
class I2DBumpBackground {
 public:
  /// Loads IMAGES/CLOUD.BMP as an indexed height map and builds the GPU
  /// presentation resources. Requires a current GL context. A missing,
  /// unreadable or unsupported (non-256x256, non-8-bit indexed) source is an
  /// error so the caller can degrade explicitly rather than showing an
  /// invented asset.
  [[nodiscard]] static std::expected<std::unique_ptr<I2DBumpBackground>, std::string> create();

  I2DBumpBackground(const I2DBumpBackground&) = delete;
  I2DBumpBackground(I2DBumpBackground&&) = delete;
  I2DBumpBackground& operator=(const I2DBumpBackground&) = delete;
  I2DBumpBackground& operator=(I2DBumpBackground&&) = delete;
  ~I2DBumpBackground();

  /// Advances the effect in fixed 30 Hz original-effect ticks and uploads the
  /// 256x256 lit intensity field when at least one tick executed. At high
  /// host refresh rates most calls are no-ops; the final GPU draw still runs
  /// every frame.
  void update(float delta_time);

  /// Draws the procedural background over the entire physical viewport,
  /// rebuilding the per-viewport warp lookup tables when the effect state or
  /// the viewport size changed.
  void render(const I2DPresentationTransform& transform);

  /// Number of effect ticks advanced by the most recent update() call.
  [[nodiscard]] std::uint32_t last_ticks() const {
    return m_last_ticks;
  }

  /// Bytes uploaded to the GPU by the most recent update() call (the lit
  /// field; 0 when nothing changed).
  [[nodiscard]] std::size_t last_upload_bytes() const {
    return m_last_upload_bytes;
  }

  /// Read access to the recovered effect for diagnostics/tests.
  [[nodiscard]] const I2DBumpEffect& effect() const {
    return m_effect;
  }

 private:
  explicit I2DBumpBackground(I2DBumpEffect effect);

  /// Rebuilds the row/column warp lookup buffers for the current viewport
  /// and uploads them. Called lazily from render() when dirty.
  void rebuild_warp_tables(const I2DPresentationTransform& transform);

  I2DBumpEffect m_effect;
  /// 256x256 lit intensity field, uploaded as an R8 texture (64 KB).
  std::optional<TextureR8> m_lit_texture;
  /// Palette 64x1 RGBA8 sRGB lookup texture.
  std::optional<Texture2D> m_palette_texture;
  /// Per-physical-row warp offsets (pixel_height x 1, R8).
  std::optional<TextureR8> m_row_warp_texture;
  /// Per-physical-column warp offsets (pixel_width x 1, R8).
  std::optional<TextureR8> m_column_warp_texture;
  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<VertexArray> m_vertex_array;

  /// CPU staging for the warp lookup values (one byte per physical row/column).
  std::vector<std::uint8_t> m_row_warp_cpu;
  std::vector<std::uint8_t> m_column_warp_cpu;
  /// Dimensions the warp tables were last built for.
  int m_warp_width{0};
  int m_warp_height{0};
  /// Set when the effect state or viewport changed and the warp tables must
  /// be rebuilt before the next draw.
  bool m_warp_dirty{true};

  /// Accumulates host seconds and is drained in fixed 30 Hz effect ticks so
  /// the animation speed is independent of the host frame rate.
  float m_tick_accumulator{0.0F};
  std::uint32_t m_last_ticks{0};
  std::size_t m_last_upload_bytes{0};
};

}  // namespace App::Interface
