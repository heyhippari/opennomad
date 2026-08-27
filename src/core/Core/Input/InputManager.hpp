#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Core/Input/ControlScheme.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/InputSource.hpp"
#include "Core/Input/RawInputState.hpp"

namespace App::Input {

/// Resolves the enabled control schemes against a per-frame RawInputState
/// into action values.
///
/// Pure logic: no SDL calls, no window access. The application feeds it one
/// RawInputState per frame; scenes and controllers read the results. Adding
/// a gamepad means adding a scheme and a device layer, not changing
/// consumers.
class InputManager {
 public:
  /// Constructs a manager with every action edge-triggered by default
  /// (the original's default g_inputEdgeMask value is unresolved).
  InputManager();

  InputManager(const InputManager&) = delete;
  InputManager(InputManager&&) = default;
  InputManager& operator=(const InputManager&) = delete;
  InputManager& operator=(InputManager&&) = default;
  ~InputManager() = default;

  InputManager& add_scheme(ControlScheme scheme);

  /// Enables or disables a scheme by name; unknown names are ignored.
  void set_scheme_enabled(const std::string& name, bool enabled);

  /// Recomputes every action value from the frame's raw device state, then
  /// updates the per-frame pressed bitfield (see is_action_pressed).
  void update(const RawInputState& state);

  /// Current value of an action. Keyboard/button-driven actions are clamped
  /// to [-1, 1]; actions fed by a mouse axis keep their raw delta value
  /// (mouse motion is unbounded per frame).
  [[nodiscard]] float get_action_value(Action action) const;

  /// Per-frame pressed bitfield, recovered from the original
  /// `g_pressedInput = current & ~(previous & edgeMask)` evaluated on the
  /// mapped action bits. With the edge-mask bit set (default) an action
  /// appears here only on its rising edge; with the mask bit cleared it
  /// stays pressed every frame while held.
  [[nodiscard]] bool is_action_pressed(Action action) const;

  /// Sets the recovered per-bit edge mask (`g_inputEdgeMask`) for one
  /// action. The original's default mask value is unresolved; OpenNomad
  /// defaults every action to edge-triggered.
  void set_action_edge_mask(Action action, bool edge_only);

  /// True on the frame an action's value crossed below k_press_threshold.
  /// Value-edge based, independent of the pressed bitfield above.
  [[nodiscard]] bool is_action_released(Action action) const;

  /// First keyboard key or mouse button that rose from released to held during
  /// the most recent raw-device snapshot. This deliberately bypasses semantic
  /// actions so a binding UI can capture the key that is about to *become* an
  /// action. Keyboard wins when key and mouse edges occur in the same frame.
  [[nodiscard]] std::optional<InputSource> last_physical_press() const;

  /// Clears all values and edge state (e.g. when switching scenes).
  /// Edge-mask configuration is preserved.
  void reset();

  /// Resets the neutral per-frame input field, recovered from the original
  /// clearing `DAT_0090e0e0 = 0` before the engine callback. Its purpose
  /// and consumer remain unresolved; the field exists only to preserve the
  /// reset point without inventing semantics.
  void reset_per_frame_input();
  [[nodiscard]] std::uint32_t per_frame_input() const;

 private:
  /// A value crossing this threshold counts as a digital "press".
  static constexpr float k_press_threshold{0.5F};

  std::vector<ControlScheme> m_schemes;
  std::array<float, k_action_count> m_action_values{};
  std::array<float, k_action_count> m_previous_action_values{};
  /// Per-bit edge mask recovered from `g_inputEdgeMask`: true = edge-only.
  std::array<bool, k_action_count> m_action_edge_mask{};
  /// Per-frame pressed bitfield recovered from `g_pressedInput`.
  std::array<bool, k_action_count> m_action_pressed{};
  /// Previous frame's held state, recovered from `g_previousInputState`.
  std::array<bool, k_action_count> m_previous_held{};
  RawInputState m_previous_raw_state{};
  std::optional<InputSource> m_last_physical_press;
  /// Neutral per-frame input field recovered from `DAT_0090e0e0`;
  /// purpose and consumer unresolved (see reset_per_frame_input).
  std::uint32_t m_per_frame_input{0};
};

}  // namespace App::Input
