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
#include <fmt/format.h>
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/DebugContext.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Debug/RuntimeTimingDebug.hpp"
#include "Core/DisplayConfiguration.hpp"
#include "Core/FrameTiming.hpp"
#include "Core/Input/ControlScheme.hpp"
#include "Core/Input/HeldInputState.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/RawInputState.hpp"
#include "Core/Input/TextInputState.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/MainLoopController.hpp"
#include "Core/Resources.hpp"
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
#include "Settings/GameSettings.hpp"

namespace App {

namespace {

/// Consumes a must-use expected result whose failure the coordinator already
/// logged (ordering violations cannot occur in the fixed startup sequence).
void swallow_expected(std::expected<void, std::string> result) {
  static_cast<void>(result);
}

void apply_audio_setting(const Settings::GameSettings& settings,
    Audio::AudioSystem& audio,
    const std::string_view stable_id) {
  if (stable_id == "audio.dialogue_volume") {
    audio.set_dialogue_gain(
        Audio::normalized_settings_gain(settings.number_value(stable_id).value_or(100)));
  } else if (stable_id == "audio.music_volume") {
    audio.set_music_gain(
        Audio::normalized_settings_gain(settings.number_value(stable_id).value_or(100)));
  } else if (stable_id == "audio.sfx_volume") {
    audio.set_sfx_gain(
        Audio::normalized_settings_gain(settings.number_value(stable_id).value_or(100)));
  } else if (stable_id == "audio.ambient_volume") {
    audio.set_ambience_gain(
        Audio::normalized_settings_gain(settings.number_value(stable_id).value_or(100)));
  } else if (stable_id == "audio.spatial_audio") {
    audio.set_spatial_audio_enabled(settings.choice_raw_value(stable_id).value_or(1) != 0);
  }
}

void apply_display_setting(
    Settings::GameSettings& settings, Window& window, const std::string_view stable_id) {
  if (stable_id != "display.mode" && stable_id != "display.resolution") {
    return;
  }
  DisplayMode mode{static_cast<DisplayMode>(settings.choice_raw_value("display.mode").value_or(1))};
  DisplayResolution resolution{
      unpack_display_resolution(settings.choice_raw_value("display.resolution").value_or(0))
          .value_or(DisplayResolution{.width = 800, .height = 600})};
  const DisplayMode previous_mode{window.reconcile_display_state()};
  const DisplayResolution previous_resolution{window.actual_resolution()};
  const DisplayModeCatalog catalog{window.display_mode_catalog()};
  if (mode == DisplayMode::k_exclusive_fullscreen) {
    const auto selected{select_exclusive_resolution(catalog, resolution)};
    if (!selected.has_value()) {
      App::Log::warn(LogCategory::Renderer,
          "no exclusive display modes are available; falling back to Borderless Fullscreen");
      mode = DisplayMode::k_borderless_fullscreen;
    } else if (selected.value() != resolution) {
      App::Log::warn(LogCategory::Renderer,
          "saved exclusive resolution {} is unavailable; falling back to {}",
          display_resolution_label(resolution),
          display_resolution_label(selected.value()));
      resolution = selected.value();
      settings.set_choice_raw_value(
          "display.resolution", pack_display_resolution(resolution).value_or(0), false);
    }
  }
  if (window.apply_display_configuration(mode, resolution)) {
    settings.set_choice_raw_value("display.mode", static_cast<std::int32_t>(mode), false);
    if (mode != DisplayMode::k_borderless_fullscreen) {
      const DisplayResolution actual{window.actual_resolution()};
      settings.set_choice_raw_value(
          "display.resolution", pack_display_resolution(actual).value_or(0), false);
    }
    if (mode != DisplayMode::k_windowed) {
      settings.set_choice_raw_value(
          "display.fullscreen_preference", static_cast<std::int32_t>(mode), false);
    }
    return;
  }
  if (mode == DisplayMode::k_exclusive_fullscreen &&
      window.apply_display_configuration(DisplayMode::k_borderless_fullscreen, resolution)) {
    App::Log::warn(
        LogCategory::Renderer, "exclusive fullscreen failed; using Borderless Fullscreen");
    mode = DisplayMode::k_borderless_fullscreen;
  } else if (window.apply_display_configuration(DisplayMode::k_windowed, previous_resolution)) {
    App::Log::warn(LogCategory::Renderer, "fullscreen change failed; using Windowed mode");
    mode = DisplayMode::k_windowed;
  } else {
    static_cast<void>(window.apply_display_configuration(previous_mode, previous_resolution));
    mode = window.reconcile_display_state();
    App::Log::warn(LogCategory::Renderer, "display change failed; restored the previous mode");
  }
  settings.set_choice_raw_value("display.mode", static_cast<std::int32_t>(mode), false);
  if (mode != DisplayMode::k_windowed) {
    settings.set_choice_raw_value(
        "display.fullscreen_preference", static_cast<std::int32_t>(mode), false);
  }
  if (mode != DisplayMode::k_borderless_fullscreen) {
    const DisplayResolution actual{window.actual_resolution()};
    settings.set_choice_raw_value(
        "display.resolution", pack_display_resolution(actual).value_or(0), false);
  }
}

/// Presents startup videos through a VideoScene. The debug UI is composited
/// over the video only while the mouse is released (F12), so the debug menu
/// stays usable before the normal frame loop begins; input is resolved by
/// the application's input manager through the playback loop's stop
/// predicate (see Application::create).
class StartupVideoPresenter final : public Video::VideoPresenter {
 public:
  StartupVideoPresenter(Window* window, Video::VideoScene* scene)
      : m_window(window),
        m_scene(scene),
        m_last_ticks{SDL_GetTicks()} {}

