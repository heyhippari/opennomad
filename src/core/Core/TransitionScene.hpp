#pragma once

#include <memory>

#include "Core/Scene.hpp"

namespace App {

/// Clearly documented transitional scene installed after interface 29
/// completes and the resumed AREA script requests track 87. It does not
/// render the Kay'l portal/tunnel introduction (a later milestone); it only
/// clears the framebuffer so the closed main menu is no longer drawn.
/// GRID world context 0 remains resident in ScenarioManager.
class TransitionScene final : public Scene {
 public:
  /// Creates the transitional scene. The constructor is private; only the
  /// factory may build a scene.
  static std::unique_ptr<TransitionScene> create();

  ~TransitionScene() override = default;

  TransitionScene(const TransitionScene&) = delete;
  TransitionScene(TransitionScene&&) = delete;
  TransitionScene& operator=(const TransitionScene&) = delete;
  TransitionScene& operator=(TransitionScene&&) = delete;

  void update(float delta_time, const Input::InputManager& input) override;
  void render() override;

 private:
  TransitionScene() = default;

  /// True once the transitional log message has been emitted.
  bool m_logged{false};
};

}  // namespace App
