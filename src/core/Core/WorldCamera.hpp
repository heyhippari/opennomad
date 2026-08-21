#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "Core/Camera.hpp"
#include "Core/WorldPresentation.hpp"

namespace App {

struct WorldCameraPose {
  std::array<float, 3> eye{};
  std::array<float, 3> target{};
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

  /// Runtime logical XYZ -> OpenNomad renderer coordinates.
  ///
  /// Runtime world coordinates share the basis used by 3DO object placement.
  /// OpenNomad's presentation basis is a 180-degree X rotation followed by
  /// the 1/40 world scale: (x, y, z) -> (x, -y, -z) * 0.025. The implementation
  /// lives here so AREA/scenario code remains renderer-independent.
  [[nodiscard]] static std::array<float, 3> runtime_to_renderer(
      const std::array<std::int32_t, 3>& value);

 private:
  void commit_pose();
  [[nodiscard]] static WorldCameraPose interpolate(
      const WorldCameraPose& from, const WorldCameraPose& to, float amount);

  static constexpr float k_runtime_units_to_world{0.025F};
  static constexpr float k_scenario_frames_per_second{30.0F};

  // Projection metadata (+0x1E in the IAM camera record) is preserved in
  // last_command(), but the exact Runtime focal->FOV formula is not yet
  // recovered. Keep one explicit provisional projection here rather than
  // smearing guesses throughout the renderer.
  Camera m_camera{60.0F, 1.0F, 0.05F, 5000.0F};

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