  void present(const Video::VideoFrame& frame) override {
    const int pixel_width{m_window->get_pixel_width()};
    const int pixel_height{m_window->get_pixel_height()};
    glViewport(0, 0, pixel_width, pixel_height);
    m_scene->present_frame(frame, pixel_width, pixel_height);

    // F12 releases the mouse during video playback. Once released, draw the
    // debug UI over the video so the menu bar (and any open debug windows)
    // remain interactive before the normal frame loop starts.
    if (!SDL_GetWindowRelativeMouseMode(m_window->get_native_window())) {
      const std::uint64_t now{SDL_GetTicks()};
      const float delta_seconds{static_cast<float>(now - m_last_ticks) / 1000.0F};
      m_last_ticks = now;
      m_window->render_debug_ui_overlay(delta_seconds);
    }

    {
      APP_PROFILE_SCOPE("VideoPresentSwap");
      SDL_GL_SwapWindow(m_window->get_native_window());
    }
  }

 private:
  Window* m_window{nullptr};
  Video::VideoScene* m_scene{nullptr};
  std::uint64_t m_last_ticks{0};
};

}  // namespace

Application::Application(Application&& other) noexcept
    : m_window(std::move(other.m_window)),
      m_game_settings(std::move(other.m_game_settings)),
      m_audio_settings_listener_id(other.m_audio_settings_listener_id),
      m_display_settings_listener_id(other.m_display_settings_listener_id),
      m_settings_path(std::move(other.m_settings_path)),
      m_settings_persistence_enabled(other.m_settings_persistence_enabled),
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
      m_startup_waiting_for_main_menu(other.m_startup_waiting_for_main_menu),
      m_running(other.m_running),
      m_sdl_initialized(std::exchange(other.m_sdl_initialized, false)),
      m_mouse_captured(other.m_mouse_captured),
      m_capture_retry_cooldown(other.m_capture_retry_cooldown),
      m_pending_mouse_delta_x(other.m_pending_mouse_delta_x),
      m_pending_mouse_delta_y(other.m_pending_mouse_delta_y),
      m_activity(other.m_activity),
      m_held_input(other.m_held_input),
      m_text_input(other.m_text_input),
      m_frame_timing(other.m_frame_timing),
      m_last_engine_callback(other.m_last_engine_callback),
      m_skip_engine_frame(other.m_skip_engine_frame),
      m_accumulator(other.m_accumulator) {
  if (m_window != nullptr) {
    m_window->debug_ui().set_runtime_timing(this);
  }
}

std::expected<Application, std::string> Application::create(const std::string& title) {
  APP_PROFILE_FUNCTION();

  auto trace{std::make_unique<Startup::StartupTraceRecorder>()};
  auto coordinator{std::make_unique<Startup::StartupCoordinator>(*trace)};
  auto game_settings{std::make_unique<Settings::GameSettings>()};
  game_settings->ensure_number("audio.dialogue_volume", 0, 100, 10, 100);
  game_settings->ensure_number("audio.music_volume", 0, 100, 10, 100);
  game_settings->ensure_number("audio.sfx_volume", 0, 100, 10, 100);
  game_settings->ensure_number("audio.ambient_volume", 0, 100, 10, 100);
  game_settings->ensure_choice("audio.spatial_audio",
      {Settings::SettingChoice{.label = "Off", .raw_value = 0},
          Settings::SettingChoice{.label = "On", .raw_value = 1}},
      1U);
  game_settings->ensure_choice("enhancements.animation_interpolation",
      {Settings::SettingChoice{.label = "Off", .raw_value = 0},
          Settings::SettingChoice{.label = "On", .raw_value = 1}},
      1U);
  game_settings->ensure_choice("enhancements.menu_transition_style",
      {Settings::SettingChoice{.label = "Modern", .raw_value = 0},
          Settings::SettingChoice{.label = "Classic", .raw_value = 1},
          Settings::SettingChoice{.label = "Reduced Motion", .raw_value = 2}},
      0U);
  game_settings->ensure_choice("display.mode",
      {Settings::SettingChoice{.label = "Windowed", .raw_value = 0},
          Settings::SettingChoice{.label = "Borderless Fullscreen", .raw_value = 1},
          Settings::SettingChoice{.label = "Exclusive Fullscreen", .raw_value = 2}},
      1U);
  game_settings->ensure_choice("display.fullscreen_preference",
      {Settings::SettingChoice{.label = "Borderless", .raw_value = 1},
          Settings::SettingChoice{.label = "Exclusive", .raw_value = 2}},
      0U);
  game_settings->ensure_choice("video.clipping_distance",
      {Settings::SettingChoice{.label = "25 m", .raw_value = 25},
          Settings::SettingChoice{.label = "50 m", .raw_value = 50},
          Settings::SettingChoice{.label = "100 m", .raw_value = 100},
          Settings::SettingChoice{.label = "150 m", .raw_value = 150},
          Settings::SettingChoice{.label = "200 m", .raw_value = 200}},
      1U);
  game_settings->ensure_choice("video.display_sky",
      {Settings::SettingChoice{.label = "Off", .raw_value = 0},
          Settings::SettingChoice{.label = "On", .raw_value = 1}},
      1U);
  game_settings->ensure_choice("video.display_shadow",
      {Settings::SettingChoice{.label = "Off", .raw_value = 0},
          Settings::SettingChoice{.label = "On", .raw_value = 1}},
      1U);
  game_settings->ensure_choice("video.street_activity",
      {Settings::SettingChoice{.label = "0", .raw_value = 0},
          Settings::SettingChoice{.label = "1", .raw_value = 1},
          Settings::SettingChoice{.label = "2", .raw_value = 2},
          Settings::SettingChoice{.label = "3", .raw_value = 3},
          Settings::SettingChoice{.label = "4", .raw_value = 4}},
      3U);
  game_settings->ensure_choice("video.detail_level",
      {Settings::SettingChoice{.label = "0", .raw_value = 0},
          Settings::SettingChoice{.label = "1", .raw_value = 1},
          Settings::SettingChoice{.label = "2", .raw_value = 2}},
      1U);
  game_settings->ensure_choice("game.fight_difficulty",
      {Settings::SettingChoice{.label = "Easy", .raw_value = 0},
          Settings::SettingChoice{.label = "Normal", .raw_value = 1},
          Settings::SettingChoice{.label = "Hard", .raw_value = 2}},
      1U);
  game_settings->ensure_choice("game.shoot_difficulty",
      {Settings::SettingChoice{.label = "Easy", .raw_value = 0},
          Settings::SettingChoice{.label = "Normal", .raw_value = 1},
          Settings::SettingChoice{.label = "Hard", .raw_value = 2}},
      1U);
  game_settings->ensure_choice("game.fight_camera",
      {Settings::SettingChoice{.label = "Off", .raw_value = 0},
          Settings::SettingChoice{.label = "On", .raw_value = 1}},
      1U);
  App::Log::info(LogCategory::Core, "OpenNomad starting");

  // --- Phase 1: process bootstrap ---
  swallow_expected(coordinator->begin(Startup::StartupPhase::k_process_bootstrap));
  const unsigned int init_flags{SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO};
  if (!SDL_Init(init_flags)) {
    return std::expected<Application, std::string>{std::unexpect,
        fmt::format("Can't initialize Omikron: SDL_Init failed: {}", SDL_GetError())};
  }
  App::Log::debug(LogCategory::Core, "SDL video driver: {}", SDL_GetCurrentVideoDriver());

  const std::filesystem::path settings_path{Resources::get_user_config_path() / "settings.cfg"};
  bool settings_persistence_enabled{true};
  if (auto result{game_settings->load(settings_path)}; !result) {
    std::error_code error;
    if (std::filesystem::exists(settings_path, error) && !error) {
      settings_persistence_enabled = false;
      App::Log::warn(
          LogCategory::Core, "native settings disabled for this session: {}", result.error());
    }
  }
  const SDL_DisplayMode* primary_desktop{SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay())};
  const DisplayResolution default_resolution{
      .width = primary_desktop == nullptr ? 800 : primary_desktop->w,
      .height = primary_desktop == nullptr ? 600 : primary_desktop->h};
  std::vector<Settings::SettingChoice> startup_resolutions{
      Settings::SettingChoice{.label = display_resolution_label(default_resolution),
          .raw_value = pack_display_resolution(default_resolution).value_or(0)}};
  if (const auto saved{game_settings->loaded_choice_raw_value("display.resolution")};
      saved.has_value()) {
    if (const auto resolution{unpack_display_resolution(saved.value())};
        resolution.has_value() && resolution.value() != default_resolution) {
      startup_resolutions.push_back(Settings::SettingChoice{
          .label = display_resolution_label(resolution.value()), .raw_value = saved.value()});
    }
  }
  game_settings->ensure_choice("display.resolution", std::move(startup_resolutions), 0U);

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
  const DisplayMode startup_mode{
      static_cast<DisplayMode>(game_settings->choice_raw_value("display.mode").value_or(1))};
  const DisplayResolution startup_resolution{
      unpack_display_resolution(game_settings->choice_raw_value("display.resolution").value_or(0))
          .value_or(DisplayResolution{.width = 800, .height = 600})};
  auto window{Window::create(Window::Settings{.title = title,
      .width = startup_resolution.width,
      .height = startup_resolution.height,
      .display_mode = startup_mode,
      .resolution = startup_resolution,
      .hidden = true})};
  if (!window) {
    SDL_Quit();
    return std::expected<Application, std::string>{
        std::unexpect, fmt::format("Can't initialize Omikron: {}", std::move(window).error())};
  }
  App::Log::info(LogCategory::Core,
      "window created — {}x{}, {}",
      window.value()->get_width(),
      window.value()->get_height(),
      SDL_GetCurrentVideoDriver());
  trace->record("Window.Created");
  swallow_expected(coordinator->complete(
      Startup::StartupPhase::k_create_windows, Startup::StartupPhaseStatus::k_complete));

