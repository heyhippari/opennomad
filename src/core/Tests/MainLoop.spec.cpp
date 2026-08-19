#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include "Core/MainLoopController.hpp"
#include "Core/RuntimeActivityState.hpp"
#include "Core/WindowSizeState.hpp"

namespace App {
namespace {

/// A focused, foreground, unsuspended application.
RuntimeActivityState make_active_state() {
  RuntimeActivityState state;
  state.on_render_window_active(true);
  state.on_application_active(true);
  return state;
}

}  // namespace
}  // namespace App

TEST_SUITE("Core::MainLoop") {
  using App::MainLoopAction;
  using App::MainLoopController;
  using App::MainLoopDecision;
  using App::RuntimeActivityState;

  TEST_CASE("A focused, foreground application can execute frames") {
    RuntimeActivityState state{App::make_active_state()};

    const MainLoopDecision decision{MainLoopController::advance(true, state)};
    CHECK(decision.action == MainLoopAction::k_run_frame);
    CHECK_FALSE(decision.reset_frame_timing);
  }

  TEST_CASE("Losing render-window focus blocks frame execution") {
    RuntimeActivityState state{App::make_active_state()};
    state.on_render_window_active(false);

    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);
  }

  TEST_CASE("Application backgrounding blocks frame execution") {
    RuntimeActivityState state{App::make_active_state()};
    state.on_application_active(false);

    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);
  }

  TEST_CASE("Explicit update suspension blocks frames even while focused and foreground") {
    RuntimeActivityState state{App::make_active_state()};
    state.set_updates_suspended(true);

    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);
    CHECK_FALSE(state.may_run_frame());
  }

  TEST_CASE("The inactive loop waits for events instead of busy-spinning") {
    RuntimeActivityState state{App::make_active_state()};
    state.on_render_window_active(false);

    // Every blocked iteration returns "wait": the loop blocks in the
    // platform's event wait rather than re-checking in a spin.
    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);
    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);
  }

  TEST_CASE("Focus or foreground restoration allows frames again") {
    RuntimeActivityState state{App::make_active_state()};
    state.on_application_active(false);
    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);

    state.on_application_active(true);
    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_run_frame);
  }

  TEST_CASE("A freshly shown window runs its first frame even before focus is granted") {
    // Startup state: the gates are initialised optimistically because
    // Wayland only grants keyboard focus after the first present. A SHOWN
    // event without focus must not close the gate, or the first frame
    // would never run and the surface would never be mapped.
    RuntimeActivityState state{App::make_active_state()};
    state.on_render_window_restored(false);
    CHECK(state.may_run_frame());
    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_run_frame);
  }

  TEST_CASE("Restoring a window opens the gate only once focus is held") {
    RuntimeActivityState state{App::make_active_state()};
    state.on_render_window_active(false);  // e.g. minimized or focus lost

    // RESTORED while unfocused must not re-open the gate...
    state.on_render_window_restored(false);
    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);

    // ...and must not have closed it for a window that was still active.
    RuntimeActivityState active{App::make_active_state()};
    active.on_render_window_restored(false);
    CHECK(active.may_run_frame());

    // RESTORED with focus re-opens it.
    state.on_render_window_restored(true);
    CHECK(state.may_run_frame());
  }

  TEST_CASE("The first frame after restoration receives a timing reset") {
    RuntimeActivityState state{App::make_active_state()};
    state.on_render_window_active(false);
    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);
    CHECK(state.reset_frame_timing_on_next_update);

    state.on_render_window_active(true);
    const MainLoopDecision decision{MainLoopController::advance(true, state)};
    CHECK(decision.action == MainLoopAction::k_run_frame);
    CHECK(decision.reset_frame_timing);
  }

  TEST_CASE("The reset flag remains set until a frame actually runs") {
    RuntimeActivityState state{App::make_active_state()};
    state.on_application_active(false);
    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);
    CHECK(state.reset_frame_timing_on_next_update);

    // Processing events that do not change the gates must not clear the
    // flag; only an executed frame clears it.
    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);
    CHECK(state.reset_frame_timing_on_next_update);

    // Reactivation alone (event processing, no frame) still keeps it set.
    state.on_application_active(true);
    CHECK(state.reset_frame_timing_on_next_update);
  }

  TEST_CASE("The reset flag is cleared after the resumed frame") {
    RuntimeActivityState state{App::make_active_state()};
    state.on_render_window_active(false);
    CHECK(MainLoopController::advance(true, state).action == MainLoopAction::k_wait_for_event);
    state.on_render_window_active(true);

    const MainLoopDecision decision{MainLoopController::advance(true, state)};
    CHECK(decision.action == MainLoopAction::k_run_frame);
    CHECK(decision.reset_frame_timing);

    // The frame executes, then the flag is cleared.
    state.clear_reset_flag_after_frame();
    CHECK_FALSE(state.reset_frame_timing_on_next_update);

    // The next frame runs without a reset.
    const MainLoopDecision next{MainLoopController::advance(true, state)};
    CHECK(next.action == MainLoopAction::k_run_frame);
    CHECK_FALSE(next.reset_frame_timing);
  }

  TEST_CASE("Quit or window close exits the loop and proceeds to shutdown") {
    RuntimeActivityState state{App::make_active_state()};

    CHECK(MainLoopController::advance(false, state).action == MainLoopAction::k_exit);
  }

  TEST_CASE("Escape uses held state and respects gameplay pause") {
    CHECK(MainLoopController::should_dispatch_escape(true, false));
    CHECK_FALSE(MainLoopController::should_dispatch_escape(false, false));
    CHECK_FALSE(MainLoopController::should_dispatch_escape(true, true));
    CHECK_FALSE(MainLoopController::should_dispatch_escape(false, true));
  }

  TEST_CASE("Relative mouse mode is disabled while inactive or explicitly suspended") {
    const RuntimeActivityState active{App::make_active_state()};
    CHECK(MainLoopController::should_enable_relative_mouse(true, true, active));
    CHECK_FALSE(MainLoopController::should_enable_relative_mouse(false, true, active));
    CHECK_FALSE(MainLoopController::should_enable_relative_mouse(true, false, active));

    RuntimeActivityState inactive{App::make_active_state()};
    inactive.on_render_window_active(false);
    CHECK_FALSE(MainLoopController::should_enable_relative_mouse(true, true, inactive));

    RuntimeActivityState suspended{App::make_active_state()};
    suspended.set_updates_suspended(true);
    CHECK_FALSE(MainLoopController::should_enable_relative_mouse(true, true, suspended));

    RuntimeActivityState backgrounded{App::make_active_state()};
    backgrounded.on_application_active(false);
    CHECK_FALSE(MainLoopController::should_enable_relative_mouse(true, true, backgrounded));
  }

  TEST_CASE("Resize events update the renderer's drawable dimensions") {
    App::WindowSizeState size;
    size.on_resized(800, 600);
    size.on_pixel_size_changed(1600, 1200);

    CHECK_EQ(size.width, 800);
    CHECK_EQ(size.height, 600);
    CHECK_EQ(size.pixel_width, 1600);
    CHECK_EQ(size.pixel_height, 1200);

    // A logical resize does not clobber the pixel-size cache.
    size.on_resized(1024, 768);
    CHECK_EQ(size.pixel_width, 1600);
    CHECK_EQ(size.pixel_height, 1200);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
