#pragma once

#include <glad/glad.h>

#include <array>
#include <cstdint>

namespace App {

/// Owns the OpenGL render state and provides a simple begin/end frame API.
class Renderer {
 public:
  Renderer();
  ~Renderer() = default;

  Renderer(const Renderer&) = delete;
  Renderer(Renderer&&) = delete;
  Renderer& operator=(Renderer other) = delete;
  Renderer& operator=(Renderer&& other) = delete;

  /// Initialise the OpenGL state machine (depth, stencil, culling, etc.).
  void init();

  /// Clear the framebuffer and set the viewport for the current frame.
  void begin_frame(int width, int height);

  /// Placeholder for any end-of-frame work (currently a no-op — swap lives in Window).
  void end_frame();

  /// Shared scene clear color, including offscreen WorldScene composition.
  [[nodiscard]] static constexpr std::array<float, 4> clear_color() {
    return {0.0F, 0.0F, 0.0F, 1.0F};
  }
};

}  // namespace App