  // --- Phase 3: initialize core engine systems ---
  swallow_expected(coordinator->begin(Startup::StartupPhase::k_initialize_core_systems));
  auto audio{Audio::AudioSystem::create()};
  if (audio) {
    trace->record("Audio.Initialized");
  } else {
    App::Log::warn(LogCategory::Audio, "Audio disabled: {}", audio.error());
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
    return std::expected<Application, std::string>{
        std::unexpect, fmt::format("Can't initialize Omikron: {}", result.error())};
  }
  swallow_expected(coordinator->complete(
      Startup::StartupPhase::k_initialize_core_systems, Startup::StartupPhaseStatus::k_complete));

  // Build the application object before the startup videos so their playback
  // loop can pump events through the normal application path and resolve the
  // skip action through the input manager. Moving the owners here also means
  // every failure path below lets ~Application release them.
  Application app;
  app.m_window = std::move(window).value();
  app.m_game_settings = std::move(game_settings);
  app.m_settings_path = settings_path;
  app.m_settings_persistence_enabled = settings_persistence_enabled;
  app.m_trace = std::move(trace);
  app.m_coordinator = std::move(coordinator);
  app.m_scenario_manager = std::move(manager);
  app.m_scenario_engine = std::move(engine);
  if (audio) {
    app.m_audio = std::move(audio).value();
    for (const std::string_view stable_id : {"audio.dialogue_volume",
             "audio.music_volume",
             "audio.sfx_volume",
             "audio.ambient_volume",
             "audio.spatial_audio"}) {
      apply_audio_setting(*app.m_game_settings, *app.m_audio, stable_id);
    }
    Settings::GameSettings* const settings_ptr{app.m_game_settings.get()};
    Audio::AudioSystem* const audio_ptr{app.m_audio.get()};
    app.m_audio_settings_listener_id = settings_ptr->add_change_listener(
        [settings_ptr, audio_ptr](const std::string_view stable_id) {
          apply_audio_setting(*settings_ptr, *audio_ptr, stable_id);
        });
    app.m_scenario_manager->set_audio_system(app.m_audio.get());
    app.m_scenario_engine->set_audio_system(app.m_audio.get());
  }
  app.m_interface_manager = std::make_unique<Interface::InterfaceManager>(*app.m_game_settings);
  app.m_interface_manager->set_window(app.m_window.get());
  Settings::GameSettings* const display_settings{app.m_game_settings.get()};
  Window* const display_window{app.m_window.get()};
  app.m_display_settings_listener_id = display_settings->add_change_listener(
      [display_settings, display_window, manager = app.m_interface_manager.get()](
          const std::string_view stable_id) {
        apply_display_setting(*display_settings, *display_window, stable_id);
        if (stable_id == "display.mode" || stable_id == "display.resolution") {
          manager->refresh_display_options();
        }
      });
  app.m_window->set_display_state_callback(
      [display_settings, display_window, manager = app.m_interface_manager.get()](
          const bool catalog_changed) {
        const DisplayMode actual_mode{display_window->reconcile_display_state()};
        display_settings->set_choice_raw_value(
            "display.mode", static_cast<std::int32_t>(actual_mode), false);
        if (actual_mode != DisplayMode::k_borderless_fullscreen) {
          const DisplayResolution actual{display_window->actual_resolution()};
          manager->refresh_display_options();
          if (const auto packed{pack_display_resolution(actual)}; packed.has_value()) {
            display_settings->set_choice_raw_value("display.resolution", packed.value(), false);
          }
        } else {
          manager->refresh_display_options();
        }
        if (catalog_changed && actual_mode == DisplayMode::k_exclusive_fullscreen) {
          apply_display_setting(*display_settings, *display_window, "display.resolution");
          manager->refresh_display_options();
        }
      });
  if (app.m_window->actual_display_mode() != startup_mode) {
    App::Log::warn(LogCategory::Renderer,
        "saved display mode {} was not established during window creation; reconciling",
        display_mode_label(startup_mode));
    apply_display_setting(*display_settings, *app.m_window, "display.mode");
  }
  static_cast<void>(app.m_window->show());
  app.m_window->set_display_shortcut_callback([display_settings]() {
    const DisplayMode actual{
        static_cast<DisplayMode>(display_settings->choice_raw_value("display.mode").value_or(1))};
    const DisplayMode preferred{static_cast<DisplayMode>(
        display_settings->choice_raw_value("display.fullscreen_preference").value_or(1))};
    display_settings->set_choice_raw_value(
        "display.mode", static_cast<std::int32_t>(toggle_display_mode(actual, preferred)));
  });
  if (app.m_audio != nullptr) {
    app.m_interface_manager->set_audio_system(app.m_audio.get());
  }
  // The skip action must be registered before the videos: the playback loop
  // asks the input manager whether Escape was pressed this frame.
  app.m_input.add_scheme(Input::ControlScheme::make_keyboard_mouse_default());

