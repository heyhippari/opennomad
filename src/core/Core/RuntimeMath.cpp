#include "Core/RuntimeMath.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace App::Runtime {

std::int32_t area_position_to_inches(const std::int32_t serialized) {
  const double converted{(static_cast<double>(serialized) * k_inches_per_metre / 256.0) - 1.0};
  return static_cast<std::int32_t>(std::trunc(converted));
}

Vec3 area_position_to_inches(const std::array<std::int32_t, 3>& serialized) {
  return Vec3{.x = static_cast<float>(area_position_to_inches(serialized.at(0))),
      .y = static_cast<float>(area_position_to_inches(serialized.at(1))),
      .z = static_cast<float>(area_position_to_inches(serialized.at(2)))};
}

std::int32_t area_angle_to_degrees(const std::int16_t units) {
  const double degrees{static_cast<double>(units) * 360.0 / 4096.0};
  return static_cast<std::int32_t>(std::trunc(degrees));
}

Matrix3 multiply(const Matrix3& first, const Matrix3& second) {
  Matrix3 result{};
  for (std::size_t row{0}; row < 3U; ++row) {
    for (std::size_t column{0}; column < 3U; ++column) {
      result.at(row, column) = (first.at(row, 0) * second.at(0, column)) +
                               (first.at(row, 1) * second.at(1, column)) +
                               (first.at(row, 2) * second.at(2, column));
    }
  }
  return result;
}

Vec3 transform_vector(const Vec3& vector, const Matrix3& matrix) {
  return Vec3{.x = (vector.x * matrix.at(0, 0)) + (vector.y * matrix.at(1, 0)) +
                   (vector.z * matrix.at(2, 0)),
      .y = (vector.x * matrix.at(0, 1)) + (vector.y * matrix.at(1, 1)) +
           (vector.z * matrix.at(2, 1)),
      .z = (vector.x * matrix.at(0, 2)) + (vector.y * matrix.at(1, 2)) +
           (vector.z * matrix.at(2, 2))};
}

Vec3 transform_point(const Vec3& point, const Transform& transform) {
  // Runtime multiplies matrix row 0/1/2 by scale X/Y/Z respectively. For a
  // row vector this is equivalent to scaling the input components first.
  const Vec3 scaled{.x = point.x * transform.scale.x,
      .y = point.y * transform.scale.y,
      .z = point.z * transform.scale.z};
  const Vec3 rotated{transform_vector(scaled, transform.matrix)};
  return Vec3{.x = rotated.x + transform.translation.x,
      .y = rotated.y + transform.translation.y,
      .z = rotated.z + transform.translation.z};
}

Transform compose(const Transform& local, const Transform& parent) {
  const Vec3 translated{transform_vector(local.translation, parent.matrix)};
  return Transform{.matrix = multiply(local.matrix, parent.matrix),
      .translation = Vec3{.x = translated.x + parent.translation.x,
          .y = translated.y + parent.translation.y,
          .z = translated.z + parent.translation.z},
      .scale = local.scale};
}

Vec3 relative_body_animation_anchor(
    const Vec3& sampled_path_coordinate, const Vec3& authored_argument) {
  return Vec3{.x = sampled_path_coordinate.x - (authored_argument.x * -k_centimetres_to_inches),
      .y = sampled_path_coordinate.y - (authored_argument.y * -k_centimetres_to_inches),
      .z = sampled_path_coordinate.z - (authored_argument.z * -k_centimetres_to_inches)};
}

Matrix3 rotation_x(const float radians) {
  const float cosine{std::cos(radians)};
  const float sine{std::sin(radians)};
  return Matrix3{{1.0F, 0.0F, 0.0F, 0.0F, cosine, -sine, 0.0F, sine, cosine}};
}

Matrix3 rotation_y(const float radians) {
  const float cosine{std::cos(radians)};
  const float sine{std::sin(radians)};
  return Matrix3{{cosine, 0.0F, sine, 0.0F, 1.0F, 0.0F, -sine, 0.0F, cosine}};
}

Matrix3 rotation_z(const float radians) {
  const float cosine{std::cos(radians)};
  const float sine{std::sin(radians)};
  return Matrix3{{cosine, -sine, 0.0F, sine, cosine, 0.0F, 0.0F, 0.0F, 1.0F}};
}

Matrix3 quaternion_matrix(const Quaternion& quaternion) {
  // Runtime.exe 0x00442A00 consumes the authored wxyz components directly.
  // Do not normalize or transpose this matrix to adapt conventions: despite
  // Runtime's row-vector object math, these are the coefficients the retail
  // routine writes into object animation state and the hierarchy consumes them
  // as-is.
  const float w_value{quaternion.w};
  const float x_value{quaternion.x};
  const float y_value{quaternion.y};
  const float z_value{quaternion.z};
  return Matrix3{{1.0F - (2.0F * ((y_value * y_value) + (z_value * z_value))),
      2.0F * ((x_value * y_value) - (w_value * z_value)),
      2.0F * ((x_value * z_value) + (w_value * y_value)),
      2.0F * ((x_value * y_value) + (w_value * z_value)),
      1.0F - (2.0F * ((x_value * x_value) + (z_value * z_value))),
      2.0F * ((y_value * z_value) - (w_value * x_value)),
      2.0F * ((x_value * z_value) - (w_value * y_value)),
      2.0F * ((y_value * z_value) + (w_value * x_value)),
      1.0F - (2.0F * ((x_value * x_value) + (y_value * y_value)))}};
}

Matrix3 euler_rotation(const float x_radians, const float y_radians, const float z_radians) {
  return multiply(multiply(rotation_y(y_radians), rotation_x(x_radians)), rotation_z(z_radians));
}

CameraView camera_view(const Vec3& eye, const Vec3& target, const float roll_radians) {
  const float delta_x{target.x - eye.x};
  const float delta_y{target.y - eye.y};
  const float delta_z{target.z - eye.z};
  const float horizontal{std::sqrt((delta_x * delta_x) + (delta_z * delta_z))};
  const float yaw{std::atan2(delta_x, delta_z)};
  const float pitch{-std::atan2(delta_y, horizontal)};
  const Matrix3 matrix{euler_rotation(pitch, yaw, roll_radians)};

  const Vec3 translation{
      .x = -((eye.x * matrix.at(0, 0)) + (eye.y * matrix.at(1, 0)) + (eye.z * matrix.at(2, 0))),
      .y = -((eye.x * matrix.at(0, 1)) + (eye.y * matrix.at(1, 1)) + (eye.z * matrix.at(2, 1))),
      .z = -((eye.x * matrix.at(0, 2)) + (eye.y * matrix.at(1, 2)) + (eye.z * matrix.at(2, 2)))};

  return CameraView{.world_to_camera = Transform{.matrix = matrix, .translation = translation},
      .yaw_radians = yaw,
      .pitch_radians = pitch,
      .roll_radians = roll_radians};
}

float horizontal_4_3_to_vertical_fov(const float horizontal_fov_degrees) {
  constexpr float k_degrees_to_radians{std::numbers::pi_v<float> / 180.0F};
  constexpr float k_radians_to_degrees{180.0F / std::numbers::pi_v<float>};
  constexpr float k_retail_aspect{4.0F / 3.0F};
  const float half_horizontal{horizontal_fov_degrees * k_degrees_to_radians * 0.5F};
  return 2.0F * std::atan(std::tan(half_horizontal) / k_retail_aspect) * k_radians_to_degrees;
}

}  // namespace App::Runtime
