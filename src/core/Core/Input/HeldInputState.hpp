#pragma once

#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "Core/Input/RawInputState.hpp"

namespace App::Input {

/// Held-state tracker for keyboard keys and mouse buttons, updated from
/// per-event down/up notifications and cleared on focus loss.
///
/// Corresponds to the original engine's per-key state maintained by the
/// window procedure (see docs/ReverseEngineering.md):
///   DAT_0052c4f0[key & 0xFF] = 1 on WM_KEYDOWN, 0 on WM_KEYUP
/// plus the leftMouseButtonDown / rightMouseButtonDown globals. The original
/// array holds 256 DirectInput key codes; OpenNomad indexes SDL scancodes
/// (SDL_SCANCODE_COUNT entries) because no Omikron key mapping exists yet.
///
/// Pure logic: fed by the application from SDL events, no SDL calls.
class HeldInputState {
 public:
  /// Records a key transition. Repeats simply re-assert the held state.
  void on_key_down(const SDL_Scancode scancode) {
    m_keys.at(key_index(scancode)) = true;
  }
  void on_key_up(const SDL_Scancode scancode) {
    m_keys.at(key_index(scancode)) = false;
  }

  /// Records a mouse button transition. Button ids follow SDL (1 = left).
  /// Ids at or above k_mouse_button_count are ignored.
  void on_mouse_button_down(const std::uint32_t button) {
    if (button < static_cast<std::uint32_t>(k_mouse_button_count)) {
      m_buttons.at(static_cast<std::size_t>(button)) = true;
    }
  }
  void on_mouse_button_up(const std::uint32_t button) {
    if (button < static_cast<std::uint32_t>(k_mouse_button_count)) {
      m_buttons.at(static_cast<std::size_t>(button)) = false;
    }
  }

  /// True while the key is held.
  [[nodiscard]] bool key_down(const SDL_Scancode scancode) const {
    return m_keys.at(key_index(scancode));
  }

  /// True while the button is held.
  [[nodiscard]] bool mouse_button_down(const std::uint32_t button) const {
    return button < static_cast<std::uint32_t>(k_mouse_button_count) &&
           m_buttons.at(static_cast<std::size_t>(button));
  }

  /// Held state of the Escape key, for the per-frame held-key test.
  [[nodiscard]] bool escape_held() const {
    return m_keys.at(key_index(SDL_SCANCODE_ESCAPE));
  }

  /// Drops all held state. Call when the render window loses focus so keys
  /// and buttons cannot remain stuck (the original clears on WM_KILLFOCUS).
  void clear() {
    m_keys.fill(false);
    m_buttons.fill(false);
  }

  /// Writes the held state into a frame snapshot.
  void fill(RawInputState& out) const {
    out.key_down = m_keys;
    out.mouse_button_down = m_buttons;
  }

 private:
  [[nodiscard]] static std::size_t key_index(const SDL_Scancode scancode) {
    return static_cast<std::size_t>(scancode);
  }

  std::array<bool, SDL_SCANCODE_COUNT> m_keys{};
  std::array<bool, k_mouse_button_count> m_buttons{};
};

}  // namespace App::Input