  // Capture the mouse FPS-style from the very start. F12 releases it and
  // clicking the window re-captures it (poll_events). Requesting it before
  // the videos lets the focus-gained / retry paths heal an early failure
  // (the window may not be focused until its first present on Wayland).
  app.set_mouse_captured(true);

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
    play_phase(
        Startup::StartupPhase::k_play_publisher_video, Startup::StartupVideoSlot::k_publisher);
    play_phase(
        Startup::StartupPhase::k_play_developer_video, Startup::StartupVideoSlot::k_developer);
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
    return std::expected<Application, std::string>{
        std::unexpect, fmt::format("Can't initialize Omikron: {}", result.error())};
  }
  swallow_expected(
      app.m_coordinator->complete(Startup::StartupPhase::k_select_permanent_mode_script,
          Startup::StartupPhaseStatus::k_complete));

  // --- Phase 8: prepare the splash ---
  swallow_expected(app.m_coordinator->begin(Startup::StartupPhase::k_prepare_splash));
  auto splash{SplashScene::create(kSplashDuration)};
  if (!splash) {
    app.m_scenario_engine.reset();
    app.m_scenario_manager.reset();
    app.m_audio.reset();
    app.m_window.reset();
    SDL_Quit();
    return std::expected<Application, std::string>{
        std::unexpect, fmt::format("Can't initialize Omikron: {}", std::move(splash).error())};
  }
  app.m_scenario_engine->open_preliminary_29();
  app.m_trace->record("Splash.Omikron.Prepared");
  swallow_expected(app.m_coordinator->complete(
      Startup::StartupPhase::k_prepare_splash, Startup::StartupPhaseStatus::k_complete));

  app.m_scene = std::move(splash).value();
  app.m_window->debug_ui().set_context(Debug::DebugContext{.scene = app.m_scene.get(),
      .scenario_manager = app.m_scenario_manager.get(),
      .scenario_engine = app.m_scenario_engine.get(),
      .interface_manager = app.m_interface_manager.get(),
      .audio_system = app.m_audio.get(),
      .startup_coordinator = app.m_coordinator.get(),
      .startup_trace = app.m_trace.get(),
      .runtime_timing = &app});
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

  app.m_sdl_initialized = true;
  return app;
}

