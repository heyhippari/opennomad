#include "Application.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
// vcpkg installs the backend headers flat (no backends/ prefix).
#include <fmt/format.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <glad/glad.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Debug/DebugContext.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/FrameTiming.hpp"
#include "Core/Input/ControlScheme.hpp"
#include "Core/Input/HeldInputState.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/RawInputState.hpp"
#include "Core/Input/TextInputState.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Log.hpp"
#include "Core/MainLoopController.hpp"
#include "Core/RuntimeActivityState.hpp"
#include "Core/Scenario/ScenarioEngine.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/SplashScene.hpp"
#include "Core/Startup/StartupCoordinator.hpp"
#include "Core/Startup/StartupMediaPolicy.hpp"
#include "Core/Startup/StartupPhase.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"
#include "Core/Video/StartupVideoSequence.hpp"
#include "Core/Video/VideoPlayer.hpp"
#include "Core/Video/VideoScene.hpp"
#include "Core/Window.hpp"
#include "Core/WorldScene.hpp"

namespace App {

namespace {

/// Consumes a must-use expected result whose failure the coordinator already
/// logged (ordering violations cannot occur in the fixed startup sequence).
void swallow_expected(std::expected<void, std::string> result) {
  static_cast<void>(result);
}

/// Presents startup videos through a VideoScene. Rendering bypasses the
/// debug UI (no ImGui during video playback); input is resolved by the
/// application's input manager and delivered through the playback loop's
/// stop predicate (see Application::create).
class StartupVideoPresenter final : public Video::VideoPresenter {
 public:
  StartupVideoPresenter(Window* window, Video::VideoScene* scene)
      : m_window(window), m_scene(scene) {}

  void present(const Video::VideoFrame& frame) override {
    glViewport(0, 0, m_window->get_pixel_width(), m_window->get_pixel_height());
    m_scene->present_frame(frame);
    {
      APP_PROFILE_SCOPE("VideoPresentSwap");
      SDL_GL_SwapWindow(m_window->get_native_window());
    }
  }

