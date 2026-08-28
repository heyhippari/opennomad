#pragma once

#include <array>
#include <cstdint>
#include <functional>
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
  using AttachmentPoseProvider =
      std::function<std::optional<WorldCameraAttachmentPose>(std::int16_t character_id)>;
  using ControllerPoseProvider = std::function<std::optional<WorldCameraPose>()>;
  WorldCameraSystem() = default;

  void set_aspect_ratio(float aspect_ratio);
  void set_clip_distance_metres(float distance);
  /// Supplies one requested live character attachment pose without coupling
  /// this presentation class to ScenarioManager or a gameplay runtime.
  void set_attachment_pose_provider(AttachmentPoseProvider provider);
  /// Supplies Runtime's live controller source (global 0x009103D4 equivalent).
  /// Controller mode 13 copies this pose every presentation update.
  void set_controller_pose_provider(ControllerPoseProvider provider);
  /// Clears the current scripted/fallback pose while preserving projection
  /// settings such as the current aspect ratio.
  void reset();

  /// Logical ownership release of controller mode 13 (the structured/
  /// cinematic camera owner) when the structured script no longer publishes
  /// a camera. The last valid pose is retained only as a presentation
  /// fallback until the real automatic player camera exists (Phase 4.3);
  /// the logical controller becomes mode 0 / automatic-pending and no longer
  /// claims a scripted source.
  void release_structured_controller();

  /// Installs a diagnostic camera which frames the loaded decor. It is used
  /// only until the first scripted IAM camera is received.
  void set_fallback_pose(const std::array<float, 3>& center, float radius);

  /// Starts/snap-applies one authored camera, or switches the recovered native
  /// camera controller when command.kind == k_controller_mode.
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
  [[nodiscard]] std::optional<std::uint16_t> active_controller_mode() const {
    return m_active_controller_mode;
  }
  [[nodiscard]] bool controller_transitioning() const {
    return m_controller_transition_duration > 0.0F &&
           m_controller_transition_elapsed < m_controller_transition_duration;
  }
  [[nodiscard]] float controller_transition_duration_seconds() const {
    return m_controller_transition_duration;
  }
  /// Returns one exact tracked mode-12 completion once, then clears it.
  [[nodiscard]] std::optional<WorldCameraOperationCompletion> take_completed_operation() {
    const std::optional<WorldCameraOperationCompletion> completed{m_completed_operation};
    m_completed_operation.reset();
    return completed;
  }
  [[nodiscard]] const std::optional<WorldCameraCommand>& last_command() const {
    return m_last_command;
  }
  [[nodiscard]] const Runtime::CameraView& runtime_view() const {
    return m_runtime_view;
  }

 private:
  void apply_controller_mode(const WorldCameraCommand& command);
  void complete_active_operation();
  void commit_pose();
  [[nodiscard]] WorldCameraPose resolve_command_pose(const WorldCameraCommand& command) const;
  [[nodiscard]] Runtime::Vec3 resolve_attachment_point(const WorldCameraCommand& command,
      const std::array<std::int32_t, 3>& serialized,
      std::int16_t selector,
      const Runtime::Vec3& absolute_fallback) const;
  [[nodiscard]] static WorldCameraPose interpolate(
      const WorldCameraPose& from, const WorldCameraPose& to, float amount);

  static constexpr float k_scenario_frames_per_second{30.0F};

  // OpenNomad preserves the retail 4:3-derived vertical FOV on widescreen,
  // allowing horizontal view to expand outside exact retail behavior.
  float m_clip_distance_metres{Runtime::k_default_clip_distance_metres};
  Camera m_camera{Runtime::horizontal_4_3_to_vertical_fov(74.0F),
      1.0F,
      Runtime::k_default_near_inches,
      Runtime::metres_to_inches(m_clip_distance_metres)};
  Runtime::CameraView m_runtime_view{};

  WorldCameraPose m_current{};
  WorldCameraPose m_transition_start{};
  WorldCameraPose m_transition_target{};
  float m_transition_elapsed{0.0F};
  float m_transition_duration{0.0F};
  bool m_has_pose{false};
  bool m_has_scripted_pose{false};
  std::optional<std::uint16_t> m_active_camera_id;
  std::optional<std::uint16_t> m_active_controller_mode;
  float m_controller_transition_elapsed{0.0F};
  float m_controller_transition_duration{0.0F};
  std::optional<WorldCameraOperationCompletion> m_active_operation;
  std::optional<WorldCameraOperationCompletion> m_completed_operation;
  std::optional<WorldCameraCommand> m_last_command;
  AttachmentPoseProvider m_attachment_pose_provider;
  ControllerPoseProvider m_controller_pose_provider;
};

}  // namespace App
