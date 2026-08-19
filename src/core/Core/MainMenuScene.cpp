#include "Core/MainMenuScene.hpp"

#include <memory>

#include "Core/Interface/InterfaceManager.hpp"

namespace App {

std::unique_ptr<MainMenuScene> MainMenuScene::create(Interface::InterfaceManager& manager) {
  // The constructor is private; only the factory may build a scene.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<MainMenuScene>{new MainMenuScene(manager)};
}

MainMenuScene::MainMenuScene(Interface::InterfaceManager& manager) : m_manager(&manager) {}

void MainMenuScene::update(const float delta_time, const Input::InputManager& input) {
  if (m_manager != nullptr) {
    m_manager->update(delta_time, input);
  }
}

void MainMenuScene::render() {
  if (m_manager == nullptr || m_width <= 0 || m_height <= 0) {
    return;
  }
  m_manager->render(m_width, m_height);
}

void MainMenuScene::resize(const int width, const int height) {
  m_width = width;
  m_height = height;
}

}  // namespace App
