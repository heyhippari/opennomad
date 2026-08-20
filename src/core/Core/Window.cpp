#include "Window.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <expected>
#include <memory>
#include <string>
#include <utility>

#include "Core/DPIHandler.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Renderer.hpp"
#include "Core/Resources.hpp"

namespace App {

std::expected<std::unique_ptr<Window>, std::string> Window::create(const Settings& settings) {
  // The constructor is private; only the factory may build a Window.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  std::unique_ptr<Window> window{new Window(settings)};
  if (window->m_gl_context.get() == nullptr) {
    return std::expected<std::unique_ptr<Window>, std::string>{
        std::unexpect, "Could not create SDL OpenGL context."};
  }
  return window;
}

Window::Window(const Settings& settings)
    : m_settings(DPIHandler::get_dpi_aware_window_size(settings)),
      m_size{.width = m_settings.width,
          .height = m_settings.height,
          .pixel_width = m_settings.width,
          .pixel_height = m_settings.height},
      m_windowed_width{m_settings.width},
      m_windowed_height{m_settings.height} {
  APP_PROFILE_FUNCTION();

  // Create window with graphics context
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  // Explicit framebuffer color depth for predictable behaviour across drivers.
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

  // Anti-aliasing: 4x MSAA.
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
  SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

  // Gamma-correct rendering pipeline.
  SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);

  // Flush the GL command stream when the context is destroyed.
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_RELEASE_BEHAVIOR, SDL_GL_CONTEXT_RELEASE_BEHAVIOR_FLUSH);

  // SDL_CreateWindow cannot be used in a member initializer (needs m_settings first).
  // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
  SDL_WindowFlags window_flags{SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                               SDL_WINDOW_RESIZABLE};
  if (m_settings.start_fullscreen) {
    window_flags |= SDL_WINDOW_FULLSCREEN;
    m_window_mode = WindowMode::BorderlessFullscreen;
  }
  m_window.reset(SDL_CreateWindow(
      settings.title.c_str(), m_settings.width, m_settings.height, window_flags));

  SDL_SetWindowMinimumSize(m_window.get(), 640, 480);

  // Seed the drawable size from SDL before the first resize event arrives.
  int pixel_width{0};
  int pixel_height{0};
  if (SDL_GetWindowSizeInPixels(m_window.get(), &pixel_width, &pixel_height) && pixel_width > 0 &&
      pixel_height > 0) {
    m_size.on_pixel_size_changed(pixel_width, pixel_height);
  }

  // NOLINTNEXTLINE
  if (!m_gl_context.create(m_window.get())) {
    App::Log::error(LogCategory::Renderer, "Could not create SDL OpenGL context.");
    return;
  }

  SDL_SetWindowPosition(m_window.get(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) Required by the glad loader API.
  gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress));
  SDL_GL_MakeCurrent(m_window.get(), m_gl_context.get());
  SDL_GL_SetSwapInterval(1);  // Enable vsync

  // --- Debug UI ---
  m_debug_ui.set_window(m_window.get());

  // Query system info once the GL context is ready.
  Debug::Metrics::get().query_system_info();
  Debug::Metrics::get().set_audio_info_entry("Status", "Initializing...");

  // --- OpenGL render state initialisation ---
  m_renderer.init();

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io{ImGui::GetIO()};

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_ViewportsEnable;

  // Absolute imgui.ini path to preserve settings independent of app location.
  static const std::string imgui_ini_filename{m_user_config_path.generic_string() + "imgui.ini"};
  io.IniFilename = imgui_ini_filename.c_str();

  // ImGUI font
  const float font_scaling_factor{SDL_GetWindowDisplayScale(m_window.get())};
  const float font_size{18.0F * font_scaling_factor};
  const std::string font_path{Resources::font_path("Manrope.ttf").generic_string()};

  io.Fonts->AddFontFromFileTTF(font_path.c_str(), font_size);
  io.FontDefault = io.Fonts->AddFontFromFileTTF(font_path.c_str(), font_size);
  io.FontGlobalScale = 1.0F / font_scaling_factor;

  // Setup Platform/Renderer backends
  ImGui_ImplSDL3_InitForOpenGL(m_window.get(), m_gl_context.get());
  ImGui_ImplOpenGL3_Init("#version 410 core");
}

Window::GlContext::~GlContext() {
  if (m_context != nullptr) {
    SDL_GL_DestroyContext(m_context);
  }
}

Window::GlContext::GlContext(GlContext&& other) noexcept
    : m_context(std::exchange(other.m_context, nullptr)) {}

Window::GlContext& Window::GlContext::operator=(GlContext&& other) noexcept {
  if (this != &other) {
    if (m_context != nullptr) {
      SDL_GL_DestroyContext(m_context);
    }
    m_context = std::exchange(other.m_context, nullptr);
  }
  return *this;
}

bool Window::GlContext::create(SDL_Window* const window) {
  m_context = SDL_GL_CreateContext(window);
  return m_context != nullptr;
}

SDL_GLContext Window::GlContext::get() const {
  return m_context;
}

Window::~Window() {
  APP_PROFILE_FUNCTION();

  // The GL context and window are released by their RAII members afterwards,
  // so ImGui can be torn down here while the context is still alive.
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

void Window::begin_frame(const float delta_time) {
  APP_PROFILE_FUNCTION();

  // Start the Dear ImGui frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  if (!m_minimized) {
    m_debug_ui.update(delta_time);
  }

  // Build the ImGui draw data and clear the framebuffer; the scene is
  // rendered by the application between begin_frame and end_frame. The
  // viewport is the drawable pixel size, distinct from the logical size on
  // high-DPI displays.
  ImGui::Render();

  m_renderer.begin_frame(m_size.pixel_width, m_size.pixel_height);
}

