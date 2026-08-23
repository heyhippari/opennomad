#pragma once

#include <glad/glad.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "Core/Buffers.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/I2DPresentation.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/Vertex.hpp"
#include "Core/VertexArray.hpp"

namespace App::Interface {

struct InterfaceInstance;
class FontManager;

/// Generic I2D renderer: draws the interface's animated background, bitmap
/// elements and text elements using the modern presentation transform (the
/// recovered 640x480 coordinate system is the layout space, not the physical
/// framebuffer). Quads sharing one texture and blit state are batched into a
/// single VBO upload and one draw call.
class I2DRenderer {
 public:
  I2DRenderer() = default;
  ~I2DRenderer() = default;

  I2DRenderer(const I2DRenderer&) = delete;
  I2DRenderer(I2DRenderer&&) = delete;
  I2DRenderer& operator=(const I2DRenderer&) = delete;
  I2DRenderer& operator=(I2DRenderer&&) = delete;

  /// Compiles the shader and builds the quad VAO/VBO/IBO. Requires a current
  /// GL context.
  [[nodiscard]] std::expected<void, std::string> initialize();

  [[nodiscard]] bool valid() const {
    return m_initialized;
  }

  /// Renders the active state of `instance` into `pixel_width` x
  /// `pixel_height` (the drawable framebuffer size), accumulating per-frame
  /// counters into `counters`.
  void render(const InterfaceInstance& instance,
      FontManager& fonts,
      int pixel_width,
      int pixel_height,
      Debug::I2DCounters& counters);

  /// Draws a presentation-only full-screen colour overlay after all recovered
  /// I2D content. Used only when an interface descriptor explicitly opts in.
  void render_overlay(
      const std::array<float, 3>& color, float alpha, int pixel_width, int pixel_height);

  /// Renders the active gameplay dialog as a modest resolution-independent
  /// subtitle/choice layer. This is ordinary I2D presentation, not ImGui.
  void render_dialog(const Dialog::DialogPresentation& dialog,
      std::size_t selected_choice,
      FontManager& fonts,
      int pixel_width,
      int pixel_height,
      Debug::I2DCounters& counters);

 private:
  /// One contiguous run of quads sharing a texture and source-key state.
  struct DrawCommand {
    std::size_t first_index{0};
    std::size_t index_count{0};
    const Texture2D* texture{nullptr};
    bool source_colour_key{false};
    std::array<float, 3> key{0.0F, 0.0F, 0.0F};
  };

  /// Emits one quad into the current batch, flushing when the render state
  /// changes. All coordinates/UVs are in the 640x480 reference space (the
  /// projection maps them to the full viewport).
  void push_quad(const Texture2D& texture,
      float x0,
      float y0,
      float x1,
      float y1,
      float u0,
      float v0,
      float u1,
      float v1,
      std::array<float, 4> tint,
      const I2DBlitOptions& blit_options);

  /// Closes the current draw command (records its index range).
  void flush_command();

  /// Uploads all vertices/indices once and issues one draw call per command.
  void flush();

  /// Resets the per-frame batch state.
  void reset();

  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<Shader> m_overlay_shader;
  std::unique_ptr<VertexArray> m_vertex_array;
  std::unique_ptr<VertexBuffer> m_vertex_buffer;
  std::unique_ptr<IndexBuffer> m_index_buffer;
  bool m_initialized{false};

  /// Reused per-frame storage (never released between frames).
  std::vector<Vertex> m_vertices;
  std::vector<std::uint32_t> m_indices;
  std::vector<DrawCommand> m_commands;

  /// Current batch state.
  const Texture2D* m_current_texture{nullptr};
  bool m_current_keyed{false};
  std::array<float, 3> m_current_key{0.0F, 0.0F, 0.0F};
  std::size_t m_current_first_index{0};
};

}  // namespace App::Interface
