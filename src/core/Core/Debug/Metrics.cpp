#include "Metrics.hpp"

#include <glad/glad.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_version.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <flat_map>
#include <string>

#include <fmt/format.h>

namespace App::Debug {

namespace {

/// Convert a GLubyte string to std::string safely.
[[nodiscard]] std::string gl_string(const GLubyte* gl_str) {
  if (gl_str == nullptr) {
    return "N/A";
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return {reinterpret_cast<const char*>(gl_str)};
}

}  // anonymous namespace

Metrics& Metrics::get() {
  static Metrics instance;
  return instance;
}

// --- Frame timing ---

void Metrics::on_frame_begin(const float delta_time) {
  m_frame_time_ms = delta_time * 1000.0F;
  m_frame_count += 1;
  m_total_elapsed += delta_time;

  // Update min/avg/max
  if (m_frame_count == 1) {
    m_frame_time_min = m_frame_time_ms;
    m_frame_time_max = m_frame_time_ms;
    m_frame_time_avg = m_frame_time_ms;
  } else {
    m_frame_time_min = std::min(m_frame_time_min, m_frame_time_ms);
    m_frame_time_max = std::max(m_frame_time_max, m_frame_time_ms);
    // Welford-style incremental average
    m_frame_time_avg += (m_frame_time_ms - m_frame_time_avg) / static_cast<float>(m_frame_count);
  }

  // Rolling history (circular buffer).
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  m_frame_time_history[m_history_head] = m_frame_time_ms;
  m_history_head = (m_history_head + 1) % kHistorySize;
  if (m_history_count < kHistorySize) {
    m_history_count += 1;
  }
}

float Metrics::get_fps() const {
  if (m_frame_time_ms <= 0.0F) {
    return 0.0F;
  }
  return 1000.0F / m_frame_time_ms;
}

float Metrics::get_frame_time_ms() const { return m_frame_time_ms; }

const std::array<float, Metrics::kHistorySize>& Metrics::get_frame_time_history() const {
  return m_frame_time_history;
}

std::size_t Metrics::get_frame_time_history_head() const { return m_history_head; }

std::size_t Metrics::get_frame_time_history_count() const { return m_history_count; }

float Metrics::get_frame_time_min() const { return m_frame_time_min; }

float Metrics::get_frame_time_avg() const { return m_frame_time_avg; }

float Metrics::get_frame_time_max() const { return m_frame_time_max; }

std::uint64_t Metrics::get_frame_count() const { return m_frame_count; }

float Metrics::get_total_elapsed() const { return m_total_elapsed; }

// --- System info ---

const std::flat_map<std::string, std::string>& Metrics::opengl_info() const { return m_opengl_info; }

const std::flat_map<std::string, std::string>& Metrics::sdl_info() const { return m_sdl_info; }

const std::flat_map<std::string, std::string>& Metrics::window_info() const { return m_window_info; }

const std::flat_map<std::string, std::string>& Metrics::audio_info() const { return m_audio_info; }

void Metrics::query_system_info() {
  if (m_system_info_queried) {
    return;
  }
  m_system_info_queried = true;

  // --- OpenGL ---
  m_opengl_info["Vendor"]   = gl_string(glGetString(GL_VENDOR));
  m_opengl_info["Renderer"] = gl_string(glGetString(GL_RENDERER));
  m_opengl_info["Version"]  = gl_string(glGetString(GL_VERSION));
  m_opengl_info["GLSL Version"] = gl_string(glGetString(GL_SHADING_LANGUAGE_VERSION));

  GLint gl_value{0};
  glGetIntegerv(GL_MAJOR_VERSION, &gl_value);
  m_opengl_info["GL Major"] = std::to_string(gl_value);
  glGetIntegerv(GL_MINOR_VERSION, &gl_value);
  m_opengl_info["GL Minor"] = std::to_string(gl_value);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_value);
  m_opengl_info["Max Texture Size"] = std::to_string(gl_value);
  glGetIntegerv(GL_MAX_SAMPLES, &gl_value);
  m_opengl_info["Max MSAA Samples"] = std::to_string(gl_value);
  glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &gl_value);
  m_opengl_info["Max Color Attachments"] = std::to_string(gl_value);
  glGetIntegerv(GL_MAX_DRAW_BUFFERS, &gl_value);
  m_opengl_info["Max Draw Buffers"] = std::to_string(gl_value);

  // --- SDL ---
  const int sdl_version_int{SDL_GetVersion()};
  const int sdl_major{SDL_VERSIONNUM_MAJOR(sdl_version_int)};
  const int sdl_minor{SDL_VERSIONNUM_MINOR(sdl_version_int)};
  const int sdl_micro{SDL_VERSIONNUM_MICRO(sdl_version_int)};
  m_sdl_info["SDL Version"] = fmt::format("{}.{}.{}", sdl_major, sdl_minor, sdl_micro);

  m_sdl_info["Platform"] = SDL_GetPlatform();

  int display_count{0};
  SDL_GetDisplays(&display_count);
  m_sdl_info["Display Count"] = std::to_string(display_count);
}

void Metrics::set_window_info_entry(const std::string& key, const std::string& value) {
  m_window_info[key] = value;
}

void Metrics::set_audio_info_entry(const std::string& key, const std::string& value) {
  m_audio_info[key] = value;
}

void Metrics::set_sprite_counters(const SpriteCounters& counters) {
  m_sprite_counters = counters;
}

const SpriteCounters& Metrics::sprite_counters() const { return m_sprite_counters; }

}  // namespace App::Debug
