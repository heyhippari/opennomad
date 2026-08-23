#include "Core/Interface/InterfacePresenter.hpp"

#include <cstddef>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/Interface/InterfaceManager.hpp"

namespace App::Interface {

InterfacePresenter::InterfacePresenter(InterfaceManager& manager) : m_manager(&manager) {}

void InterfacePresenter::update(const float delta_seconds, const Input::InputManager& input) {
  APP_PROFILE_FUNCTION();

  if (m_manager != nullptr) {
    m_manager->update(delta_seconds, input);
  }
}

void InterfacePresenter::update_without_input(const float delta_seconds) {
  APP_PROFILE_FUNCTION();

  if (m_manager != nullptr) {
    m_manager->update_without_input(delta_seconds);
  }
}

void InterfacePresenter::render(const int pixel_width, const int pixel_height) {
  APP_PROFILE_FUNCTION();

  if (m_manager != nullptr) {
    m_manager->render(pixel_width, pixel_height);
  }
}

void InterfacePresenter::render_dialog(const Dialog::DialogPresentation& dialog,
    const std::size_t selected_choice,
    const int pixel_width,
    const int pixel_height) {
  APP_PROFILE_FUNCTION();

  if (m_manager != nullptr) {
    m_manager->render_dialog(dialog, selected_choice, pixel_width, pixel_height);
  }
}

}  // namespace App::Interface
