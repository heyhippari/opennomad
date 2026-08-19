#include "InputManager.hpp"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "Core/Input/Binding.hpp"
#include "Core/Input/ControlScheme.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/InputSource.hpp"
#include "Core/Input/RawInputState.hpp"

namespace App::Input {

namespace {

/// Contribution of a single binding given the frame's raw state.
float resolve_binding(const Binding& binding, const RawInputState& state) {
  switch (binding.source.type) {
    case SourceType::k_key: {
      const int key{static_cast<int>(binding.source.index)};
      if (key < 0 || key >= static_cast<int>(SDL_SCANCODE_COUNT)) {
        return 0.0F;
      }
      return state.key_down.at(static_cast<std::size_t>(key)) ? binding.scale : 0.0F;
    }
    case SourceType::k_mouse_button: {
      const std::size_t button{static_cast<std::size_t>(binding.source.index)};
      if (button >= state.mouse_button_down.size()) {
        return 0.0F;
      }
      return state.mouse_button_down.at(button) ? binding.scale : 0.0F;
    }
    case SourceType::k_mouse_axis: {
      const auto axis{static_cast<MouseAxis>(binding.source.index)};
      if (axis == MouseAxis::k_x) {
        return state.mouse_delta_x * binding.scale;
      }
      if (axis == MouseAxis::k_y) {
        return state.mouse_delta_y * binding.scale;
      }
      return 0.0F;
    }
    case SourceType::k_gamepad_button:
    case SourceType::k_gamepad_axis:
      // Reserved for a future gamepad control scheme; resolving these
      // requires a device layer (SDL_OpenGamepad) that does not exist yet.
      return 0.0F;
  }
  std::unreachable();
}

}  // namespace

InputManager::InputManager() {
  // Every action starts edge-triggered: mask bit set = pressed only on the
  // rising edge. The original's default g_inputEdgeMask value is unresolved.
  m_action_edge_mask.fill(true);
}

InputManager& InputManager::add_scheme(ControlScheme scheme) {
  m_schemes.push_back(std::move(scheme));
  return *this;
}

void InputManager::set_scheme_enabled(const std::string& name, const bool enabled) {
  for (ControlScheme& scheme : m_schemes) {
    if (scheme.name() == name) {
      scheme.set_enabled(enabled);
    }
  }
}

void InputManager::update(const RawInputState& state) {
  std::ranges::copy(m_action_values, m_previous_action_values.begin());
  std::ranges::fill(m_action_values, 0.0F);

  // Actions fed by a mouse axis keep their raw delta (unbounded per frame);
  // purely digital actions are clamped to [-1, 1].
  std::array<bool, k_action_count> mouse_axis_fed{};
  for (const ControlScheme& scheme : m_schemes) {
    if (!scheme.is_enabled()) {
      continue;
    }
    for (const Binding& binding : scheme.bindings()) {
      const std::size_t index{static_cast<std::size_t>(binding.action)};
      m_action_values.at(index) += resolve_binding(binding, state);
      if (binding.source.type == SourceType::k_mouse_axis) {
        mouse_axis_fed.at(index) = true;
      }
    }
  }

  for (std::size_t index{0}; index < m_action_values.size(); ++index) {
    if (!mouse_axis_fed.at(index)) {
      m_action_values.at(index) = std::clamp(m_action_values.at(index), -1.0F, 1.0F);
    }
  }

  // Recovered pressed-input calculation, evaluated on the mapped action
  // bits rather than on raw device state:
  //   pressed = current & ~(previous & edgeMask)
  // Mask bit set: the action appears in the pressed bitfield only on its
  // rising edge. Mask bit cleared: it stays pressed every frame while held.
  for (std::size_t index{0}; index < k_action_count; ++index) {
    const bool held{m_action_values.at(index) >= k_press_threshold};
    m_action_pressed.at(index) = held && !(m_previous_held.at(index) && m_action_edge_mask.at(index));
    m_previous_held.at(index) = held;
  }
}

float InputManager::get_action_value(const Action action) const {
  return m_action_values.at(static_cast<std::size_t>(action));
}

bool InputManager::is_action_pressed(const Action action) const {
  return m_action_pressed.at(static_cast<std::size_t>(action));
}

void InputManager::set_action_edge_mask(const Action action, const bool edge_only) {
  m_action_edge_mask.at(static_cast<std::size_t>(action)) = edge_only;
}

bool InputManager::is_action_released(const Action action) const {
  const std::size_t index{static_cast<std::size_t>(action)};
  return m_action_values.at(index) < k_press_threshold &&
         m_previous_action_values.at(index) >= k_press_threshold;
}

void InputManager::reset() {
  std::ranges::fill(m_action_values, 0.0F);
  std::ranges::fill(m_previous_action_values, 0.0F);
  std::ranges::fill(m_action_pressed, false);
  std::ranges::fill(m_previous_held, false);
}

void InputManager::reset_per_frame_input() {
  m_per_frame_input = 0;
}

std::uint32_t InputManager::per_frame_input() const {
  return m_per_frame_input;
}

}  // namespace App::Input
