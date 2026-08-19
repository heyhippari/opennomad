#pragma once

#include <cstdint>

namespace App::Input {

/// Kinds of physical input a binding can read from.
///
/// Keyboard and mouse are implemented today. The gamepad entries reserve the
/// vocabulary so future control schemes can be added without changing this
/// enum; resolving them requires a device layer (SDL_OpenGamepad) that does
/// not exist yet.
enum class SourceType : std::uint8_t {
  /// A keyboard key; the index is an SDL_Scancode.
  k_key,
  /// A mouse button; the index is an SDL mouse button id (1 = left).
  k_mouse_button,
  /// A mouse movement axis; the index is a MouseAxis.
  k_mouse_axis,
  /// A gamepad button; the index is an SDL_GamepadButton. Not resolved yet.
  k_gamepad_button,
  /// A gamepad stick/trigger axis; the index is an SDL_GamepadAxis. Not resolved yet.
  k_gamepad_axis,
};

/// Mouse movement axes usable as binding sources.
enum class MouseAxis : std::uint8_t { k_x, k_y };

/// A physical input a binding reads from.
struct InputSource {
  SourceType type{SourceType::k_key};
  std::uint32_t index{0};
};

}  // namespace App::Input
