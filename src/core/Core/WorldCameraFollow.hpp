#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "Core/RuntimeMath.hpp"

namespace App {

/// One live attachment source for a camera endpoint: the actor position and
/// Euler angles a Runtime selector resolver writes into the camera object
/// (target +0x64/+0x70, eye +0x98/+0xA4).
struct WorldCameraEndpointSource {
  Runtime::Vec3 position{};
  Runtime::Vec3 angles_degrees{};
};

/// Authored follow parameters of one applied camera (Runtime record
/// +0x2A/+0x2C/+0x2E, IAM AREA tail +0x24/+0x26/+0x28). Frame counts are in
/// 30 Hz units; 1 behaves like 2 and 0 snaps.
struct WorldCameraFollowSettings {
  /// Camera +0x7C: offset subtracted from the rotated target source.
  Runtime::Vec3 target_offset{};
  /// Camera +0xB0: offset subtracted from the rotated eye source.
  Runtime::Vec3 eye_offset{};
  std::int16_t target_position_frames{0};
  std::int16_t eye_position_frames{0};
  std::int16_t eye_angle_frames{0};
};

struct WorldCameraFollowInput {
  /// Resolved target source, or nullopt when the target is absolute or its
  /// resolver failed (the endpoint then keeps its previous value).
  std::optional<WorldCameraEndpointSource> target;
  std::optional<WorldCameraEndpointSource> eye;
  /// Runtime 0x004C30DC: elapsed time in nominal 30 Hz units.
  float delta_units{0.0F};
};

/// Runtime's per-frame camera-object update for scripted and automatic
/// cameras (0x00417CF0, every mode except 13).
///
/// Attached endpoints ease toward their actor-relative positions; the eye
/// also eases its source angles. When the adventure follow setup is active
/// (0x00413C00, eye attached by selector 0 or automatic mode 0), a
/// target-to-eye line of sight pulls the eye in front of occluding decor
/// (0x00417070) and lifts both endpoints toward head height as it closes in.
class WorldCameraFollow {
 public:
  /// Finds the nearest decor crossing of a segment (Runtime 0x00444810).
  using SegmentQuery =
      std::function<std::optional<Runtime::Vec3>(const Runtime::Vec3&, const Runtime::Vec3&)>;

  /// Runtime 0x00414A90 adventure line-of-sight state (camera +0xD0).
  enum class Occlusion : std::uint8_t {
    k_clear = 0,
    k_pulled_in = 1,
    k_recovering = 2,
  };

  /// Ray length relative to the target-eye distance (camera +0x12C).
  static constexpr float K_RAY_SCALE{1.2F};
  /// Distance easing frames when the eye moves back out (camera +0x140).
  static constexpr float K_DISTANCE_FRAMES{8.0F};
  /// Height easing frames while occluded (camera +0x144).
  static constexpr float K_HEIGHT_FRAMES{4.0F};
  /// Head height as a fraction of body bottom above the origin (0x004BC0E4).
  static constexpr float K_HEAD_HEIGHT_SCALE{-0.7F};
  /// Runtime clamps its dynamic delta to three 30 Hz units (0x004BC1F4).
  static constexpr float K_MAXIMUM_DELTA_UNITS{3.0F};

  /// Applies a camera (0x004146C0/0x00414520): installs the absolute starting
  /// endpoints and parameters, clears follow state, and arms the one-frame
  /// snap (flag 0x1) so the first update lands exactly on its sources.
  void reset(const Runtime::Vec3& eye,
      const Runtime::Vec3& target,
      const WorldCameraFollowSettings& settings);

  /// Runtime 0x00413C00 adventure follow setup, from the eye actor's current
  /// Y and body bottom (+0xF8/+0x114).
  void enable_adventure(float eye_actor_y, float body_bottom);

  void update(const WorldCameraFollowInput& input, const SegmentQuery& segment_query);

  [[nodiscard]] const Runtime::Vec3& eye() const {
    return m_eye;
  }
  [[nodiscard]] const Runtime::Vec3& target() const {
    return m_target;
  }
  [[nodiscard]] bool adventure() const {
    return m_adventure;
  }
  [[nodiscard]] Occlusion occlusion() const {
    return m_occlusion;
  }
  [[nodiscard]] float occluded_distance() const {
    return m_occluded_distance;
  }

  /// Rotated attachment point: `source - transform(offset, Rx*Ry*Rz(angles))`
  /// (0x00441EB0/0x00442D70).
  [[nodiscard]] static Runtime::Vec3 attached_point(const Runtime::Vec3& source,
      const Runtime::Vec3& angles_degrees,
      const Runtime::Vec3& offset);

 private:
  void follow_target(const WorldCameraEndpointSource& source, float delta_units);
  void follow_eye(const WorldCameraEndpointSource& source, float delta_units);
  void resolve_occlusion(float delta_units, const SegmentQuery& segment_query);

  WorldCameraFollowSettings m_settings{};
  /// Committed endpoints (camera +0x14/+0x20).
  Runtime::Vec3 m_eye{};
  Runtime::Vec3 m_target{};
  /// Working endpoints of the current update (camera +0x34/+0x40).
  Runtime::Vec3 m_eye_out{};
  Runtime::Vec3 m_target_out{};
  /// Eased eye source angles (camera +0x4C).
  Runtime::Vec3 m_eye_angles{};
  /// Last resolved sources (camera +0x64/+0x70 and +0x98/+0xA4).
  std::optional<WorldCameraEndpointSource> m_target_source;
  std::optional<WorldCameraEndpointSource> m_eye_source;
  bool m_snap{true};

  bool m_adventure{false};
  Occlusion m_occlusion{Occlusion::k_clear};
  /// Camera +0x148: eye distance held while occluded.
  float m_occluded_distance{0.0F};
  /// Camera +0x138/+0x13C: eye and target head offsets from the actor Y.
  float m_head_offset{0.0F};
  /// Camera +0x154/+0x158: eye actor Y this frame and last frame.
  float m_actor_y{0.0F};
  float m_previous_actor_y{0.0F};
};

}  // namespace App
