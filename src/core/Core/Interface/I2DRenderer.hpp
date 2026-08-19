#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include <glad/glad.h>

#include "Core/Buffers.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/VertexArray.hpp"

namespace App::Interface {

struct InterfaceInstance;
class FontManager;

/// Generic I2D renderer: draws the interface's animated background, bitmap
/// elements and text elements into a 640x480 virtual canvas, mapping that
/// canvas to the physical window with aspect-preserving letterboxing.
///
/// The renderer operates on generic I2D data only; it performs no menu-
/// specific drawing and never loads resources itself.
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
  /// `pixel_height` (the drawable framebuffer size).
  void render(const InterfaceInstance& instance,
      const FontManager& fonts,
      int pixel_width,
      int pixel_height);

 private:
  /// Sets the viewport/projection for the 640x480 virtual canvas.
  void begin_canvas(int pixel_width, int pixel_height);

  /// Restores the GL state modified by the I2D pass.
  static void end_canvas();

  void draw_quad(const Texture2D& texture,
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

  std::unique_ptr<Shader> m_shader;
  std::unique_ptr<VertexArray> m_vertex_array;
  std::unique_ptr<VertexBuffer> m_vertex_buffer;
  std::unique_ptr<IndexBuffer> m_index_buffer;
  bool m_initialized{false};
};

}  // namespace App::Interface
