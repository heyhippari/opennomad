#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

#include "Core/RuntimeMath.hpp"

namespace App {

/// Session-lifetime copy of Runtime's global cyclic 3DO texture phases.
///
/// Runtime advances both globals by 0.0004 per nominal 30 Hz tick. Expressing
/// the same rate in seconds keeps presentation deterministic and independent
/// of display refresh. WorldScene owns this state so decor renderer rebuilds
/// do not restart the phases.
class WorldUvPhaseState {
 public:
  static constexpr double k_periods_per_second{0.012};

  void update(const float delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0F) {
      return;
    }
    const double increment{static_cast<double>(delta_seconds) * k_periods_per_second};
    m_u_phase = std::fmod(m_u_phase + increment, 1.0);
    m_v_phase = std::fmod(m_v_phase + increment, 1.0);
  }

  [[nodiscard]] double u_phase() const {
    return m_u_phase;
  }

  [[nodiscard]] double v_phase() const {
    return m_v_phase;
  }

 private:
  double m_u_phase{0.0};
  double m_v_phase{0.0};
};

/// One live Runtime actor attachment pose supplied across the narrow
/// gameplay-to-presentation capability boundary.
struct WorldCameraAttachmentPose {
  Runtime::Vec3 translation{};
  Runtime::Matrix3 orientation{};
};

/// One Runtime AREA camera command waiting to be consumed by the presentation
/// layer.
///
/// Serialized AREA integers are retained for diagnostics alongside normalized
/// Runtime-native positions. Scenario code never depends on GL conventions.
struct WorldCameraCommand {
  std::uint32_t scene_id{0};
  std::uint32_t scene_generation{0};
  std::uint16_t camera_id{0};

  std::array<std::int32_t, 3> serialized_eye{};
  std::array<std::int32_t, 3> serialized_target{};
  /// Absolute fallback vectors. Actor-attached vectors are re-resolved from
  /// serialized data every presentation update, never baked at command issue.
  Runtime::Vec3 runtime_eye{};
  Runtime::Vec3 runtime_target{};

  /// Original AREA duration in 30 Hz scenario units.
  std::int16_t duration_units{0};
  std::int16_t flags{0};
  bool wait_for_completion{false};

  /// Confirmed camera metadata, preserving both serialized units and
  /// normalized integer degrees.
  std::uint16_t camera_type{0};
  std::int16_t roll_units{0};
  std::int16_t horizontal_fov_units{0};
  std::int32_t roll_degrees{0};
  std::int32_t horizontal_fov_degrees{0};
  /// IAM +0x20 / +0x22. -1 is absolute, 0 is the current actor. Other
  /// selectors stay explicitly unsupported and use the safe absolute fallback.
  std::int16_t target_attachment_selector{-1};
  std::int16_t eye_attachment_selector{-1};
  std::array<std::uint16_t, 4> tail_fields{};
};

/// One AREA 0x76/0x77 presentation command resolved against the active world.
///
/// Mode 1 (0x76) fades into the authored RGB colour; mode 2 (0x77) fades out
/// of it. Durations use the 30 Hz AREA clock while interpolation is sampled at
/// display rate.
struct WorldFadeCommand {
  std::uint32_t scene_id{0};
  std::uint32_t scene_generation{0};
  std::uint8_t mode{0};
  std::uint32_t color{0};
  std::int16_t duration_units{0};
  std::int16_t operand_c{0};
};

/// CPU-only state for asynchronous AREA 0x76/0x77 presentation fades.
class WorldFadeState {
 public:
  static constexpr float k_frames_per_second{30.0F};

  /// Applies only a supported command belonging to the presented world.
  [[nodiscard]] bool apply_command(
      const WorldFadeCommand& command, std::uint32_t scene_id, std::uint32_t generation) {
    if (command.scene_id != scene_id || command.scene_generation != generation ||
        (command.mode != 1U && command.mode != 2U)) {
      return false;
    }

    m_mode = command.mode;
    m_color = command.color & 0x00FFFFFFU;
    m_duration_seconds = std::abs(static_cast<float>(command.duration_units)) / k_frames_per_second;
    m_elapsed_seconds = 0.0F;
    m_alpha = command.mode == 1U ? 0.0F : 1.0F;
    if (m_duration_seconds <= 0.0F) {
      m_alpha = command.mode == 1U ? 1.0F : 0.0F;
    }
    return true;
  }

  void update(const float delta_time) {
    if (m_duration_seconds <= 0.0F || m_elapsed_seconds >= m_duration_seconds) {
      return;
    }
    m_elapsed_seconds += std::max(delta_time, 0.0F);
    const float progress{std::clamp(m_elapsed_seconds / m_duration_seconds, 0.0F, 1.0F)};
    m_alpha = m_mode == 1U ? progress : 1.0F - progress;
  }

  void reset() {
    m_mode = 0;
    m_color = 0;
    m_alpha = 0.0F;
    m_elapsed_seconds = 0.0F;
    m_duration_seconds = 0.0F;
  }

  [[nodiscard]] std::uint8_t mode() const {
    return m_mode;
  }
  [[nodiscard]] std::uint32_t color() const {
    return m_color;
  }
  [[nodiscard]] float alpha() const {
    return m_alpha;
  }
  [[nodiscard]] float elapsed_seconds() const {
    return m_elapsed_seconds;
  }
  [[nodiscard]] float duration_seconds() const {
    return m_duration_seconds;
  }
  [[nodiscard]] bool transitioning() const {
    return m_duration_seconds > 0.0F && m_elapsed_seconds < m_duration_seconds;
  }

