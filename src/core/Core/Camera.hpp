#pragma once

#include <span>

namespace App {

/// Perspective camera producing view and projection matrices.
///
/// Uses the GLM library internally; callers retrieve raw float arrays for
/// passing to shader uniforms.
class Camera {
 public:
  Camera(float fov_degrees, float aspect_ratio, float near_plane, float far_plane);
  ~Camera() = default;

  Camera(const Camera&) = delete;
  Camera(Camera&&) = delete;
  Camera& operator=(Camera other) = delete;
  Camera& operator=(Camera&& other) = delete;

  /// Recompute the projection matrix (call after a viewport resize).
  void set_aspect_ratio(float aspect_ratio);
  void set_perspective(float vertical_fov_degrees, float near_plane, float far_plane);

  /// Retrieve matrices as 16-float column-major spans suitable for glUniformMatrix4fv.
  [[nodiscard]] std::span<const float, 16> get_view_matrix() const;
  [[nodiscard]] std::span<const float, 16> get_projection_matrix() const;

  /// Retrieves the camera position (eye) in world space.
  [[nodiscard]] std::span<const float, 3> get_position() const;

  /// Retrieves the camera orientation in degrees.
  [[nodiscard]] float get_yaw() const;
  [[nodiscard]] float get_pitch() const;

  /// Depth-test bounds of the projection.
  [[nodiscard]] float get_near_plane() const;
  [[nodiscard]] float get_far_plane() const;

  // --- Transform setters ---
  void set_position(float x, float y, float z);
  void set_rotation(float yaw_degrees, float pitch_degrees);

  /// Installs an explicit GL view matrix and corresponding GL-world eye.
  /// Runtime-scripted cameras use this to preserve their recovered roll and
  /// row-vector matrix convention without passing through glm::lookAt.
  void set_view_matrix(std::span<const float, 16> view_matrix, std::span<const float, 3> position);

  /// Points the camera from its current position at the given world-space target.
  void look_at(float target_x, float target_y, float target_z);

 private:
  void update_view_matrix();

  float m_fov{60.0F};
  float m_aspect_ratio{1.0F};
  float m_near{0.1F};
  float m_far{1000.0F};

  float m_position[3]{0.0F, 0.0F, 3.0F};
  float m_yaw{0.0F};
  float m_pitch{0.0F};

  // Column-major 4×4 matrices stored as flat arrays.
  float m_view_matrix[16]{};
  float m_projection_matrix[16]{};

  bool m_view_dirty{true};
};

}  // namespace App
