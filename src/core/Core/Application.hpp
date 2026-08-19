#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/FrameTiming.hpp"
#include "Core/Input/HeldInputState.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Input/TextInputState.hpp"
#include "Core/RuntimeActivityState.hpp"
#include "Core/Scenario/ScenarioEngine.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scene.hpp"
#include "Core/Startup/StartupCoordinator.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"
#include "Core/Window.hpp"

namespace App {

namespace Interface {
class InterfaceManager;
}

class Application {
 public:
  /// Initialises SDL, creates the window and audio device, and builds the
  /// default scene. Audio failure is non-fatal (logged and skipped); any
  /// other failure is returned as an error.
  static std::expected<Application, std::string> create(const std::string& title);

  ~Application();

  Application(const Application&) = delete;
  Application(Application&& other) noexcept;
  Application& operator=(Application other) = delete;
  Application& operator=(Application&& other) = delete;

  void run();
  void stop();

  /// Engine-controlled update suspension, independent of application focus
  /// and gameplay pause. While suspended the main loop blocks in the event
  /// wait and the next executed frame re-baselines its timing.
  ///
  /// The application is strictly single-threaded: a future cross-thread
  /// caller must also wake the blocked event loop (by posting an event)
  /// for the suspension to take effect promptly.
  void set_updates_suspended(bool suspended);

  /// Enables or disables text-input capture (see Input::TextInputState).
  /// SDL text-input activation is currently coordinated by the ImGui
  /// backend; a future consumer of the captured text must coordinate
  /// SDL_StartTextInput/SDL_StopTextInput with it (docs/ReverseEngineering.md).
  void set_text_capture_enabled(bool enabled);

  /// Sets the time-scale mode of the recovered frame timing
  /// (FrameTiming::TimeScaleMode). Debug/test control; no UI is wired yet.
  void set_time_scale_mode(FrameTiming::TimeScaleMode mode);

  /// Forces every timed frame's delta (Omikron units, 1.0 = 1/30 s) until
  /// cleared — the recovered g_forcedDeltaTime without the -1.0 sentinel.
  /// Debug/test control; no UI is wired yet.
  void set_forced_delta(std::optional<float> delta);
  void clear_forced_delta();

  /// Gameplay pause, distinct from focus loss, backgrounding and update
  /// suspension: the engine callback keeps running, but the effective delta
  /// produced at the end of the current timed frame becomes zero
  /// (base_delta keeps the unpaused value).
  void set_gameplay_paused(bool paused);

  /// Suppresses only the engine-frame callback of the timed frame
  /// (recovered skipEngineFrame / DAT_0090ef2e): input polling, pressed
  /// state, frame measurement and delta calculation still run.
  /// Debug/test control; no producer is wired yet.
  void set_skip_engine_frame(bool skip);

 private:
  Application() = default;

  /// Drains every queued SDL event through process_event.
  void poll_events();
  /// Processes one SDL event: updates the activity gates, held-input state
  /// and ImGui, and handles quit/close.
  void process_event(const SDL_Event& event);
  /// Blocks in SDL_WaitEvent and processes the event that woke the loop.
  void wait_for_event();
  /// Applies the FPS-style capture intent to SDL.
  void set_mouse_captured(bool captured);
  /// Seeds the held-input tracker from SDL's current device state.
  void seed_held_input();
  /// Input step of the timed frame, in the recovered order: snapshot the
  /// devices → update action values and the pressed bitfield → reset the
  /// per-frame input field.
  void snapshot_input();
  /// Engine-frame callback of the timed frame, provisionally the original
  /// FUN_004200f0. Runs after the input step and before the new delta is
  /// calculated, so it consumes the previous frame's effective delta
  /// (converted to seconds at this boundary — see docs/ReverseEngineering.md).
  void run_engine_frame();
  /// Integration point for the original per-frame held-key test
  /// `if (EscapeHeld && !gamePaused) DispatchGameCommand(0x1F, -1, -1);`.
  /// gamePaused maps to FrameTimingState::gameplay_paused; the game-command
  /// dispatcher itself does not exist yet.
  void dispatch_held_escape();
  /// Completes the splash and runs scenario modes 3 -> 2 -> 1, installing
  /// the WorldScene after mode 2 establishes the world context and then
  /// opening interface 29 through the AREA script. Returns false on a
  /// mandatory failure (startup stops).
  bool advance_startup_past_splash();
  /// Wires the interface dispatcher's open sink so any AREA interface-open
  /// request (opcode 0x46) is forwarded to the generic InterfaceManager.
  /// No interface is special-cased and no scene is mutated here.
  void wire_interface_dispatch();
  /// Drains deferred interface completions after the scene update: notifies
  /// the scenario engine and closes the specific completed interface by
  /// handle. The active WorldScene is never replaced.
  void drain_interface_completions();
  /// Continues the scenario scheduler on the normal frame path after startup.
  void update_scenario(float delta_seconds);