Application::~Application() {
  APP_PROFILE_FUNCTION();

  if (m_settings_persistence_enabled && m_game_settings != nullptr) {
    if (auto result{m_game_settings->save(m_settings_path)}; !result) {
      App::Log::warn(LogCategory::Core, "failed to save native settings: {}", result.error());
    }
  }
  if (m_game_settings != nullptr && m_audio_settings_listener_id != 0) {
    m_game_settings->remove_change_listener(m_audio_settings_listener_id);
  }
  if (m_game_settings != nullptr && m_display_settings_listener_id != 0) {
    m_game_settings->remove_change_listener(m_display_settings_listener_id);
  }

  // Release every GL-owning subsystem while the window's GL context is still
  // alive, then destroy the window (and its context) last. The interface
  // manager owns the I2D renderer and bump background, whose destructors call
  // glDelete*; without this ordering those calls run against a dead context
  // (a quit / ALT+F4 segfault). The remaining members are CPU-only but are
  // released in dependency order (engine -> manager -> coordinator -> trace).
  m_scene.reset();
  m_interface_manager.reset();
  m_scenario_engine.reset();
  m_scenario_manager.reset();
  m_coordinator.reset();
  m_trace.reset();
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
  App::Log::info(LogCategory::Startup, "displaying Omikron splash");

  // The startup videos already presented the first frames; ask the
  // compositor for keyboard focus again now that the surface is mapped.
  // Without focus the FPS-style mouse capture cannot engage on Wayland,
  // and the pre-present activation request made while showing the window
  // may have been ignored by the compositor.
  SDL_RaiseWindow(m_window->get_native_window());

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
    m_last_engine_callback.begin_timed_frame();
    FrameTiming::run_timed_frame(
        m_frame_timing,
        decision.reset_frame_timing,
        m_skip_engine_frame,
        []() -> std::uint64_t {
          return SDL_GetTicks();
        },
        [this] {
          snapshot_input();
        },
        [this] {
          m_last_engine_callback.record_callback(m_frame_timing.effective_delta);
          run_engine_frame();
        });

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

  // A genuine post-splash startup failure stops the application. Do not
  // continue updating/rendering subsystems for the remainder of this frame.
  // A normal AREA yield while waiting for interface 29 leaves m_running true,
  // so the scenario update below still executes the next recovered tick.
  if (!m_running) {
    return;
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

  // Feed the CTL player-input profile mask (semantic actions, never raw
  // devices) to the scenario layer before the scenario scheduler services
  // character controllers.
  if (m_scenario_manager != nullptr) {
    m_scenario_manager->set_ctl_input_mask(m_input.ctl_profile_mask());
  }

  // Continue the scenario scheduler on the normal frame path. The initial
  // mode-1 execution already happened during startup; this keeps a waiting
  // or resumed AREA script progressing without re-entering a scenario mode.
  update_scenario(delta_seconds);

  // Runtime evaluates presentation cameras after compact/structured script
  // service and actor service, not before them. WorldScene::update() above is
  // deliberately the input/UI phase; this post-scenario phase consumes the
  // camera controller command and structured camera source published during
  // update_scenario() in the SAME engine frame.
  if (m_scene != nullptr) {
    m_scene->post_scenario_update(delta_seconds);
  }

  // Update the audio subsystem once per executed frame using real seconds
  // (never Omikron 30 Hz delta units). SDL3_mixer mixes asynchronously, but
  // the main-thread update drains events and rebuilds diagnostics. WorldScene
  // has already committed the final camera/listener pose for this frame.
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
  if (captured) {
    // Hide the cursor immediately so it never flashes while the window is
    // still unfocused. Wayland only engages the pointer lock once the window
    // has keyboard focus, so relative mode alone cannot hide it yet.
    SDL_HideCursor();
  } else {
    SDL_ShowCursor();
  }
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

Debug::RuntimeTimingDebugSnapshot Application::timing_debug_snapshot() const {
  return Debug::make_runtime_timing_debug_snapshot(
      m_frame_timing, m_activity, m_skip_engine_frame, m_last_engine_callback);
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
      [this](const InterfaceOpenRequest& request) -> std::expected<InterfaceHandle, std::string> {
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
    App::Log::error(LogCategory::Scenario, "Scenario update failed: {}", result.error());
  }
}

bool Application::advance_startup_past_splash() {
  APP_PROFILE_FUNCTION();

  if (m_trace == nullptr || m_coordinator == nullptr || m_scenario_engine == nullptr) {
    return false;
  }

  // Finishes the phase that may span multiple AREA ticks. Runtime's AREA
  // dispatcher yields after presentation opcode 0x76, which appears directly
  // before the main-menu 0x46 in the initial event. Therefore interface 29 is
  // not required to exist during the first mode-1 call.
  const auto finish_main_menu_phase = [this]() -> bool {
    if (auto result{m_coordinator->complete(
            Startup::StartupPhase::k_open_main_menu, Startup::StartupPhaseStatus::k_complete)};
        !result) {
      App::Log::error(LogCategory::Startup, "Startup ordering error: {}", result.error());
      m_running = false;
      return false;
    }
    if (auto result{m_coordinator->finish()}; !result) {
      App::Log::error(LogCategory::Startup, "Startup ordering error: {}", result.error());
      m_running = false;
      return false;
    }

    m_startup_waiting_for_main_menu = false;
    App::Log::info(LogCategory::Startup, "startup complete");
    m_input.reset();
    return true;
  };

  // Re-entry after the initial mode-1 tick yielded before opcode 0x46.
  // The normal ScenarioEngine::update() path runs between calls and advances
  // the AREA context one recovered scenario tick at a time.
  if (m_startup_waiting_for_main_menu) {
    if (!m_scenario_engine->main_menu_active()) {
      return false;
    }
    return finish_main_menu_phase();
  }

  m_trace->record("Splash.Omikron.Complete");

  if (auto result{m_coordinator->complete(
          Startup::StartupPhase::k_run_splash, Startup::StartupPhaseStatus::k_complete)};
      !result) {
    App::Log::error(LogCategory::Startup, "Startup ordering error: {}", result.error());
    m_running = false;
    return false;
  }

  const auto run_phase = [this](
                             const Startup::StartupPhase phase, const ScenarioMode mode) -> bool {
    if (auto result{m_coordinator->begin(phase)}; !result) {
      App::Log::error(LogCategory::Startup, "Startup ordering error: {}", result.error());
      m_running = false;
      return false;
    }
    if (auto result{m_scenario_engine->enter_mode(mode, 0)}; !result) {
      App::Log::error(LogCategory::Core, "Can't initialize Omikron: {}", result.error());
      swallow_expected(
          m_coordinator->complete(phase, Startup::StartupPhaseStatus::k_failed, result.error()));
      m_running = false;
      return false;
    }
    if (auto result{m_coordinator->complete(phase, Startup::StartupPhaseStatus::k_complete)};
        !result) {
      App::Log::error(LogCategory::Startup, "Startup ordering error: {}", result.error());
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
  // rendered in the middle of this synchronous startup sequence, so the
  // initial world context is never exposed before the main menu.
  {
    auto world{WorldScene::create(*m_scenario_manager, *m_interface_manager)};
    if (!world) {
      App::Log::error(LogCategory::Core, "Can't initialize Omikron: {}", std::move(world).error());
      m_running = false;
      return false;
    }
    m_scene = std::move(world).value();
    m_window->debug_ui().set_scene(m_scene.get());
  }

  if (!run_phase(Startup::StartupPhase::k_run_initial_area_script, ScenarioMode::k_tick)) {
    return false;
  }

  // Begin the main-menu phase, but do not require opcode 0x46 to have run in
  // this same mode-1 call. The recovered AREA VM deliberately yields after
  // opcode 0x76 immediately before it. The normal per-frame scenario update
  // will resume the context and reach 0x46 on a subsequent tick.
  if (auto result{m_coordinator->begin(Startup::StartupPhase::k_open_main_menu)}; !result) {
    App::Log::error(LogCategory::Startup, "Startup ordering error: {}", result.error());
    m_running = false;
    return false;
  }

  m_startup_waiting_for_main_menu = true;

  if (!m_scenario_engine->main_menu_active()) {
    // Not an error: AREA yielded at a recovered side-effect boundary.
    return false;
  }

  // Usually false on retail data because 0x76 yielded immediately before
  // 0x46, but retain the synchronous case for other AREA programs.
  return finish_main_menu_phase();
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
    App::Log::warn(LogCategory::Core, "SDL_WaitEvent failed: {}", SDL_GetError());
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
  }

  ImGui_ImplSDL3_ProcessEvent(&event);

  // Application-level termination.
  if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_TERMINATING) {
    stop();
    return;
  }

  if (event.type >= SDL_EVENT_DISPLAY_FIRST && event.type <= SDL_EVENT_DISPLAY_LAST) {
    m_window->notify_display_state_changed();
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
          App::Log::debug(LogCategory::Input,
              "Mouse capture: focus gained (flag: {})",
              m_window->is_relative_mouse_mode());
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
        App::Log::debug(LogCategory::Input,
            "Mouse capture: focus lost (flag: {})",
            m_window->is_relative_mouse_mode());
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
          App::Log::debug(LogCategory::Input,
              "Mouse capture: mouse entered (flag: {})",
              m_window->is_relative_mouse_mode());
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