 private:
  Window* m_window{nullptr};
  Video::VideoScene* m_scene{nullptr};
};

}  // namespace

Application::Application(Application&& other) noexcept
    : m_window(std::move(other.m_window)),
      m_audio(std::move(other.m_audio)),
      m_scenario_manager(std::move(other.m_scenario_manager)),
      m_scenario_engine(std::move(other.m_scenario_engine)),
      m_trace(std::move(other.m_trace)),
      m_coordinator(std::move(other.m_coordinator)),
      m_input(std::move(other.m_input)),
      m_scene(std::move(other.m_scene)),
      m_interface_manager(std::move(other.m_interface_manager)),
      m_splash_seconds_left(other.m_splash_seconds_left),
      m_startup_complete(other.m_startup_complete),
      m_running(other.m_running),
      m_sdl_initialized(std::exchange(other.m_sdl_initialized, false)),
      m_mouse_captured(other.m_mouse_captured),
      m_capture_retry_cooldown(other.m_capture_retry_cooldown),
      m_capture_diag_cooldown(other.m_capture_diag_cooldown),
      m_pending_mouse_delta_x(other.m_pending_mouse_delta_x),
      m_pending_mouse_delta_y(other.m_pending_mouse_delta_y),
      m_pending_mouse_motion_events(other.m_pending_mouse_motion_events),
      m_activity(other.m_activity),
      m_held_input(other.m_held_input),
      m_text_input(other.m_text_input),
      m_frame_timing(other.m_frame_timing),
      m_skip_engine_frame(other.m_skip_engine_frame),
      m_accumulator(other.m_accumulator) {}

std::expected<Application, std::string> Application::create(const std::string& title) {
  APP_PROFILE_FUNCTION();

  auto trace{std::make_unique<Startup::StartupTraceRecorder>()};
  auto coordinator{std::make_unique<Startup::StartupCoordinator>(*trace)};

  // --- Phase 1: process bootstrap ---
  swallow_expected(coordinator->begin(Startup::StartupPhase::k_process_bootstrap));
  const unsigned int init_flags{SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO};
  if (!SDL_Init(init_flags)) {
    return std::expected<Application, std::string>{std::unexpect,
        fmt::format("Can't initialize Omikron: SDL_Init failed: {}", SDL_GetError())};
  }
  App::Log::debug("SDL video driver: {}", SDL_GetCurrentVideoDriver());

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

  SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");

  // Game-oriented hints.
  SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
  swallow_expected(coordinator->complete(
      Startup::StartupPhase::k_process_bootstrap, Startup::StartupPhaseStatus::k_complete));

  // --- Phase 2: create the application/render window ---
  swallow_expected(coordinator->begin(Startup::StartupPhase::k_create_windows));
  auto window{Window::create(Window::Settings{.title = title})};
  if (!window) {
    SDL_Quit();
    return std::expected<Application, std::string>{std::unexpect,
        fmt::format("Can't initialize Omikron: {}", std::move(window).error())};
  }
  trace->record("Window.Created");
  swallow_expected(coordinator->complete(
      Startup::StartupPhase::k_create_windows, Startup::StartupPhaseStatus::k_complete));

  // --- Phase 3: initialize core engine systems ---
  swallow_expected(coordinator->begin(Startup::StartupPhase::k_initialize_core_systems));
  auto audio{Audio::AudioSystem::create()};
  if (audio) {
    trace->record("Audio.Initialized");
  } else {
    App::Log::warn("Audio disabled: {}", audio.error());
  }

  auto manager{std::make_unique<ScenarioManager>()};
  auto engine{std::make_unique<ScenarioEngine>(*manager, *trace)};
  trace->record("Input.Initialized");
  trace->record("InterfaceRegistry.Initialized");
  trace->record("ScriptRuntime.Initialized");
  if (auto result{engine->enter_mode(ScenarioMode::k_initial, 0)}; !result) {
    engine.reset();
    manager.reset();
    if (audio.has_value()) {
      audio.value().reset();
    }
    window.value().reset();
    SDL_Quit();
    return std::expected<Application, std::string>{std::unexpect,
        fmt::format("Can't initialize Omikron: {}", result.error())};
  }
  swallow_expected(coordinator->complete(
      Startup::StartupPhase::k_initialize_core_systems, Startup::StartupPhaseStatus::k_complete));

  // Build the application object before the startup videos so their playback
  // loop can pump events through the normal application path and resolve the
  // skip action through the input manager. Moving the owners here also means
  // every failure path below lets ~Application release them.
  Application app;
  app.m_window = std::move(window).value();
  app.m_trace = std::move(trace);
  app.m_coordinator = std::move(coordinator);
  app.m_scenario_manager = std::move(manager);
  app.m_scenario_engine = std::move(engine);
  if (audio) {
    app.m_audio = std::move(audio).value();
    app.m_scenario_manager->set_audio_system(app.m_audio.get());
    app.m_scenario_engine->set_audio_system(app.m_audio.get());
  }
  app.m_interface_manager = std::make_unique<Interface::InterfaceManager>();
  // The skip action must be registered before the videos: the playback loop
  // asks the input manager whether Escape was pressed this frame.
  app.m_input.add_scheme(Input::ControlScheme::make_keyboard_mouse_default());

  // --- Phases 4-6: startup videos (sequential; optional presentation) ---
  // The video system is scoped so it is fully closed before aventure.SCX.
  {
    auto video_scene{Video::VideoScene::create()};
    Video::StartupVideoSequence video_sequence{*app.m_trace, Startup::StartupMediaPolicy{}};
    StartupVideoPresenter presenter{app.m_window.get(), video_scene.get()};
    // Pump events and update the input manager every video-frame tick; only
    // the Escape skip action (or a processed quit request) stops playback.
    const auto should_stop = [&app]() {
      app.poll_events();
      app.snapshot_input();
      return !app.m_running || app.m_input.is_action_pressed(Input::Action::k_skip_video);
    };
    const auto play_phase = [&](const Startup::StartupPhase phase,
                                const Startup::StartupVideoSlot slot) {
      swallow_expected(app.m_coordinator->begin(phase));
      const Startup::StartupPhaseStatus status{
          video_sequence.play_slot(slot, presenter, should_stop)};
      swallow_expected(app.m_coordinator->complete(phase, status));
    };
    play_phase(Startup::StartupPhase::k_play_publisher_video,
        Startup::StartupVideoSlot::k_publisher);
    play_phase(Startup::StartupPhase::k_play_developer_video,
        Startup::StartupVideoSlot::k_developer);
    play_phase(Startup::StartupPhase::k_play_intro_video, Startup::StartupVideoSlot::k_intro);
  }

  // A quit request processed while the videos were playing means the caller
  // must not proceed to aventure.SCX / the splash. Return a valid but
  // already-stopped application; run() honours m_running and returns.
  if (!app.m_running) {
    app.m_sdl_initialized = true;
    return app;
  }

  // --- Phase 7: select the permanent mode script (aventure.SCX) ---
  swallow_expected(app.m_coordinator->begin(Startup::StartupPhase::k_select_permanent_mode_script));
  if (auto result{app.m_scenario_engine->select_permanent_mode_script()}; !result) {
    app.m_scenario_engine.reset();
    app.m_scenario_manager.reset();
    app.m_audio.reset();
    app.m_window.reset();
    SDL_Quit();
    return std::expected<Application, std::string>{std::unexpect,
        fmt::format("Can't initialize Omikron: {}", result.error())};
  }
  swallow_expected(app.m_coordinator->complete(
      Startup::StartupPhase::k_select_permanent_mode_script,
      Startup::StartupPhaseStatus::k_complete));

  // --- Phase 8: prepare the splash ---
  swallow_expected(app.m_coordinator->begin(Startup::StartupPhase::k_prepare_splash));
  auto splash{SplashScene::create()};
  if (!splash) {
    app.m_scenario_engine.reset();
    app.m_scenario_manager.reset();
    app.m_audio.reset();
    app.m_window.reset();
    SDL_Quit();
    return std::expected<Application, std::string>{std::unexpect,
        fmt::format("Can't initialize Omikron: {}", std::move(splash).error())};
  }
  app.m_scenario_engine->open_preliminary_29();
  app.m_trace->record("Splash.Omikron.Prepared");
  swallow_expected(app.m_coordinator->complete(
      Startup::StartupPhase::k_prepare_splash, Startup::StartupPhaseStatus::k_complete));

  app.m_scene = std::move(splash).value();
  app.m_window->debug_ui().set_context(Debug::DebugContext{
      .scene = app.m_scene.get(),
      .scenario_manager = app.m_scenario_manager.get(),
      .scenario_engine = app.m_scenario_engine.get(),
      .interface_manager = app.m_interface_manager.get(),
      .audio_system = app.m_audio.get(),
      .startup_coordinator = app.m_coordinator.get(),
      .startup_trace = app.m_trace.get()});
  app.m_splash_seconds_left = kSplashDuration;

  // Initialise the activation gates optimistically: a freshly created,
  // shown window is active. SDL's SDL_WINDOW_INPUT_FOCUS flag is unreliable
  // here on Wayland, where keyboard focus is only granted after the
  // surface's first present — gating the very first frame on that flag
  // would deadlock (no frame -> no present -> no focus event -> no frame).
  // The first poll_events() applies any real transitions before the gates
  // are evaluated, so the optimistic value never outlives real events.
  app.m_activity.on_render_window_active(true);
  // Desktop SDL has no background concept; the application starts in the
  // foreground. The mobile-only SDL background/foreground events keep this
  // gate current on those platforms (docs/ReverseEngineering.md).
  app.m_activity.on_application_active(true);

  // Capture the mouse FPS-style. F12 releases it and clicking the window
  // re-captures it (poll_events). The default control scheme, including the
  // Escape skip action, was registered before the startup videos.
  app.set_mouse_captured(true);

  app.m_sdl_initialized = true;
  return app;
}

Application::~Application() {
  APP_PROFILE_FUNCTION();

  // Destroy GL resources while the window's context is still alive.
  m_scene.reset();
  m_audio.reset();
  m_window.reset();
  if (m_sdl_initialized) {
    SDL_Quit();
  }
}

void App::Application::run() {
  APP_PROFILE_FUNCTION();

  // A quit request processed while the startup videos were playing (in
  // create()) must be honoured before the loop begins; otherwise the loop
  // would resurrect a stopped application.
  if (!m_running) {
    return;
  }

  // Seed the held-input tracker from SDL's current device state so keys or
  // buttons already held before the first event are not missed.
  seed_held_input();

  // Wire the interface dispatch sink before the splash can complete: the area
  // script's interface-29 open (opcode 0x46) opens the interface through the
  // generic InterfaceManager; the already-installed WorldScene presents it.
  wire_interface_dispatch();

  // The recovered frame clock re-baselines on the first frame: start with
  // the timing-reset request set so no pre-loop time is measured.
  m_activity.reset_frame_timing_on_next_update = true;

  // Phase: run the five-second splash. The main loop has now begun.
  if (m_coordinator != nullptr) {
    swallow_expected(m_coordinator->begin(Startup::StartupPhase::k_run_splash));
  }
  if (m_trace != nullptr) {
    m_trace->record("MainLoop.Started");
    m_trace->record("Splash.Omikron.Active");
  }

  while (m_running) {
    APP_PROFILE_SCOPE("MainLoop");

    // Drain every queued event before evaluating the gates: an event that
    // just deactivated or suspended the application must prevent the frame.
    poll_events();

    if (!m_running) {
      break;
    }

    // Relative mouse mode is only meaningful while frames may execute.
    // Release it whenever the application is inactive or suspended so the
    // cursor stays usable while the loop waits.
    const bool window_focused{m_window->has_keyboard_focus()};
    if (m_window->is_relative_mouse_mode() && !MainLoopController::should_enable_relative_mouse(
                                                  m_mouse_captured, window_focused, m_activity)) {
      m_window->set_relative_mouse_mode(false);
      m_pending_mouse_delta_x = 0.0F;
      m_pending_mouse_delta_y = 0.0F;
    }

    // Idle-driven loop, mirroring the original message loop: run a frame
    // only when the queue is drained and every gate passes; otherwise mark
    // timing for reset and block in the event wait instead of busy-spinning.
    const MainLoopDecision decision{MainLoopController::advance(m_running, m_activity)};
    if (decision.action != MainLoopAction::k_run_frame) {
      if (decision.action == MainLoopAction::k_exit) {
        break;
      }
      wait_for_event();
      continue;
    }

    // Recovered UpdateFrameTiming order (Core/FrameTiming.hpp): input is
    // prepared first, the engine callback runs next — consuming the
    // effective delta produced by the previous frame — and only then is
    // the completed frame measured and the next callback's delta
    // calculated. The clock is the monotonic millisecond SDL_GetTicks();
    // the reset request re-baselines it immediately before input and the
    // callback so inactive waiting time is never measured as frame time.
    FrameTiming::run_timed_frame(
        m_frame_timing,
        decision.reset_frame_timing,
        m_skip_engine_frame,
        []() -> std::uint64_t { return SDL_GetTicks(); },
        [this] { snapshot_input(); },
        [this] { run_engine_frame(); });

    // A frame executed: the timing-reset request has been served. Only now
    // may the flag be cleared — event processing alone must never clear it.
    m_activity.clear_reset_flag_after_frame();
  }
}

void Application::snapshot_input() {
  APP_PROFILE_FUNCTION();

  // Recovered input-step order: snapshot the devices → update action values
  // and the pressed bitfield → reset the per-frame input field. Runs before
  // the engine callback and still runs when the callback is skipped.
  Input::RawInputState raw_state{};
  m_held_input.fill(raw_state);
  const bool window_focused{m_window->has_keyboard_focus()};
  // Apply the per-event relative motion collected by poll_events only
  // while capture is active. SDL filters absolute cursor motion out of
  // the event stream in relative mode, whereas SDL_GetRelativeMouseState
  // would also return accumulated absolute motion, which stops at the
  // window edge when the pointer is not truly locked.
  if (m_mouse_captured && window_focused && m_window->is_relative_mouse_mode()) {
    raw_state.mouse_delta_x = m_pending_mouse_delta_x;
    raw_state.mouse_delta_y = m_pending_mouse_delta_y;
  }

  // ImGui-captured devices are zeroed so interacting with the debug UI
  // never drives gameplay.
  const ImGuiIO& io{ImGui::GetIO()};
  if (io.WantCaptureKeyboard) {
    std::ranges::fill(raw_state.key_down, false);
  }
  if (io.WantCaptureMouse) {
    raw_state.mouse_delta_x = 0.0F;
    raw_state.mouse_delta_y = 0.0F;
    std::ranges::fill(raw_state.mouse_button_down, false);
  }
  // update() resolves action values and recomputes the pressed bitfield
  // (recovered g_pressedInput on the mapped action bits).
  m_input.update(raw_state);
  // Recovered `DAT_0090e0e0 = 0`, performed before the engine callback.
  m_input.reset_per_frame_input();
}

void Application::run_engine_frame() {
  APP_PROFILE_FUNCTION();

  // Recovered boundary (docs/ReverseEngineering.md): the engine callback
  // consumes the effective delta produced by the PREVIOUS timed frame.
  // Omikron delta units (1.0 = 1/30 s) are converted to seconds exactly
  // once, here; every consumer below speaks seconds.
  const float delta_seconds{m_frame_timing.effective_delta / FrameTiming::k_delta_units_per_second};

  // Feed the metrics system.
  Debug::Metrics::get().on_frame_begin(delta_seconds);

  const bool window_focused{m_window->has_keyboard_focus()};

  // --- Mouse capture reconciliation ---
  // Keep the capture intent and SDL's actual relative-mouse state in sync.
  // SDL can drop relative mode when an activation attempt fails (e.g. a
  // grab conflict during a focus transition), so re-request it while
  // captured and focused, throttled to avoid hammering the backend when
  // the platform keeps refusing.
  if (m_mouse_captured) {
    if (MainLoopController::should_enable_relative_mouse(
            m_mouse_captured, window_focused, m_activity) &&
        !m_window->is_relative_mouse_mode() && m_capture_retry_cooldown <= 0.0F) {
      m_window->set_relative_mouse_mode(true);
      m_capture_retry_cooldown = kCaptureRetryInterval;
    }
  } else if (m_window->is_relative_mouse_mode()) {
    m_window->set_relative_mouse_mode(false);
  }
  m_capture_retry_cooldown = std::max(0.0F, m_capture_retry_cooldown - delta_seconds);

  // Pin the cursor to the window centre every frame while captured. When
  // relative mode is genuinely active this is a no-op inside SDL (warping
  // in relative mode only adjusts the internal position); when the
  // compositor's pointer lock is missing or was dropped, it keeps the real
  // cursor inside the surface so look input can never stop at a window
  // edge (alt-tab recovery on Wayland).
  if (m_mouse_captured && window_focused && m_window->is_relative_mouse_mode()) {
    SDL_WarpMouseInWindow(m_window->get_native_window(),
        static_cast<float>(m_window->get_width()) / 2.0F,
        static_cast<float>(m_window->get_height()) / 2.0F);
  }

  // TEMPORARY diagnostic: snapshot the capture state once a second so we
  // can see what SDL reports after an alt-tab focus cycle.
  m_capture_diag_cooldown -= delta_seconds;
  if (m_mouse_captured && m_capture_diag_cooldown <= 0.0F) {
    float cursor_x{0.0F};
    float cursor_y{0.0F};
    SDL_GetMouseState(&cursor_x, &cursor_y);
    App::Log::debug(
        "Mouse capture diag: focused={}, flag={}, cursor=({}, {}), events={}, deltas=({}, {})",
        window_focused,
        m_window->is_relative_mouse_mode(),
        cursor_x,
        cursor_y,
        m_pending_mouse_motion_events,
        m_pending_mouse_delta_x,
        m_pending_mouse_delta_y);
    m_capture_diag_cooldown = 1.0F;
  }

  // --- Held-Escape command dispatch (original per-frame held-key test) ---
  //   if (EscapeHeld && !gamePaused) DispatchGameCommand(0x1F, -1, -1);
  // Command 0x1F is not represented yet, so dispatch_held_escape is a
  // documented no-op integration point (docs/ReverseEngineering.md).
  if (MainLoopController::should_dispatch_escape(
          m_held_input.escape_held(), m_frame_timing.gameplay_paused)) {
    dispatch_held_escape();
  }

  // --- Fixed timestep accumulator (60 Hz) ---
  m_accumulator += delta_seconds;
  while (m_accumulator >= kFixedTimestep) {
    // fixed_update(kFixedTimestep) — physics / game logic goes here.
    m_accumulator -= kFixedTimestep;
  }

  // --- Splash countdown -> scenario modes 3 -> 2 -> 1 -> interface 29 ---
  if (!m_startup_complete) {
    m_splash_seconds_left -= delta_seconds;
    if (m_splash_seconds_left <= 0.0F) {
      m_startup_complete = advance_startup_past_splash();
    }
  }

  if (m_scene != nullptr) {
    m_scene->update(delta_seconds, m_input);
    // Scenes receive the drawable pixel size: the aspect ratio matches the
    // logical one and the viewport is correct on high-DPI displays.
    m_scene->resize(m_window->get_pixel_width(), m_window->get_pixel_height());
  }

  // Deferred interface completions are drained after the scene/interface
  // update so a completion queued by the New Game child-state action is
  // never delivered while the selected element is still being iterated.
  drain_interface_completions();

  // Continue the scenario scheduler on the normal frame path. The initial
  // mode-1 execution already happened during startup; this keeps a waiting
  // or resumed AREA script progressing without re-entering a scenario mode.
  update_scenario(delta_seconds);

  // Update the audio subsystem once per executed frame using real seconds
  // (never Omikron 30 Hz delta units). SDL3_mixer mixes asynchronously, but
  // the main-thread update drains events and rebuilds diagnostics.
  if (m_audio != nullptr) {
    m_audio->update(delta_seconds);
  }

  m_window->begin_frame(delta_seconds);

  if (m_scene != nullptr) {
    m_scene->render();
  }

  m_window->end_frame();
}

void App::Application::stop() {
  APP_PROFILE_FUNCTION();

  m_running = false;
}

void Application::set_mouse_captured(const bool captured) {
  APP_PROFILE_FUNCTION();

  m_mouse_captured = captured;
  m_window->set_relative_mouse_mode(captured);
}

void Application::set_updates_suspended(const bool suspended) {
  APP_PROFILE_FUNCTION();

  m_activity.set_updates_suspended(suspended);
}

void Application::set_text_capture_enabled(const bool enabled) {
  APP_PROFILE_FUNCTION();

  m_text_input.set_enabled(enabled);
}

void Application::set_time_scale_mode(const FrameTiming::TimeScaleMode mode) {
  APP_PROFILE_FUNCTION();

  m_frame_timing.time_scale_mode = mode;
}

void Application::set_forced_delta(const std::optional<float> delta) {
  APP_PROFILE_FUNCTION();

  m_frame_timing.forced_delta = delta;
}

void Application::clear_forced_delta() {
  APP_PROFILE_FUNCTION();

  m_frame_timing.forced_delta.reset();
}

void Application::set_gameplay_paused(const bool paused) {
  APP_PROFILE_FUNCTION();

  m_frame_timing.gameplay_paused = paused;
}

void Application::set_skip_engine_frame(const bool skip) {
  APP_PROFILE_FUNCTION();

  m_skip_engine_frame = skip;
}

void Application::dispatch_held_escape() {
  // Original behavior: DispatchGameCommand(0x1F, -1, -1). The game-command
  // dispatcher does not exist yet, so there is nothing to dispatch
  // (docs/ReverseEngineering.md).
}

void Application::wire_interface_dispatch() {
  if (m_scenario_engine == nullptr || m_interface_manager == nullptr) {
    return;
  }
  // The area script's opcode 0x46 requests an interface; the generic
  // interface system opens it. No interface ID is special-cased and no scene
  // is mutated here — the already-installed WorldScene presents whatever the
  // InterfaceManager reports.
  m_scenario_engine->dispatcher().set_interface_open_sink(
      [this](const InterfaceOpenRequest& request)
          -> std::expected<InterfaceHandle, std::string> {
        return m_interface_manager->open(request);
      });
}

void Application::drain_interface_completions() {
  APP_PROFILE_FUNCTION();

  if (m_interface_manager == nullptr) {
    return;
  }
  while (std::optional<InterfaceCompletion> completion{m_interface_manager->take_completion()}) {
    if (m_scenario_engine != nullptr) {
      m_scenario_engine->notify_interface_completion(completion.value());
    }
    // Close only the specific completed instance; other resident interfaces
    // stay alive. The WorldScene remains installed regardless.
    m_interface_manager->close(completion->handle);
  }
}

void Application::update_scenario(const float delta_seconds) {
  APP_PROFILE_FUNCTION();

  if (m_scenario_engine == nullptr) {
    return;
  }
  if (auto result{m_scenario_engine->update(delta_seconds)}; !result) {
    App::Log::error("Scenario update failed: {}", result.error());
  }
}

bool Application::advance_startup_past_splash() {
  APP_PROFILE_FUNCTION();

  if (m_trace == nullptr || m_coordinator == nullptr || m_scenario_engine == nullptr) {
    return false;
  }

  m_trace->record("Splash.Omikron.Complete");

  if (auto result{m_coordinator->complete(
          Startup::StartupPhase::k_run_splash, Startup::StartupPhaseStatus::k_complete)};
      !result) {
    App::Log::error("Startup ordering error: {}", result.error());
    m_running = false;
    return false;
  }

  const auto run_phase = [this](const Startup::StartupPhase phase,
                               const ScenarioMode mode) -> bool {
    if (auto result{m_coordinator->begin(phase)}; !result) {
      App::Log::error("Startup ordering error: {}", result.error());
      m_running = false;
      return false;
    }
    if (auto result{m_scenario_engine->enter_mode(mode, 0)}; !result) {
      App::Log::error("Can't initialize Omikron: {}", result.error());
      swallow_expected(m_coordinator->complete(
          phase, Startup::StartupPhaseStatus::k_failed, result.error()));
      m_running = false;
      return false;
    }
    if (auto result{m_coordinator->complete(phase, Startup::StartupPhaseStatus::k_complete)};
        !result) {
      App::Log::error("Startup ordering error: {}", result.error());
      m_running = false;
      return false;
    }
    return true;
  };

  if (!run_phase(Startup::StartupPhase::k_reset_preliminary_scenario, ScenarioMode::k_teardown)) {
    return false;
  }
  if (!run_phase(Startup::StartupPhase::k_initialize_new_session, ScenarioMode::k_new_session)) {
    return false;
  }

  // After mode 2 has successfully established the world context, install the
  // stable WorldScene. Mode 1 then executes, the AREA script opens interface
  // 29, and the WorldScene's InterfacePresenter presents it. No frame is
  // rendered in the middle of this synchronous startup sequence, so GRID is
  // never exposed before the main menu.
  {
    auto world{WorldScene::create(*m_scenario_manager, *m_interface_manager)};
    if (!world) {
      App::Log::error("Can't initialize Omikron: {}", std::move(world).error());
      m_running = false;
      return false;
    }
    m_scene = std::move(world).value();
    m_window->debug_ui().set_scene(m_scene.get());
  }

  if (!run_phase(Startup::StartupPhase::k_run_initial_area_script, ScenarioMode::k_tick)) {
    return false;
  }

  // The area script's opcode 0x46 already opened interface 29 during mode 1;
  // the InterfaceManager owns it and the WorldScene presents it. There is no
  // direct menu creation here and no scene swap.
  if (auto result{m_coordinator->begin(Startup::StartupPhase::k_open_main_menu)}; !result) {
    App::Log::error("Startup ordering error: {}", result.error());
    m_running = false;
    return false;
  }
  if (!m_scenario_engine->main_menu_active()) {
    App::Log::error(
        "Can't initialize Omikron: the area script did not open interface 29 (no main menu)");
    swallow_expected(m_coordinator->complete(
        Startup::StartupPhase::k_open_main_menu, Startup::StartupPhaseStatus::k_failed,
        "area script did not open interface 29"));
    m_running = false;
    return false;
  }
  if (auto result{m_coordinator->complete(
          Startup::StartupPhase::k_open_main_menu, Startup::StartupPhaseStatus::k_complete)};
      !result) {
    App::Log::error("Startup ordering error: {}", result.error());
    m_running = false;
    return false;
  }
  if (auto result{m_coordinator->finish()}; !result) {
    App::Log::error("Startup ordering error: {}", result.error());
    m_running = false;
    return false;
  }

  m_input.reset();
  return true;
}

void Application::seed_held_input() {
  APP_PROFILE_FUNCTION();

  int num_keys{0};
  const bool* key_states{SDL_GetKeyboardState(&num_keys)};
  if (key_states != nullptr) {
    const int key_count{std::min(num_keys, static_cast<int>(SDL_SCANCODE_COUNT))};
    for (int index{0}; index < key_count; ++index) {
      if (key_states[index]) {
        // SDL hands us a raw C array; indexing it is the intended API.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        m_held_input.on_key_down(static_cast<SDL_Scancode>(index));
      }
    }
  }
  const SDL_MouseButtonFlags button_flags{SDL_GetMouseState(nullptr, nullptr)};
  for (std::size_t button{1}; button < static_cast<std::size_t>(Input::k_mouse_button_count);
      ++button) {
    if ((button_flags & SDL_BUTTON_MASK(static_cast<int>(button))) != 0U) {
      m_held_input.on_mouse_button_down(static_cast<std::uint32_t>(button));
    }
  }
}

void Application::poll_events() {
  APP_PROFILE_FUNCTION();

  m_pending_mouse_delta_x = 0.0F;
  m_pending_mouse_delta_y = 0.0F;
  m_pending_mouse_motion_events = 0;

  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    APP_PROFILE_SCOPE("EventPolling");
    process_event(event);
  }
}

