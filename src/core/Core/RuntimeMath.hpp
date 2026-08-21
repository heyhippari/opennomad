#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace App::Runtime {

/// Native Runtime.exe vector: +X right, +Y down, +Z forward, in inches.
struct Vec3 {
  float x{0.0F};
  float y{0.0F};
  float z{0.0F};
};

/// Runtime quaternion serialization order used by 3DA and 3DP: w, x, y, z.
struct Quaternion {
  float w{1.0F};
  float x{0.0F};
  float y{0.0F};
  float z{0.0F};
};

/// Runtime.exe row-major 3x3 matrix used with row vectors (`v' = v * M`).
struct Matrix3 {
  std::array<float, 9> values{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};

  [[nodiscard]] constexpr float at(const std::size_t row, const std::size_t column) const {
    return values.at((row * 3U) + column);
  }

  [[nodiscard]] constexpr float& at(const std::size_t row, const std::size_t column) {
    return values.at((row * 3U) + column);
  }

  [[nodiscard]] static constexpr Matrix3 identity() {
    return {};
  }
};

/// Runtime affine transform. Scale multiplies the corresponding matrix rows
/// before a point is transformed, matching Runtime's object transform path.
struct Transform {
  Matrix3 matrix{};
  Vec3 translation{};
  Vec3 scale{1.0F, 1.0F, 1.0F};
};

struct CameraView {
  Transform world_to_camera{};
  float yaw_radians{0.0F};
  float pitch_radians{0.0F};
  float roll_radians{0.0F};
};

inline constexpr double k_inches_per_metre{39.37007874015748};
inline constexpr float k_centimetres_to_inches{0.393700778F};
inline constexpr float k_default_near_inches{2.0F};
inline constexpr float k_default_clip_distance_metres{50.0F};

/// Converts one confirmed positional AREA integer through Runtime's x87/_ftol
/// path. Conversion truncates toward zero; it must not be rounded or floored.
[[nodiscard]] std::int32_t area_position_to_inches(std::int32_t serialized);
[[nodiscard]] Vec3 area_position_to_inches(const std::array<std::int32_t, 3>& serialized);

/// Converts one signed 16-bit AREA angle to Runtime integer degrees.
[[nodiscard]] std::int32_t area_angle_to_degrees(std::int16_t units);

[[nodiscard]] constexpr float metres_to_inches(const float metres) {
  return metres * static_cast<float>(k_inches_per_metre);
}

[[nodiscard]] constexpr float inches_to_metres(const float inches) {
  return inches * 0.0254F;
}

[[nodiscard]] Matrix3 multiply(const Matrix3& first, const Matrix3& second);
[[nodiscard]] Vec3 transform_vector(const Vec3& vector, const Matrix3& matrix);
[[nodiscard]] Vec3 transform_point(const Vec3& point, const Transform& transform);
[[nodiscard]] Transform compose(const Transform& local, const Transform& parent);

/// Runtime's body-animation placement formula: sampled - authored * -0.393700778.
[[nodiscard]] Vec3 relative_body_animation_anchor(
    const Vec3& sampled_path_coordinate, const Vec3& authored_argument);

[[nodiscard]] Matrix3 rotation_x(float radians);
[[nodiscard]] Matrix3 rotation_y(float radians);
[[nodiscard]] Matrix3 rotation_z(float radians);

/// Converts a Runtime-native wxyz quaternion to the row-vector 3x3 matrix
/// used by Runtime objects.
[[nodiscard]] Matrix3 quaternion_matrix(const Quaternion& quaternion);

/// Runtime Euler builder: `Ry(y) * Rx(x) * Rz(z)`. With row vectors this
/// applies Y, then X, then Z.
[[nodiscard]] Matrix3 euler_rotation(float x_radians, float y_radians, float z_radians);

/// Builds Runtime's recovered world-to-camera affine transform, including
/// roll. Camera-space positive Z is in front of the camera.
[[nodiscard]] CameraView camera_view(const Vec3& eye, const Vec3& target, float roll_radians);

/// Converts Runtime's serialized horizontal 4:3 FOV to the vertical FOV used
/// by OpenGL projection. OpenNomad keeps this vertical FOV on widescreen so
/// the horizontal view expands beyond the retail 4:3 viewport.
[[nodiscard]] float horizontal_4_3_to_vertical_fov(float horizontal_fov_degrees);

}  // namespace App::Runtime
