#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "Core/Debug/DebugUI.hpp"
#include "Core/Renderer.hpp"
#include "Core/Resources.hpp"
#include "Core/WindowSizeState.hpp"

namespace App {

enum class WindowMode : std::uint8_t { Windowed, BorderlessFullscreen };

class Window {
 public:
  struct Settings {
    std::string title;
    int width{800};
    int height{600};
    /// Creates the window already fullscreen; F11 / Alt+Enter then restores
    /// the windowed size.
    bool start_fullscreen{false};
  };

  /// Creates the SDL window, GL context, ImGui backend and renderer state.
  static std::expected<std::unique_ptr<Window>, std::string> create(const Settings& settings);

  ~Window();

  Window(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window other) = delete;
  Window& operator=(Window&& other) = delete;

  /// Start a frame: ImGui frame begin, debug UI update, framebuffer clear.
  void begin_frame(float delta_time);

  /// Finish a frame: draw the UI over the scene and present the backbuffer.
  void end_frame();

  /// Renders the debug UI over the current backbuffer without clearing it.
  /// Used by the startup-video presenter, which draws video frames directly
  /// before swapping and bypasses the normal begin/end frame pair.
  void render_debug_ui_overlay(float delta_time);

  void toggle_fullscreen();
  void on_minimize();
  void on_shown();
  static void on_close();
  void on_event(const SDL_WindowEvent& event);
  void on_keyboard_event(const SDL_KeyboardEvent& event);

  /// Switches between relative (captured, cursor hidden) and absolute mouse
  /// mode. Used by the FPS-style mouse-look toggle.
  /// \return false if SDL could not apply the mode (details are logged).
  bool set_relative_mouse_mode(bool enabled);

  /// The debug UI (ImGui windows); scene wiring and toggles go through it.
  [[nodiscard]] Debug::DebugUI& debug_ui();

  /// True while SDL wants the window in relative mouse mode. SDL keeps the
  /// flag across focus loss and clears it when an activation attempt fails.
  [[nodiscard]] bool is_relative_mouse_mode() const;

  /// True while the window holds keyboard focus.
  [[nodiscard]] bool has_keyboard_focus() const;

  [[nodiscard]] SDL_Window* get_native_window() const;
  [[nodiscard]] bool is_minimized() const;
  [[nodiscard]] int get_width() const;
  [[nodiscard]] int get_height() const;
  /// Drawable (backbuffer) size in pixels; distinct from the logical size
  /// on high-DPI displays.
  [[nodiscard]] int get_pixel_width() const;
  [[nodiscard]] int get_pixel_height() const;

 private:
  /// Frees an SDL_Window — std::unique_ptr deleter.
  struct WindowDeleter {
    void operator()(SDL_Window* window) const noexcept {
      SDL_DestroyWindow(window);
    }
  };

  /// RAII owner for the void*-typed SDL GL context (std::unique_ptr cannot
  /// hold void, so a small movable wrapper stands in).
  class GlContext {
   public:
    GlContext() = default;
    ~GlContext();

    GlContext(const GlContext&) = delete;
    GlContext& operator=(const GlContext&) = delete;
    GlContext(GlContext&& other) noexcept;
    GlContext& operator=(GlContext&& other) noexcept;

    [[nodiscard]] bool create(SDL_Window* window);
    [[nodiscard]] SDL_GLContext get() const;

   private:
    SDL_GLContext m_context{nullptr};
  };

  explicit Window(const Settings& settings);

  const Settings m_settings;
  // Declaration order matters: members are destroyed in reverse order, so the
  // GL context (declared second) is destroyed before the window.
  std::unique_ptr<SDL_Window, WindowDeleter> m_window;
  GlContext m_gl_context;

  WindowMode m_window_mode{WindowMode::Windowed};
  bool m_minimized{false};

  /// Cached logical and drawable dimensions, updated from SDL resize events.
  WindowSizeState m_size;
  /// Windowed geometry to restore when leaving fullscreen. Kept separate
  /// from m_size, which tracks the live (possibly fullscreen) size.
  int m_windowed_width{800};
  int m_windowed_height{600};
  int m_window_pos_x{SDL_WINDOWPOS_CENTERED};
  int m_window_pos_y{SDL_WINDOWPOS_CENTERED};

  Renderer m_renderer;

  Debug::DebugUI m_debug_ui;

  const std::filesystem::path m_user_config_path{Resources::get_user_config_path()};
};

}  // namespace App
