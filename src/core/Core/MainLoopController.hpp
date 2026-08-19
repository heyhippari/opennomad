#pragma once

#include <cstdint>

#include "Core/RuntimeActivityState.hpp"

namespace App {

/// Next action for the idle-driven main loop, mirroring the original
/// Windows message loop (see docs/ReverseEngineering.md):
/// - drain queued events,
/// - run a frame only when every activity gate passes and the queue is empty,
/// - otherwise mark timing for reset and block in the platform's
///   wait-for-event facility instead of busy-spinning,
/// - exit on quit.
enum class MainLoopAction : std::uint8_t {
  /// Execute one frame. reset_frame_timing is true on the first frame after
  /// the loop was blocked.
  k_run_frame,
  /// Block waiting for an event; do not run a frame.
  k_wait_for_event,
  /// A quit/close event was processed; leave the loop and tear down.
  k_exit,
};

/// What the loop should do next.
struct MainLoopDecision {
  MainLoopAction action{MainLoopAction::k_exit};
  /// True when the frame must re-baseline its timing because the loop was
  /// blocked since the previous frame.
  bool reset_frame_timing{false};
};

/// Stateless decision logic for the idle-driven main loop.
///
/// Pure logic: no SDL calls, so the sequencing rules are unit-testable.
class MainLoopController {
 public:
  /// Evaluates the gates and returns the loop's next action.
  ///
  /// While blocked, sets reset_frame_timing_on_next_update so the resumed
  /// frame discards the inactive interval. The flag is only cleared by
  /// RuntimeActivityState::clear_reset_flag_after_frame after a frame runs.
  [[nodiscard]] static MainLoopDecision advance(bool running, RuntimeActivityState& state) {
    if (!running) {
      return {.action = MainLoopAction::k_exit, .reset_frame_timing = false};
    }
    if (state.may_run_frame()) {
      return {.action = MainLoopAction::k_run_frame,
          .reset_frame_timing = state.reset_frame_timing_on_next_update};
    }
    state.reset_frame_timing_on_next_update = true;
    return {.action = MainLoopAction::k_wait_for_event, .reset_frame_timing = false};
  }

  /// The original main loop runs a per-frame held-key test:
  /// `if (EscapeHeld && !gamePaused) DispatchGameCommand(0x1F, -1, -1);`
  /// (GetAsyncKeyState, so a held test rather than a key-down event).
  [[nodiscard]] static bool should_dispatch_escape(bool escape_held, bool game_paused) noexcept {
    return escape_held && !game_paused;
  }

  /// Relative mouse mode is only appropriate while the application may
  /// execute frames (focused, foreground, not suspended); it is released
  /// otherwise so the cursor stays usable while the loop waits.
  [[nodiscard]] static bool should_enable_relative_mouse(
      bool mouse_captured, bool window_focused, const RuntimeActivityState& state) noexcept {
    return mouse_captured && window_focused && state.may_run_frame();
  }
};

}  // namespace App
