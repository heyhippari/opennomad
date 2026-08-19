#pragma once

#include "Core/Camera.hpp"
#include "Core/Input/InputManager.hpp"

namespace App {

/// Free-fly first-person camera: WASD + Space/LeftShift movement and mouse
/// look, driven entirely through the input system's action values.
///
/// Movement follows the camera's look direction (pitch included), strafes
/// along the screen-right axis and rises along world up. Pitch is clamped to
/// k_max_pitch_degrees to keep the view from flipping over vertical.
class CameraController {
 public:
  explicit CameraController(Camera& camera);

  CameraController(const CameraController&) = delete;
  CameraController(CameraController&&) = delete;
  CameraController& operator=(const CameraController&) = delete;
  CameraController& operator=(CameraController&&) = delete;
  ~CameraController() = default;

  /// Applies this frame's look and movement actions to the camera.
  void update(const App::Input::InputManager& input, float delta_time);

  void set_move_speed(float units_per_second);
  void set_look_sensitivity(float degrees_per_pixel);
  [[nodiscard]] float get_move_speed() const;
  [[nodiscard]] float get_look_sensitivity() const;

 private:
  /// Maximum pitch in degrees; keeps the view from flipping past vertical.
  static constexpr float k_max_pitch_degrees{89.0F};

  Camera& m_camera;
  float m_move_speed{5.0F};
  float m_look_sensitivity{0.15F};
};

}  // namespace App
