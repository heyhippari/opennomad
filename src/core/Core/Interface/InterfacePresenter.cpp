#include "Core/Interface/InterfacePresenter.hpp"

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Interface/InterfaceManager.hpp"

namespace App::Interface {

InterfacePresenter::InterfacePresenter(InterfaceManager& manager) : m_manager(&manager) {}

void InterfacePresenter::update(const float delta_seconds, const Input::InputManager& input) {
  APP_PROFILE_FUNCTION();

  if (m_manager != nullptr) {
    m_manager->update(delta_seconds, input);
  }
}

void InterfacePresenter::render(const int pixel_width, const int pixel_height) {
  APP_PROFILE_FUNCTION();

  if (m_manager != nullptr) {
    m_manager->render(pixel_width, pixel_height);
  }
}

}  // namespace App::Interface