 private:
  std::uint8_t m_mode{0};
  std::uint32_t m_color{0};
  float m_alpha{0.0F};
  float m_elapsed_seconds{0.0F};
  float m_duration_seconds{0.0F};
};

/// One AREA 0x84/0x85 cinematic-mask request resolved against the active world.
struct WorldLetterboxCommand {
  std::uint32_t scene_id{0};
  std::uint32_t scene_generation{0};
  bool enabled{false};
};

/// CPU-only state for OpenNomad's cinematic top/bottom presentation mask.
///
/// Runtime confirms the transition endpoints and its 60-unit duration. The
/// linear interpolation below is provisional until Runtime's exact curve is
/// recovered. Pixel geometry is deliberately derived from the live viewport.
class WorldLetterboxState {
 public:
  static constexpr float k_target_aspect{1.85F};
  static constexpr float k_transition_duration_seconds{2.0F};
  static constexpr std::uint32_t k_transition_runtime_units{60U};

  /// Returns one full-strength bar height for OpenNomad's modernized 1.85:1
  /// target. Viewports already at or wider than 1.85:1 are not pillarboxed.
  [[nodiscard]] static float target_bar_height(float width, float height) {
    if (width <= 0.0F || height <= 0.0F) {
      return 0.0F;
    }
    return std::max(0.0F, 0.5F * (height - (width / k_target_aspect)));
  }

  [[nodiscard]] float current_bar_height(float width, float height) const {
    return target_bar_height(width, height) * m_amount;
  }

  /// Applies only a command belonging to the currently presented world.
  [[nodiscard]] bool apply_command(
      const WorldLetterboxCommand& command, std::uint32_t scene_id, std::uint32_t generation) {
    if (command.scene_id != scene_id || command.scene_generation != generation) {
      return false;
    }
    set_enabled(command.enabled);
    return true;
  }

  void set_enabled(bool enabled) {
    m_requested = enabled;
    m_start_amount = m_amount;
    m_target_amount = enabled ? 1.0F : 0.0F;
    m_elapsed = 0.0F;
  }

  void update(float delta_time) {
    if (!transitioning()) {
      return;
    }
    m_elapsed += std::max(delta_time, 0.0F);
    const float progress{std::clamp(m_elapsed / k_transition_duration_seconds, 0.0F, 1.0F)};
    // Provisional linear curve; Runtime duration and endpoints are confirmed.
    m_amount = m_start_amount + ((m_target_amount - m_start_amount) * progress);
  }

  void reset() {
    m_amount = 0.0F;
    m_start_amount = 0.0F;
    m_target_amount = 0.0F;
    m_elapsed = 0.0F;
    m_requested = false;
  }

  [[nodiscard]] float amount() const {
    return m_amount;
  }

  [[nodiscard]] bool requested() const {
    return m_requested;
  }

  [[nodiscard]] bool transitioning() const {
    return m_amount != m_target_amount;
  }

 private:
  float m_amount{0.0F};
  float m_start_amount{0.0F};
  float m_target_amount{0.0F};
  float m_elapsed{0.0F};
  bool m_requested{false};
};

/// CPU-only mailbox from scenario execution to WorldScene.
///
/// ScenarioManager owns this because AREA execution can emit presentation
/// commands before or after a WorldScene frame. It intentionally contains no
/// Camera, matrices or GL objects; WorldScene drains the commands and owns the
/// actual presentation state.
class WorldPresentationState {
 public:
  void enqueue_camera(WorldCameraCommand command) {
    m_camera_commands.push_back(std::move(command));
  }

  void enqueue_fade(WorldFadeCommand command) {
    m_fade_commands.push_back(std::move(command));
  }

  void enqueue_letterbox(WorldLetterboxCommand command) {
    m_letterbox_commands.push_back(std::move(command));
  }

  [[nodiscard]] std::optional<WorldCameraCommand> take_camera() {
    if (m_camera_commands.empty()) {
      return std::nullopt;
    }
    WorldCameraCommand command{std::move(m_camera_commands.front())};
    m_camera_commands.pop_front();
    return command;
  }

  [[nodiscard]] std::optional<WorldFadeCommand> take_fade() {
    if (m_fade_commands.empty()) {
      return std::nullopt;
    }
    WorldFadeCommand command{std::move(m_fade_commands.front())};
    m_fade_commands.pop_front();
    return command;
  }

  [[nodiscard]] std::optional<WorldLetterboxCommand> take_letterbox() {
    if (m_letterbox_commands.empty()) {
      return std::nullopt;
    }
    WorldLetterboxCommand command{std::move(m_letterbox_commands.front())};
    m_letterbox_commands.pop_front();
    return command;
  }

  [[nodiscard]] std::size_t pending_camera_count() const {
    return m_camera_commands.size();
  }

  [[nodiscard]] std::size_t pending_fade_count() const {
    return m_fade_commands.size();
  }

  [[nodiscard]] std::size_t pending_letterbox_count() const {
    return m_letterbox_commands.size();
  }

  void clear() {
    m_camera_commands.clear();
    m_fade_commands.clear();
    m_letterbox_commands.clear();
  }

 private:
  std::deque<WorldCameraCommand> m_camera_commands;
  std::deque<WorldFadeCommand> m_fade_commands;
  std::deque<WorldLetterboxCommand> m_letterbox_commands;
};

}  // namespace App
