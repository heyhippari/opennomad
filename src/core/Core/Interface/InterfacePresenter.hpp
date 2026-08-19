#pragma once

#include "Core/Input/InputManager.hpp"

namespace App::Interface {

class InterfaceManager;

/// Presentation component for the generic I2D interface subsystem. It is NOT
/// a Scene: it has no simulation role and no knowledge of any specific
/// interface (29, 35, ...). It simply forwards update and render to the
/// InterfaceManager, which owns residency, focus and the I2D state graphs.
class InterfacePresenter {
 public:
  explicit InterfacePresenter(InterfaceManager& manager);

  InterfacePresenter(const InterfacePresenter&) = delete;
  InterfacePresenter(InterfacePresenter&&) = delete;
  InterfacePresenter& operator=(const InterfacePresenter&) = delete;
  InterfacePresenter& operator=(InterfacePresenter&&) = delete;

  /// Advances the interface presentation state and handles navigation.
  void update(float delta_seconds, const Input::InputManager& input);

  /// Renders the presentable resident interfaces.
  void render(int pixel_width, int pixel_height);

 private:
  InterfaceManager* m_manager{nullptr};
};

}  // namespace App::Interface
