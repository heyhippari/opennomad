#pragma once

#include <cstddef>
#include <string_view>

#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Interface/RuntimeText.hpp"

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

  /// Advances interface animation while another presentation layer owns input.
  void update_without_input(float delta_seconds);

  /// Renders the presentable resident interfaces.
  void render(int pixel_width, int pixel_height);

  /// Renders the gameplay dialog layer after ordinary interfaces.
  [[nodiscard]] float render_dialog(const Dialog::DialogPresentation& dialog,
      std::size_t selected_choice,
      float scroll_offset,
      int pixel_width,
      int pixel_height);

  void render_world_text(const RuntimeTextDocument& document,
      std::uint64_t presentation_time_ms,
      int pixel_width,
      int pixel_height);

 private:
  InterfaceManager* m_manager{nullptr};
};

}  // namespace App::Interface
