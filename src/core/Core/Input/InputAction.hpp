#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

namespace App::Input {

/// Semantic input actions consumed by gameplay code.
///
/// Scenes read action *values* (axes, resolved by InputManager) or action
/// *edges* (InputManager::is_action_pressed / is_action_released) and never
/// see raw devices. Every control scheme maps its physical inputs onto this
/// shared vocabulary, so adding a gamepad scheme or a new action (jump,
/// interact, ...) never changes consumers.
enum class Action : std::uint8_t {
  /// Forward/backward movement axis (positive = forward).
  k_move_forward,
  /// Strafe axis (positive = right).
  k_move_right,
  /// Vertical movement axis (positive = up).
  k_move_up,
  /// Look-yaw delta (positive = mouse moved right).
  k_look_yaw,
  /// Look-pitch delta (positive = mouse moved down; consumers may invert).
  k_look_pitch,
  /// Debug toggle: switch the scene's own lights on/off (L).
  k_toggle_lights,
  /// Skips the currently playing startup video (Escape).
  k_skip_video,
};

/// Number of entries in Action. Keep in sync with the enum above — the
/// static_assert turns a forgotten update into a compile error.
inline constexpr std::size_t k_action_count{7};
static_assert(k_action_count == std::to_underlying(Action::k_skip_video) + 1U);

}  // namespace App::Input