void Window::end_frame() {
  APP_PROFILE_FUNCTION();

  // Draw the UI on top of the scene and present the backbuffer.
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  const ImGuiIO& io{ImGui::GetIO()};
  if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
    SDL_Window* backup_current_window{SDL_GL_GetCurrentWindow()};
    SDL_GLContext backup_current_context{SDL_GL_GetCurrentContext()};
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
  }

  SDL_GL_SwapWindow(m_window.get());
}

void Window::render_debug_ui_overlay(const float delta_time) {
  APP_PROFILE_FUNCTION();

  // Mirror begin_frame()'s ImGui plumbing but do not clear the backbuffer:
  // the caller has already drawn content (a startup video frame) that the
  // debug UI must be composited on top of. The backbuffer is presented by
  // the caller, not here.
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  if (!m_minimized) {
    m_debug_ui.update(delta_time);
  }

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  const ImGuiIO& io{ImGui::GetIO()};
  if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
    SDL_Window* backup_current_window{SDL_GL_GetCurrentWindow()};
    SDL_GLContext backup_current_context{SDL_GL_GetCurrentContext()};
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
  }
}

void Window::on_minimize() {
  APP_PROFILE_FUNCTION();

  m_minimized = true;
}

void Window::on_shown() {
  APP_PROFILE_FUNCTION();

  m_minimized = false;
}

void Window::on_close() {
  APP_PROFILE_FUNCTION();

  SDL_Event window_close_event;
  window_close_event.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&window_close_event);
}

void Window::on_event(const SDL_WindowEvent& event) {
  APP_PROFILE_FUNCTION();

  switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      on_close();
      return;
    case SDL_EVENT_WINDOW_MINIMIZED:
      on_minimize();
      return;
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_RESTORED:
      on_shown();
      return;
    case SDL_EVENT_WINDOW_RESIZED:
      m_size.on_resized(event.data1, event.data2);
      return;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      m_size.on_pixel_size_changed(event.data1, event.data2);
      return;
    default:
      // Do nothing otherwise
      return;
  }
}

void Window::toggle_fullscreen() {
  APP_PROFILE_FUNCTION();

  if (m_window_mode == WindowMode::Windowed) {
    // Save the current windowed geometry so we can restore it later.
    SDL_GetWindowSize(m_window.get(), &m_windowed_width, &m_windowed_height);
    SDL_GetWindowPosition(m_window.get(), &m_window_pos_x, &m_window_pos_y);

    SDL_SetWindowFullscreen(m_window.get(), true);
    m_window_mode = WindowMode::BorderlessFullscreen;
  } else {
    // Set the desired windowed geometry *before* leaving fullscreen — this
    // avoids a race where SDL3 auto-restores an unexpected size and then a
    // subsequent SDL_SetWindowSize call is ignored by the window manager.
    SDL_SetWindowSize(m_window.get(), m_windowed_width, m_windowed_height);
    SDL_SetWindowPosition(m_window.get(), m_window_pos_x, m_window_pos_y);
    SDL_SetWindowFullscreen(m_window.get(), false);
    m_window_mode = WindowMode::Windowed;
  }
}

void Window::on_keyboard_event(const SDL_KeyboardEvent& event) {
  APP_PROFILE_FUNCTION();

  if (event.type != SDL_EVENT_KEY_DOWN || event.repeat) {
    return;
  }

  // Alt+Enter or F11 toggles fullscreen.
  const bool alt_enter{(event.key == SDLK_RETURN) && ((event.mod & SDL_KMOD_ALT) != 0)};
  const bool f11{event.key == SDLK_F11};

  if (alt_enter || f11) {
    toggle_fullscreen();
    return;
  }

  // F3 toggles the performance overlay.
  if (event.key == SDLK_F3) {
    m_debug_ui.toggle_performance();
    return;
  }
}

SDL_Window* Window::get_native_window() const {
  APP_PROFILE_FUNCTION();

  return m_window.get();
}

Debug::DebugUI& Window::debug_ui() { return m_debug_ui; }

bool Window::set_relative_mouse_mode(const bool enabled) {
  APP_PROFILE_FUNCTION();

  if (SDL_SetWindowRelativeMouseMode(m_window.get(), enabled)) {
    return true;
  }
  // SDL rolls the relative-mode flag back when an enable attempt fails, so
  // callers can retry later. Disable failures are unexpected and minor.
  if (enabled) {
    App::Log::warn(LogCategory::Input, "Failed to enable relative mouse mode: {}", SDL_GetError());
  } else {
    App::Log::debug(
        LogCategory::Input, "Failed to disable relative mouse mode: {}", SDL_GetError());
  }
  return false;
}

bool Window::is_relative_mouse_mode() const {
  return SDL_GetWindowRelativeMouseMode(m_window.get());
}

bool Window::has_keyboard_focus() const {
  return (SDL_GetWindowFlags(m_window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0U;
}

bool Window::is_minimized() const {
  return m_minimized;
}

int Window::get_width() const {
  return m_size.width;
}

int Window::get_height() const {
  return m_size.height;
}

int Window::get_pixel_width() const {
  return m_size.pixel_width;
}

int Window::get_pixel_height() const {
  return m_size.pixel_height;
}

}  // namespace App
