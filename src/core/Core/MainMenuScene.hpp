#pragma once

#include <expected>
#include <memory>
#include <string>

#include "Core/Input/InputManager.hpp"
#include "Core/Mesh.hpp"
#include "Core/Scene.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"

namespace App {

/// Native OpenNomad main-menu screen. This is a minimal, self-contained
/// menu state reached through the IAM/START startup path: it draws a static
/// backdrop; the full interactive menu is a later milestone.
class MainMenuScene final : public Scene {
 public:
  /// Builds the fullscreen backdrop quad and its shader/texture resources.
  static std::expected<std::unique_ptr<MainMenuScene>, std::string> create();

  ~MainMenuScene() override = default;

  MainMenuScene(const MainMenuScene&) = delete;
  MainMenuScene(MainMenuScene&&) = delete;
  MainMenuScene& operator=(MainMenuScene other) = delete;
  MainMenuScene& operator=(MainMenuScene&& other) = delete;

  void update(float delta_time, const Input::InputManager& input) override;
  void render() override;

 private:
  explicit MainMenuScene(Texture2D backdrop, Shader shader);

  Texture2D m_backdrop;
  Shader m_shader;
  Mesh m_quad;
};

}  // namespace App
