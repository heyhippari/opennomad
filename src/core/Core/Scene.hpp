#pragma once

#include "Core/Input/InputManager.hpp"

namespace App {

/// Interface for a game state: updated once per frame and rendered between
/// the framebuffer clear and the UI pass.
class Scene {
 public:
  Scene() = default;
  virtual ~Scene() = default;

  Scene(const Scene&) = delete;
  Scene(Scene&&) = delete;
  Scene& operator=(Scene other) = delete;
  Scene& operator=(Scene&& other) = delete;

  /// Advance the scene state by delta_time seconds. `input` carries the
  /// frame's resolved action values (see Input::InputManager).
  virtual void update(float delta_time, const Input::InputManager& input) = 0;

  /// Draw the scene; the GL context is current and the framebuffer cleared.
  virtual void render() = 0;

  /// Called by the application when the drawable size changes.
  virtual void resize(int /*width*/, int /*height*/) {}
};

}  // namespace App
