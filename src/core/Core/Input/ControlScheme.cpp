#include "ControlScheme.hpp"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Core/Input/Binding.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/InputSource.hpp"

namespace App::Input {

ControlScheme::ControlScheme(std::string name, const Device device)
    : m_name(std::move(name)), m_device(device) {}

ControlScheme& ControlScheme::add_binding(Binding binding) {
  m_bindings.push_back(binding);
  return *this;
}

void ControlScheme::set_enabled(const bool enabled) { m_enabled = enabled; }

bool ControlScheme::is_enabled() const { return m_enabled; }

const std::string& ControlScheme::name() const { return m_name; }

Device ControlScheme::device() const { return m_device; }

const std::vector<Binding>& ControlScheme::bindings() const { return m_bindings; }

ControlScheme ControlScheme::make_keyboard_mouse_default() {
  using App::Input::MouseAxis;

  ControlScheme scheme{"Keyboard + Mouse", Device::k_keyboard_mouse};
  scheme
      .add_binding({.action = Action::k_move_forward,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_W)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_move_forward,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_S)},
          .scale = -1.0F})
      .add_binding({.action = Action::k_move_right,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_D)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_move_right,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_A)},
          .scale = -1.0F})
      .add_binding({.action = Action::k_move_up,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_SPACE)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_move_up,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_LSHIFT)},
          .scale = -1.0F})
      .add_binding({.action = Action::k_look_yaw,
          .source = InputSource{.type = SourceType::k_mouse_axis,
              .index = static_cast<std::uint32_t>(MouseAxis::k_x)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_look_pitch,
          .source = InputSource{.type = SourceType::k_mouse_axis,
              .index = static_cast<std::uint32_t>(MouseAxis::k_y)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_toggle_lights,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_L)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_skip_video,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_ESCAPE)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_menu_up,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_UP)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_menu_down,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_DOWN)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_menu_confirm,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_RETURN)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_menu_confirm,
          .source = InputSource{.type = SourceType::k_mouse_button,
              .index = static_cast<std::uint32_t>(SDL_BUTTON_LEFT)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_menu_cancel,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_BACKSPACE)},
          .scale = 1.0F})
      .add_binding({.action = Action::k_menu_cancel,
          .source = InputSource{.type = SourceType::k_key,
              .index = static_cast<std::uint32_t>(SDL_SCANCODE_ESCAPE)},
          .scale = 1.0F});
  return scheme;
}

}  // namespace App::Input
