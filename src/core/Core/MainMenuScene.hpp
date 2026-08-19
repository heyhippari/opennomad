#pragma once

#include <memory>

#include "Core/Input/InputManager.hpp"
#include "Core/Scene.hpp"

namespace App::Interface {
class InterfaceManager;
}

namespace App {

/// Adapter/host scene for the generic interface system. Interface 29 is the
/// active interface; this scene delegates update and render to the
/// InterfaceManager and owns no menu composition data (labels, positions,
/// fonts, artwork or layout all live in the interface system).
class MainMenuScene final : public Scene {
 public:
  static std::unique_ptr<MainMenuScene> create(Interface::InterfaceManager& manager);

  ~MainMenuScene() override = default;

  MainMenuScene(const MainMenuScene&) = delete;
  MainMenuScene(MainMenuScene&&) = delete;
  MainMenuScene& operator=(const MainMenuScene&) = delete;
  MainMenuScene& operator=(MainMenuScene&&) = delete;

  void update(float delta_time, const Input::InputManager& input) override;
  void render() override;
  void resize(int width, int height) override;

 private:
  explicit MainMenuScene(Interface::InterfaceManager& manager);

  Interface::InterfaceManager* m_manager{nullptr};
  int m_width{640};
  int m_height{480};
};

}  // namespace App
