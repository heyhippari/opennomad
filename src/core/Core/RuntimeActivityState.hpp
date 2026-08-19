#pragma once

namespace App {

/// Activation and suspension gates controlling whether the main loop may
/// execute a frame.
///
/// Mirrors the original engine's main-loop globals (address mapping in
/// docs/ReverseEngineering.md):
///   DAT_004e7694 -> render_window_active
///   DAT_0052dd58 -> application_active
///   DAT_0052dd4c -> updates_suspended
///   DAT_004c5944 -> reset_frame_timing_on_next_update
///
/// Pure logic: no SDL calls. The application keeps this state current from
/// SDL window/application events and evaluates may_run_frame() every loop
/// iteration.
struct RuntimeActivityState {
  /// Activation/focus state of the render target. False while the render
  /// window has lost keyboard focus or is minimised.
  ///
  /// Initialised true after window creation: SDL's SDL_WINDOW_INPUT_FOCUS
  /// flag is unreliable before the first present (Wayland only grants
  /// keyboard focus once the surface is mapped, and mapping requires a
  /// present), so gating the first frame on it would deadlock. Focus
  /// events correct the state from there.
  bool render_window_active{false};
  /// Foreground/background activation state of the application.
  bool application_active{false};
  /// Engine-controlled suspension, independent of focus and gameplay pause.
  bool updates_suspended{false};
  /// Set while the loop is blocked waiting for events; consumed and cleared
  /// by the next frame that actually executes (see
  /// clear_reset_flag_after_frame). Event processing alone never clears it.
  bool reset_frame_timing_on_next_update{false};

  /// True when a frame may execute: all three gates pass.
  [[nodiscard]] bool may_run_frame() const noexcept {
    return render_window_active && application_active && !updates_suspended;
  }

  /// Applies the render-window focus/activation transition.
  void on_render_window_active(const bool active) noexcept {
    render_window_active = active;
  }

  /// SDL_EVENT_WINDOW_SHOWN / SDL_EVENT_WINDOW_RESTORED: the render target
  /// became visible again.
  ///
  /// Never closes the gate: SDL delivers SHOWN for a freshly created window
  /// before Wayland has granted keyboard focus, and closing the gate here
  /// would deadlock the first frame (mapping requires a present, which the
  /// blocked loop never reaches). Focus transitions are the only events
  /// that close it; the gate opens once focus is actually held.
  void on_render_window_restored(const bool has_keyboard_focus) noexcept {
    if (has_keyboard_focus) {
      render_window_active = true;
    }
  }

  /// Applies the application foreground/background transition.
  void on_application_active(const bool active) noexcept {
    application_active = active;
  }

  /// Engine-controlled update suspension
  /// (see Application::set_updates_suspended).
  void set_updates_suspended(const bool suspended) noexcept {
    updates_suspended = suspended;
  }

  /// Clears the timing-reset flag. Only call after a frame has actually run.
  void clear_reset_flag_after_frame() noexcept {
    reset_frame_timing_on_next_update = false;
  }
};

}  // namespace App