  static constexpr float kFixedTimestep{1.0F / 60.0F};
  /// Minimum interval between relative-mode re-enable attempts while
  /// capture is wanted but SDL has it disabled.
  static constexpr float kCaptureRetryInterval{1.0F};
  /// How long the splash screen stays up before the loaded scene appears.
  static constexpr float kSplashDuration{5.0F};

  std::unique_ptr<Window> m_window{nullptr};
  std::unique_ptr<Audio::AudioSystem> m_audio{nullptr};
  std::unique_ptr<ScenarioManager> m_scenario_manager{nullptr};
  /// Scenario-mode dispatcher (modes 0/1/2/3).
  std::unique_ptr<ScenarioEngine> m_scenario_engine{nullptr};
  /// Ordered startup trace recorder.
  std::unique_ptr<Startup::StartupTraceRecorder> m_trace{nullptr};
  /// Startup phase coordinator enforcing the Runtime.exe ordering.
  std::unique_ptr<Startup::StartupCoordinator> m_coordinator{nullptr};
  /// Resolves the frame's device state into action values for the scene.
  Input::InputManager m_input{};
  // Declared after the window so it is destroyed before the GL context.
  std::unique_ptr<Scene> m_scene{nullptr};
  /// Generic interface system; owns the active interface (interface 29) and
  /// its GL resources. Declared after the window so its GL resources are
  /// released before the context is destroyed.
  std::unique_ptr<Interface::InterfaceManager> m_interface_manager{nullptr};
  /// Seconds left before switching from the splash to the runtime world.
  float m_splash_seconds_left{0.0F};
  /// True once the splash has been replaced by the stable WorldScene and the
  /// startup scenario modes (3 -> 2 -> 1) have completed.
  bool m_startup_complete{false};

  bool m_running{true};
  bool m_sdl_initialized{false};
  /// FPS-style mouse capture: relative mouse mode from startup, released by
  /// F12 and re-captured by clicking the window. Kept in sync with SDL's
  /// actual state each frame (SDL silently drops relative mode when an
  /// activation attempt fails).
  bool m_mouse_captured{false};
  /// Time left before the next relative-mode re-enable retry.
  float m_capture_retry_cooldown{0.0F};
  /// Per-event relative mouse motion accumulated by poll_events since the
  /// last frame; applied to the input snapshot while capture is active.
  float m_pending_mouse_delta_x{0.0F};
  float m_pending_mouse_delta_y{0.0F};

  /// Activation and suspension gates for the idle-driven main loop.
  RuntimeActivityState m_activity{};
  /// Held-key/button state maintained from SDL events, cleared on focus loss.
  Input::HeldInputState m_held_input{};
  /// Minimal text-input capture (hook for future text prompts).
  Input::TextInputState m_text_input{};

  /// Recovered UpdateFrameTiming state: millisecond frame clocks, FPS,
  /// base/effective delta in Omikron units (1.0 = 1/30 s).
  FrameTiming::FrameTimingState m_frame_timing{};
  /// Recovered skipEngineFrame (DAT_0090ef2e): suppresses only the
  /// engine-frame callback inside the timed frame.
  bool m_skip_engine_frame{false};

  /// Fixed-timestep accumulator for physics/game logic (60 Hz).
  float m_accumulator{0.0F};
};

}  // namespace App