void Application::wait_for_event() {
  APP_PROFILE_FUNCTION();

  SDL_Event event{};
  if (!SDL_WaitEvent(&event)) {
    App::Log::warn("SDL_WaitEvent failed: {}", SDL_GetError());
    // Avoid busy-spinning if the wait facility keeps failing.
    SDL_Delay(100);
    return;
  }
  process_event(event);
}

void Application::process_event(const SDL_Event& event) {
  APP_PROFILE_FUNCTION();

  const SDL_WindowID own_window{SDL_GetWindowID(m_window->get_native_window())};

  // Accumulate per-event relative motion. While the window is in relative
  // mouse mode, SDL posts only genuine relative deltas here (absolute
  // cursor motion is filtered inside SDL), unlike SDL_GetRelativeMouseState,
  // which mixes both sources and stalls at the window edge when the
  // compositor's pointer lock is not active.
  if (m_mouse_captured && event.type == SDL_EVENT_MOUSE_MOTION &&
      event.motion.windowID == own_window) {
    m_pending_mouse_delta_x += event.motion.xrel;
    m_pending_mouse_delta_y += event.motion.yrel;
    ++m_pending_mouse_motion_events;
  }

  ImGui_ImplSDL3_ProcessEvent(&event);

  // Application-level termination.
  if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_TERMINATING) {
    stop();
    return;
  }

  // Foreground/background transitions. Desktop SDL never delivers these;
  // the window focus events below cover desktop activation, while these
  // keep the applicationActive gate correct on mobile platforms.
  if (event.type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
      event.type == SDL_EVENT_DID_ENTER_BACKGROUND) {
    m_activity.on_application_active(false);
  } else if (event.type == SDL_EVENT_WILL_ENTER_FOREGROUND ||
             event.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
    m_activity.on_application_active(true);
  }

  if (event.window.windowID == own_window) {
    if (event.window.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
      stop();
      return;
    }

    m_window->on_event(event.window);

    switch (event.window.type) {
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        // Collapsed mapping of WM_ACTIVATE WA_ACTIVE / WA_CLICKACTIVE /
        // WM_SETFOCUS / WM_CHILDACTIVATE: both gates open again.
        m_activity.on_render_window_active(!m_window->is_minimized());
        m_activity.on_application_active(true);
        // SDL re-activates relative mode after dispatching FOCUS_GAINED and a
        // failed (re)activation is otherwise silent: force a full off->on
        // cycle so the flag and the actual mode converge and failures are
        // logged. This runs inside the same poll loop, so no frame is
        // rendered with the cursor freed in between.
        if (m_mouse_captured) {
          App::Log::debug(
              "Mouse capture: focus gained (flag: {})", m_window->is_relative_mouse_mode());
          m_window->set_relative_mouse_mode(false);
          m_window->set_relative_mouse_mode(true);
        }
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        // Collapsed mapping of WM_ACTIVATE WA_INACTIVE: both gates close.
        m_activity.on_render_window_active(false);
        m_activity.on_application_active(false);
        // Reconcile held input: SDL may never deliver key-up/button-up
        // events for releases that happen while unfocused, so drop all held
        // state (the original clears on WM_KILLFOCUS).
        m_held_input.clear();
        m_pending_mouse_delta_x = 0.0F;
        m_pending_mouse_delta_y = 0.0F;
        App::Log::debug("Mouse capture: focus lost (flag: {})", m_window->is_relative_mouse_mode());
        break;
      case SDL_EVENT_WINDOW_MINIMIZED:
        m_activity.on_render_window_active(false);
        m_held_input.clear();
        break;
      case SDL_EVENT_WINDOW_SHOWN:
      case SDL_EVENT_WINDOW_RESTORED:
        // Visibility restored. Opens the gate only when SDL already reports
        // keyboard focus; otherwise the upcoming FOCUS_GAINED does it.
        // Never closes the gate here — see
        // RuntimeActivityState::on_render_window_restored.
        m_activity.on_render_window_restored(m_window->has_keyboard_focus());
        break;
      case SDL_EVENT_WINDOW_MOUSE_ENTER:
        // When the pointer re-enters the window, re-issue the relative-mode
        // activation so SDL re-requests the compositor pointer lock while the
        // pointer is known to be inside the surface. This heals the case
        // where the lock request made during focus regain was not honoured
        // (alt-tab recovery on Wayland).
        if (m_mouse_captured) {
          App::Log::debug(
              "Mouse capture: mouse entered (flag: {})", m_window->is_relative_mouse_mode());
          m_window->set_relative_mouse_mode(false);
          m_window->set_relative_mouse_mode(true);
        }
        break;
      default:
        break;
    }
  }

  if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
    if (event.key.windowID == own_window) {
      if (event.type == SDL_EVENT_KEY_DOWN) {
        m_held_input.on_key_down(event.key.scancode);
      } else {
        m_held_input.on_key_up(event.key.scancode);
      }
      // F12 releases the captured mouse so the debug UI can be used.
      if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F12 &&
          m_mouse_captured) {
        set_mouse_captured(false);
      }
      m_window->on_keyboard_event(event.key);
    }
  }

  if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
      event.button.windowID == own_window &&
      event.button.button < static_cast<std::uint8_t>(Input::k_mouse_button_count)) {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      m_held_input.on_mouse_button_down(event.button.button);
    } else {
      m_held_input.on_mouse_button_up(event.button.button);
    }
  }

  // Text input only feeds the capture state while it is enabled.
  if (event.type == SDL_EVENT_TEXT_INPUT && event.text.windowID == own_window) {
    m_text_input.on_text_input(event.text.text);
  }

  // Clicking the window outside ImGui re-captures the mouse.
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_mouse_captured &&
      event.button.button == SDL_BUTTON_LEFT && !ImGui::GetIO().WantCaptureMouse) {
    set_mouse_captured(true);
  }
}

}  // namespace App
