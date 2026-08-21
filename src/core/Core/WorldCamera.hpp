#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "Core/Camera.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/WorldPresentation.hpp"

namespace App {

struct WorldCameraPose {
  /// Runtime-native XYZ inches; never GL presentation coordinates.
  Runtime::Vec3 eye{};
  Runtime::Vec3 target{};
  float roll_degrees{0.0F};
  float horizontal_fov_degrees{74.0F};
};

/// Presentation-side camera driven by Runtime AREA camera commands.
///
/// AREA opcodes retain their original 30 Hz timing, while this class samples
/// the transition every display frame. That preserves Runtime timing without
/// forcing visible camera motion to 30 Hz.
class WorldCameraSystem {
 public:
  WorldCameraSystem() = default;

  void set_aspect_ratio(float aspect_ratio);

  /// Clears the current scripted/fallback pose while preserving projection
  /// settings such as the current aspect ratio.
  void reset();

  /// Installs a diagnostic camera which frames the loaded decor. It is used
  /// only until the first scripted IAM camera is received.
  void set_fallback_pose(const std::array<float, 3>& center, float radius);

  /// Starts/snap-applies one resolved AREA camera command.
  void apply_command(const WorldCameraCommand& command);

  /// Advances an active interpolation using real display-frame seconds.
  void update(float delta_seconds);

  [[nodiscard]] Camera& camera() {
    return m_camera;
  }
  [[nodiscard]] const Camera& camera() const {
    return m_camera;
  }

  [[nodiscard]] const WorldCameraPose& pose() const {
    return m_current;
  }
  [[nodiscard]] bool has_pose() const {
    return m_has_pose;
  }
  [[nodiscard]] bool has_scripted_pose() const {
    return m_has_scripted_pose;
  }
  [[nodiscard]] bool transitioning() const {
    return m_transition_duration > 0.0F;
  }
  [[nodiscard]] std::optional<std::uint16_t> active_camera_id() const {
    return m_active_camera_id;
  }
  [[nodiscard]] const std::optional<WorldCameraCommand>& last_command() const {
    return m_last_command;
  }
  [[nodiscard]] const Runtime::CameraView& runtime_view() const {
    return m_runtime_view;
  }

 private:
  void commit_pose();
  [[nodiscard]] static WorldCameraPose interpolate(
      const WorldCameraPose& from, const WorldCameraPose& to, float amount);

  static constexpr float k_scenario_frames_per_second{30.0F};

  // OpenNomad preserves the retail 4:3-derived vertical FOV on widescreen,
  // allowing horizontal view to expand outside exact retail behavior.
  Camera m_camera{Runtime::horizontal_4_3_to_vertical_fov(74.0F),
      1.0F,
      Runtime::k_default_near_inches,
      Runtime::metres_to_inches(Runtime::k_default_clip_distance_metres)};
  Runtime::CameraView m_runtime_view{};

  WorldCameraPose m_current{};
  WorldCameraPose m_transition_start{};
  WorldCameraPose m_transition_target{};
  float m_transition_elapsed{0.0F};
  float m_transition_duration{0.0F};
  bool m_has_pose{false};
  bool m_has_scripted_pose{false};
  std::optional<std::uint16_t> m_active_camera_id;
  std::optional<WorldCameraCommand> m_last_command;
};

}  // namespace App
