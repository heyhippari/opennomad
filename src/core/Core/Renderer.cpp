#include "Renderer.hpp"

#include <glad/glad.h>

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
  glEnable(GL_FRAMEBUFFER_SRGB);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glEnable(GL_MULTISAMPLE);
}

void Renderer::begin_frame(const int width, const int height) {
  APP_PROFILE_FUNCTION();

  glViewport(0, 0, width, height);
  glClearColor(m_clear_color[0], m_clear_color[1], m_clear_color[2], m_clear_color[3]);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void Renderer::end_frame() {
  // Swap is handled by Window because ImGui needs to render into the
  // default framebuffer after the scene.
}

}  // namespace App
