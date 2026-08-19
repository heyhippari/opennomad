#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <string>

namespace App::Debug {

/// Per-frame sprite pipeline counters, pushed by the scene after rendering.
struct SpriteCounters {
  std::size_t live{0};
  std::size_t attached{0};
  std::size_t visible{0};
  std::size_t drawn{0};
  std::size_t culled{0};
  std::size_t invalid{0};
  std::size_t batches{0};
  std::size_t draw_calls{0};
};

/// Singleton collecting per-frame performance metrics and system information.
///
/// Thread-compatible (all methods are called from the main thread).
/// Compiled out when APP_DEBUG_UI is not defined (release builds).
class Metrics {
 public:
  /// Maximum number of frame time samples retained for the history graph.
  static constexpr std::size_t kHistorySize{300};

  Metrics(const Metrics&) = delete;
  Metrics(Metrics&&) = delete;
  Metrics& operator=(Metrics other) = delete;
  Metrics& operator=(Metrics&& other) = delete;

  static Metrics& get();

  // --- Frame timing ---

  /// Push a new frame sample. Called once per frame from Application::run().
  void on_frame_begin(float delta_time);

  /// Current instantaneous FPS (0 if no frames yet).
  [[nodiscard]] float get_fps() const;

  /// Current frame time in milliseconds.
  [[nodiscard]] float get_frame_time_ms() const;

  /// Rolling history of frame times (ms) for graphing. Size = kHistorySize.
  [[nodiscard]] const std::array<float, kHistorySize>& get_frame_time_history() const;

  /// Index into the history buffer of the most recent sample (for wrap-aware iteration).
  [[nodiscard]] std::size_t get_frame_time_history_head() const;

  /// Number of valid entries in the history buffer (≤ kHistorySize).
  [[nodiscard]] std::size_t get_frame_time_history_count() const;

  /// Session-wide min frame time (ms). Returns 0 before any frames.
  [[nodiscard]] float get_frame_time_min() const;

  /// Session-wide average frame time (ms).
  [[nodiscard]] float get_frame_time_avg() const;

  /// Session-wide max frame time (ms).
  [[nodiscard]] float get_frame_time_max() const;

  /// Total frame count since start.
  [[nodiscard]] std::uint64_t get_frame_count() const;

  /// Total elapsed wall-clock time since first frame (seconds).
  [[nodiscard]] float get_total_elapsed() const;

  // --- System info (queried on startup / on demand) ---

  /// OpenGL key-value info (vendor, renderer, version, GLSL version, ...).
  [[nodiscard]] const std::flat_map<std::string, std::string>& opengl_info() const;

  /// SDL key-value info (version, platform, display count, refresh rate, ...).
  [[nodiscard]] const std::flat_map<std::string, std::string>& sdl_info() const;

  /// Window key-value info (size, DPI, VSync, mode, ...).
  [[nodiscard]] const std::flat_map<std::string, std::string>& window_info() const;

  /// Audio key-value info.
  [[nodiscard]] const std::flat_map<std::string, std::string>& audio_info() const;

  /// Query all system info at once (call after GL context + Audio are ready).
  void query_system_info();

  /// Set a window-related info entry (dimensions, DPI, VSync, mode, ...).
  void set_window_info_entry(const std::string& key, const std::string& value);

  /// Set an audio-related info entry.
  void set_audio_info_entry(const std::string& key, const std::string& value);

  // --- Sprite pipeline ---

  /// Latest sprite queue counters (pushed once per rendered frame).
  void set_sprite_counters(const SpriteCounters& counters);
  [[nodiscard]] const SpriteCounters& sprite_counters() const;

 private:
  Metrics() = default;
  ~Metrics() = default;

  // --- Frame timing ---
  float m_frame_time_ms{0.0F};
  std::uint64_t m_frame_count{0};
  float m_total_elapsed{0.0F};
  float m_frame_time_min{0.0F};
  float m_frame_time_avg{0.0F};
  float m_frame_time_max{0.0F};

  std::array<float, kHistorySize> m_frame_time_history{};
  std::size_t m_history_head{0};
  std::size_t m_history_count{0};

  // --- System info ---
  std::flat_map<std::string, std::string> m_opengl_info;
  std::flat_map<std::string, std::string> m_sdl_info;
  std::flat_map<std::string, std::string> m_window_info;
  std::flat_map<std::string, std::string> m_audio_info;

  // --- Sprite pipeline ---
  SpriteCounters m_sprite_counters;

  bool m_system_info_queried{false};
};

}  // namespace App::Debug
