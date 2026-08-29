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
  /// Interface navigation: move the selection up (previous entry).
  k_menu_up,
  /// Interface navigation: move the selection down (next entry).
  k_menu_down,
  /// Interface navigation: decrease/previous value for the selected entry.
  k_menu_left,
  /// Interface navigation: increase/next value for the selected entry.
  k_menu_right,
  /// Interface navigation: activate the selected entry.
  k_menu_confirm,
  /// Interface navigation: return to the parent state / cancel.
  k_menu_cancel,
  /// CTL player-input profile slots 0..13 (Runtime's profile-0 canonical
  /// action bits 0x0001..0x2000). These are lower-level than any one CTL
  /// bank's interpretation; the CTL controller consumes only the resulting
  /// 32-bit mask. Retail keyboard defaults: Left/Right/Up/Down arrows, E, R,
  /// D, F, Left Ctrl, Space, G, H, Left Shift, Tab.
  k_ctl_slot_0,
  k_ctl_slot_1,
  k_ctl_slot_2,
  k_ctl_slot_3,
  k_ctl_slot_4,
  k_ctl_slot_5,
  k_ctl_slot_6,
  k_ctl_slot_7,
  k_ctl_slot_8,
  k_ctl_slot_9,
  k_ctl_slot_10,
  k_ctl_slot_11,
  k_ctl_slot_12,
  k_ctl_slot_13,
};

/// Number of entries in Action. Keep in sync with the enum above — the
/// static_assert turns a forgotten update into a compile error.
inline constexpr std::size_t k_action_count{27};
static_assert(k_action_count == std::to_underlying(Action::k_ctl_slot_13) + 1U);

/// CTL profile slot of one k_ctl_slot_* action.
[[nodiscard]] constexpr std::size_t ctl_slot_index(const Action action) {
  return static_cast<std::size_t>(
      std::to_underlying(action) - std::to_underlying(Action::k_ctl_slot_0));
}

}  // namespace App::Input
