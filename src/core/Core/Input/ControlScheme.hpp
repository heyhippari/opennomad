#pragma once

#include <string>
#include <vector>

#include "Core/Input/Binding.hpp"

namespace App::Input {

/// Device family a control scheme is written for.
///
/// Schemes are interchangeable alternatives for the same action vocabulary:
/// gameplay code stays identical whether keyboard+mouse or gamepad input
/// drives it.
enum class Device : std::uint8_t { k_keyboard_mouse, k_gamepad };

/// A named, enableable set of bindings. The InputManager resolves every
/// enabled scheme each frame; several enabled schemes act as hybrid input.
class ControlScheme {
 public:
  ControlScheme(std::string name, Device device);

  ControlScheme(const ControlScheme&) = delete;
  ControlScheme(ControlScheme&&) = default;
  ControlScheme& operator=(const ControlScheme&) = delete;
  ControlScheme& operator=(ControlScheme&&) = default;
  ~ControlScheme() = default;

  ControlScheme& add_binding(Binding binding);

  void set_enabled(bool enabled);
  [[nodiscard]] bool is_enabled() const;

  [[nodiscard]] const std::string& name() const;
  [[nodiscard]] Device device() const;
  [[nodiscard]] const std::vector<Binding>& bindings() const;

  /// The default keyboard-and-mouse scheme following modern conventions:
  /// WASD for horizontal movement, Space/LeftShift for vertical movement and
  /// the mouse axes for look.
  [[nodiscard]] static ControlScheme make_keyboard_mouse_default();

 private:
  std::string m_name;
  Device m_device{Device::k_keyboard_mouse};
  std::vector<Binding> m_bindings;
  bool m_enabled{true};
};

}  // namespace App::Input
