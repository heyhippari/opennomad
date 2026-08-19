#pragma once

#include <SDL3/SDL_scancode.h>

#include <array>

namespace App::Input {

/// Number of mouse buttons tracked per frame. SDL currently defines buttons
/// 1..5 (left..X2); the slack keeps room for future button ids.
inline constexpr int k_mouse_button_count{8};

/// Per-frame snapshot of every device the input manager reads from.
///
/// Filled by the application once per frame from SDL and consumed by
/// InputManager::update. Plain data: unit tests construct it directly, and
/// the manager itself performs no SDL calls.
struct RawInputState {
  /// True for every key currently held, indexed by SDL_Scancode.
  std::array<bool, SDL_SCANCODE_COUNT> key_down{};
  /// Accumulated mouse motion since the previous frame, in pixels.
  float mouse_delta_x{0.0F};
  float mouse_delta_y{0.0F};
  /// True for every mouse button currently held, indexed by SDL button id
  /// (1 = left). The id 0 entry is unused.
  std::array<bool, k_mouse_button_count> mouse_button_down{};
};

}  // namespace App::Input
