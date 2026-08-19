#include "Core/MainMenuScene.hpp"

#include <fmt/format.h>
#include <glad/glad.h>

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Mesh.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/Vertex.hpp"

namespace App {

namespace {

/// Unit quad in NDC: two triangles with CCW winding.
std::vector<Vertex> make_quad_vertices() {
  return {
      Vertex{.position = {-1.0F, -1.0F, 0.0F}, .uv = {0.0F, 0.0F}},
      Vertex{.position = {1.0F, -1.0F, 0.0F}, .uv = {1.0F, 0.0F}},
      Vertex{.position = {1.0F, 1.0F, 0.0F}, .uv = {1.0F, 1.0F}},
      Vertex{.position = {-1.0F, 1.0F, 0.0F}, .uv = {0.0F, 1.0F}},
  };
}

std::vector<std::uint32_t> make_quad_indices() {
  return {0U, 1U, 2U, 0U, 2U, 3U};
}

/// A single opaque white pixel used to tint the backdrop through u_tint.
constexpr std::array<std::uint8_t, 4> K_WHITE_PIXEL{255U, 255U, 255U, 255U};

}  // namespace

std::expected<std::unique_ptr<MainMenuScene>, std::string> MainMenuScene::create() {
  APP_PROFILE_FUNCTION();

  auto backdrop{
      Texture2D::create(1, 1, std::span<const std::uint8_t>{K_WHITE_PIXEL}, /*srgb=*/false)};
  if (!backdrop) {
    return std::expected<std::unique_ptr<MainMenuScene>, std::string>{
        std::unexpect, fmt::format("MainMenu: {}", backdrop.error())};
  }

  // The constructor is private; only the factory may build a scene.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<MainMenuScene>{
      new MainMenuScene(std::move(backdrop).value(), Shader::create_default())};
}

MainMenuScene::MainMenuScene(Texture2D backdrop, Shader shader)
    : m_backdrop(std::move(backdrop)),
      m_shader(std::move(shader)),
      m_quad(make_quad_vertices(), make_quad_indices()) {}

void MainMenuScene::update(const float /*delta_time*/, const Input::InputManager& /*input*/) {
  // Static backdrop; interaction arrives in a later milestone.
}

void MainMenuScene::render() {
  APP_PROFILE_FUNCTION();

  glClearColor(0.03F, 0.05F, 0.09F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT);

  // Identity transform: the unit quad fills the viewport.
  constexpr std::array<GLfloat, 16> mvp{1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      1.0F};
  const std::array<GLfloat, 4> tint{0.06F, 0.10F, 0.18F, 1.0F};

  m_shader.bind();
  m_shader.set_uniform_mat4("u_mvp", std::span<const GLfloat, 16>{mvp});
  m_shader.set_uniform_int("u_texture0", 0);
  m_shader.set_uniform_vec4("u_tint", std::span<const GLfloat, 4>{tint});
  m_backdrop.bind(0);
  m_quad.draw();
  Texture2D::unbind();
  Shader::unbind();
}

}  // namespace App
