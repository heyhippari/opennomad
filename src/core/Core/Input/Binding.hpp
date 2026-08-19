#pragma once

#include "Core/Input/InputAction.hpp"
#include "Core/Input/InputSource.hpp"

namespace App::Input {

/// Maps one physical input onto an action, contributing `scale` to the
/// action's value while the input is active: keys and buttons contribute
/// `scale` while held, mouse axes `scale` per unit of movement.
///
/// Several bindings may target the same action — their contributions are
/// summed by the InputManager (W = +1 and S = -1 on one action make an axis).
struct Binding {
  Action action{Action::k_move_forward};
  InputSource source{};
  float scale{1.0F};
};

}  // namespace App::Input
