#include "Renderer.hpp"

#include <glad/glad.h>

#include <array>

#include "Core/Debug/Instrumentor.hpp"

namespace App {

Renderer::Renderer() {
  APP_PROFILE_FUNCTION();
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Renderer::init() {
  APP_PROFILE_FUNCTION();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_STENCIL_TEST);
  glDisable(GL_FRAMEBUFFER_SRGB);
  // Runtime's low-bit-depth target used dithering. OpenNomad deliberately
  // omits it for deterministic high-precision color targets.
  glDisable(GL_DITHER);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glEnable(GL_MULTISAMPLE);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Renderer::begin_frame(const int width, const int height) {
  APP_PROFILE_FUNCTION();

  glViewport(0, 0, width, height);
  constexpr std::array<float, 4> k_clear_color{clear_color()};
  glClearColor(
      k_clear_color.at(0), k_clear_color.at(1), k_clear_color.at(2), k_clear_color.at(3));
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void Renderer::end_frame() {
  // Swap is handled by Window because ImGui needs to render into the
  // default framebuffer after the scene.
}

}  // namespace App
