#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

#include <cmath>
#include <cstdint>
#include <span>
#include <utility>

#include "Core/Camera.hpp"
#include "Core/CameraController.hpp"
#include "Core/Input/Binding.hpp"
#include "Core/Input/ControlScheme.hpp"
#include "Core/Input/HeldInputState.hpp"
#include "Core/Input/InputAction.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Input/InputSource.hpp"
#include "Core/Input/RawInputState.hpp"
#include "Core/Input/TextInputState.hpp"

namespace App::Input {
namespace {

/// A manager with the default keyboard + mouse scheme registered.
InputManager make_default_manager() {
  InputManager manager;
  manager.add_scheme(ControlScheme::make_keyboard_mouse_default());
  return manager;
}

/// An extra key binding targeting move_forward (used for multi-scheme sums).
ControlScheme make_extra_scheme() {
  ControlScheme scheme{"Extra", Device::k_keyboard_mouse};
  scheme.add_binding({.action = Action::k_move_forward,
      .source = InputSource{.type = SourceType::k_key,
          .index = static_cast<std::uint32_t>(SDL_SCANCODE_E)},
      .scale = 1.0F});
  return scheme;
}

}  // namespace
}  // namespace App::Input

TEST_SUITE("Core::Input") {
  using App::Input::Action;
  using App::Input::ControlScheme;
  using App::Input::HeldInputState;
  using App::Input::InputManager;
  using App::Input::InputSource;
  using App::Input::RawInputState;
  using App::Input::SourceType;
  using App::Input::TextInputState;

  TEST_CASE("Keyboard bindings resolve to signed axes") {
    InputManager manager{App::Input::make_default_manager()};
    RawInputState state{};

    state.key_down.at(SDL_SCANCODE_W) = true;
    manager.update(state);
    CHECK_EQ(manager.get_action_value(Action::k_move_forward), doctest::Approx(1.0F));
    CHECK_EQ(manager.get_action_value(Action::k_move_right), doctest::Approx(0.0F));

    // W and S cancel each other out.
    state.key_down.at(SDL_SCANCODE_S) = true;
    manager.update(state);
    CHECK_EQ(manager.get_action_value(Action::k_move_forward), doctest::Approx(0.0F));

    state.key_down.at(SDL_SCANCODE_W) = false;
    manager.update(state);
    CHECK_EQ(manager.get_action_value(Action::k_move_forward), doctest::Approx(-1.0F));
  }

  TEST_CASE("Mouse deltas feed the look axes unclamped") {
    InputManager manager{App::Input::make_default_manager()};
    RawInputState state{};
    state.mouse_delta_x = 42.0F;
    state.mouse_delta_y = -7.5F;

    manager.update(state);
    CHECK_EQ(manager.get_action_value(Action::k_look_yaw), doctest::Approx(42.0F));
    CHECK_EQ(manager.get_action_value(Action::k_look_pitch), doctest::Approx(-7.5F));
  }

  TEST_CASE("Mouse button bindings resolve while held") {
    InputManager manager;
    ControlScheme scheme{"Mouse", App::Input::Device::k_keyboard_mouse};
    scheme.add_binding({.action = Action::k_move_up,
        .source = InputSource{.type = SourceType::k_mouse_button, .index = SDL_BUTTON_LEFT},
        .scale = 1.0F});
    manager.add_scheme(std::move(scheme));

    RawInputState state{};
    state.mouse_button_down.at(SDL_BUTTON_LEFT) = true;
    manager.update(state);
    CHECK_EQ(manager.get_action_value(Action::k_move_up), doctest::Approx(1.0F));
  }

  TEST_CASE("Disabled schemes contribute nothing") {
    InputManager manager{App::Input::make_default_manager()};
    manager.set_scheme_enabled("Keyboard + Mouse", false);

    RawInputState state{};
    state.key_down.at(SDL_SCANCODE_W) = true;
    manager.update(state);
    CHECK_EQ(manager.get_action_value(Action::k_move_forward), doctest::Approx(0.0F));
  }

  TEST_CASE("Digital actions clamp to [-1, 1] across schemes") {
    InputManager manager{App::Input::make_default_manager()};
    manager.add_scheme(App::Input::make_extra_scheme());

    // W (default scheme) + E (extra scheme) both push move_forward to +2.
    RawInputState state{};
    state.key_down.at(SDL_SCANCODE_W) = true;
    state.key_down.at(SDL_SCANCODE_E) = true;
    manager.update(state);
    CHECK_EQ(manager.get_action_value(Action::k_move_forward), doctest::Approx(1.0F));

    // The extra scheme alone contributes +1.
    state.key_down.at(SDL_SCANCODE_W) = false;
    manager.update(state);
    CHECK_EQ(manager.get_action_value(Action::k_move_forward), doctest::Approx(1.0F));
  }

  TEST_CASE("Press and release edges fire on threshold crossings") {
    InputManager manager{App::Input::make_default_manager()};
    RawInputState state{};

    manager.update(state);
    CHECK_FALSE(manager.is_action_pressed(Action::k_move_forward));

    state.key_down.at(SDL_SCANCODE_W) = true;
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_move_forward));
    CHECK_FALSE(manager.is_action_released(Action::k_move_forward));

    manager.update(state);
    CHECK_FALSE(manager.is_action_pressed(Action::k_move_forward));

    state.key_down.at(SDL_SCANCODE_W) = false;
    manager.update(state);
    CHECK(manager.is_action_released(Action::k_move_forward));
    CHECK_FALSE(manager.is_action_pressed(Action::k_move_forward));
  }

  TEST_CASE("Debug toggle keys map to dedicated actions") {
    InputManager manager{App::Input::make_default_manager()};
    RawInputState state{};

    manager.update(state);
    CHECK_FALSE(manager.is_action_pressed(Action::k_toggle_lights));

    state.key_down.at(SDL_SCANCODE_L) = true;
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));
    CHECK_EQ(manager.get_action_value(Action::k_toggle_lights), doctest::Approx(1.0F));

    // Held keys do not re-fire the edge.
    manager.update(state);
    CHECK_FALSE(manager.is_action_pressed(Action::k_toggle_lights));

    state.key_down.at(SDL_SCANCODE_L) = false;
    manager.update(state);
    CHECK(manager.is_action_released(Action::k_toggle_lights));
  }

  TEST_CASE("Edge-mask bit one: pressed fires only on the rising edge") {
    InputManager manager{App::Input::make_default_manager()};
    manager.set_action_edge_mask(Action::k_toggle_lights, true);
    RawInputState state{};

    manager.update(state);
    CHECK_FALSE(manager.is_action_pressed(Action::k_toggle_lights));

    state.key_down.at(SDL_SCANCODE_L) = true;
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));

    // Still held: the mask suppresses the level, no new pressed edge.
    manager.update(state);
    CHECK_FALSE(manager.is_action_pressed(Action::k_toggle_lights));
  }

  TEST_CASE("Edge-mask bit zero: pressed stays active while held") {
    InputManager manager{App::Input::make_default_manager()};
    manager.set_action_edge_mask(Action::k_toggle_lights, false);
    RawInputState state{};

    manager.update(state);
    CHECK_FALSE(manager.is_action_pressed(Action::k_toggle_lights));

    state.key_down.at(SDL_SCANCODE_L) = true;
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));

    // Still held: with the mask cleared the action stays pressed.
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));
  }

  TEST_CASE("Previous held state updates after the pressed calculation") {
    InputManager manager{App::Input::make_default_manager()};
    manager.set_action_edge_mask(Action::k_toggle_lights, true);
    RawInputState state{};

    state.key_down.at(SDL_SCANCODE_L) = true;
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));

    manager.update(state);
    CHECK_FALSE(manager.is_action_pressed(Action::k_toggle_lights));

    // Release, then press again: the previous state was updated to "held",
    // so the next press is a fresh rising edge.
    state.key_down.at(SDL_SCANCODE_L) = false;
    manager.update(state);
    CHECK_FALSE(manager.is_action_pressed(Action::k_toggle_lights));

    state.key_down.at(SDL_SCANCODE_L) = true;
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));
  }

  TEST_CASE("The edge mask is per action") {
    InputManager manager{App::Input::make_default_manager()};
    manager.set_action_edge_mask(Action::k_toggle_lights, false);
    manager.set_action_edge_mask(Action::k_move_up, true);
    RawInputState state{};

    state.key_down.at(SDL_SCANCODE_L) = true;
    state.key_down.at(SDL_SCANCODE_SPACE) = true;
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));
    CHECK(manager.is_action_pressed(Action::k_move_up));

    // Held again: level action stays pressed, edge action does not.
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));
    CHECK_FALSE(manager.is_action_pressed(Action::k_move_up));
  }

  TEST_CASE("The per-frame input field resets to zero on request") {
    // Neutral field recovered from the original clearing DAT_0090e0e0 = 0
    // before the engine callback; nothing writes it yet, but the reset
    // point must exist and start from zero.
    InputManager manager{App::Input::make_default_manager()};
    CHECK_EQ(manager.per_frame_input(), 0U);
    manager.reset_per_frame_input();
    CHECK_EQ(manager.per_frame_input(), 0U);
  }

  TEST_CASE("reset() clears pressed state but preserves edge-mask configuration") {
    InputManager manager{App::Input::make_default_manager()};
    manager.set_action_edge_mask(Action::k_toggle_lights, false);
    RawInputState state{};
    state.key_down.at(SDL_SCANCODE_L) = true;
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));

    manager.reset();
    CHECK_FALSE(manager.is_action_pressed(Action::k_toggle_lights));

    // Mask configuration survives the reset.
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));
    manager.update(state);
    CHECK(manager.is_action_pressed(Action::k_toggle_lights));
  }

  TEST_CASE("Key-down and key-up update held-key state") {
    HeldInputState held;
    CHECK_FALSE(held.key_down(SDL_SCANCODE_W));

    held.on_key_down(SDL_SCANCODE_W);
    CHECK(held.key_down(SDL_SCANCODE_W));

    held.on_key_up(SDL_SCANCODE_W);
    CHECK_FALSE(held.key_down(SDL_SCANCODE_W));

    // The tracker feeds the per-frame snapshot.
    held.on_key_down(SDL_SCANCODE_A);
    RawInputState snapshot{};
    held.fill(snapshot);
    CHECK(snapshot.key_down.at(SDL_SCANCODE_A));
    CHECK_FALSE(snapshot.key_down.at(SDL_SCANCODE_W));
  }

  TEST_CASE("Held keys do not remain stuck after focus loss") {
    HeldInputState held;
    held.on_key_down(SDL_SCANCODE_W);
    held.on_key_down(SDL_SCANCODE_LSHIFT);

    held.clear();

    RawInputState snapshot{};
    held.fill(snapshot);
    for (const bool down : snapshot.key_down) {
      CHECK_FALSE(down);
    }
    CHECK_FALSE(held.key_down(SDL_SCANCODE_W));
    CHECK_FALSE(held.key_down(SDL_SCANCODE_LSHIFT));
  }

  TEST_CASE("Left and right mouse-button state is updated correctly") {
    HeldInputState held;
    CHECK_FALSE(held.mouse_button_down(SDL_BUTTON_LEFT));
    CHECK_FALSE(held.mouse_button_down(SDL_BUTTON_RIGHT));

    held.on_mouse_button_down(SDL_BUTTON_LEFT);
    held.on_mouse_button_down(SDL_BUTTON_RIGHT);
    CHECK(held.mouse_button_down(SDL_BUTTON_LEFT));
    CHECK(held.mouse_button_down(SDL_BUTTON_RIGHT));

    held.on_mouse_button_up(SDL_BUTTON_LEFT);
    CHECK_FALSE(held.mouse_button_down(SDL_BUTTON_LEFT));
    CHECK(held.mouse_button_down(SDL_BUTTON_RIGHT));
  }

  TEST_CASE("Mouse-button state is reconciled after focus loss") {
    HeldInputState held;
    held.on_mouse_button_down(SDL_BUTTON_LEFT);
    held.on_mouse_button_down(SDL_BUTTON_RIGHT);

    held.clear();

    CHECK_FALSE(held.mouse_button_down(SDL_BUTTON_LEFT));
    CHECK_FALSE(held.mouse_button_down(SDL_BUTTON_RIGHT));
    RawInputState snapshot{};
    held.fill(snapshot);
    for (const bool down : snapshot.mouse_button_down) {
      CHECK_FALSE(down);
    }
  }

  TEST_CASE("Text input is accepted only while text capture is enabled") {
    TextInputState text;
    CHECK_FALSE(text.is_enabled());

    // Disabled: input is dropped.
    text.on_text_input("dropped");
    CHECK(text.text().empty());

    text.set_enabled(true);
    CHECK(text.is_enabled());
    text.on_text_input("h");
    text.on_text_input("é");  // UTF-8 is preserved, not truncated to a byte.
    CHECK_EQ(text.text(), "hé");

    text.set_enabled(false);
    text.on_text_input("dropped");
    CHECK_EQ(text.text(), "hé");

    text.clear();
    CHECK(text.text().empty());
    CHECK_FALSE(text.is_enabled());
  }
}

TEST_SUITE("Core::CameraController") {
  using App::Input::InputManager;
  using App::Input::RawInputState;

  TEST_CASE("Forward action moves the camera along its look direction") {
    App::Camera camera{60.0F, 1.0F, 0.1F, 1000.0F};
    camera.set_position(0.0F, 0.0F, 0.0F);
    camera.set_rotation(0.0F, 0.0F);  // Facing +Z at yaw 0.
    App::CameraController controller{camera};

    InputManager manager{App::Input::make_default_manager()};
    RawInputState state{};
    state.key_down.at(SDL_SCANCODE_W) = true;
    manager.update(state);

    controller.update(manager, 1.0F);

    const float expected_speed{controller.get_move_speed()};
    const std::span<const float, 3> position{camera.get_position()};
    CHECK_EQ(position[0], doctest::Approx(0.0F));
    CHECK_EQ(position[1], doctest::Approx(0.0F));
    CHECK_EQ(position[2], doctest::Approx(expected_speed));
  }

  TEST_CASE("Strafe action moves along the screen-right axis") {
    App::Camera camera{60.0F, 1.0F, 0.1F, 1000.0F};
    camera.set_position(0.0F, 0.0F, 0.0F);
    camera.set_rotation(0.0F, 0.0F);
    App::CameraController controller{camera};

    InputManager manager{App::Input::make_default_manager()};
    RawInputState state{};
    state.key_down.at(SDL_SCANCODE_D) = true;
    manager.update(state);

    controller.update(manager, 1.0F);

    // Facing +Z, screen-right is world -X (cross(front, up) convention).
    const float expected_speed{controller.get_move_speed()};
    const std::span<const float, 3> position{camera.get_position()};
    CHECK_EQ(position[0], doctest::Approx(-expected_speed));
    CHECK_EQ(position[1], doctest::Approx(0.0F));
    CHECK_EQ(position[2], doctest::Approx(0.0F));
  }

  TEST_CASE("Vertical action moves along world up") {
    App::Camera camera{60.0F, 1.0F, 0.1F, 1000.0F};
    camera.set_position(0.0F, 0.0F, 0.0F);
    camera.set_rotation(0.0F, 0.0F);
    App::CameraController controller{camera};

    InputManager manager{App::Input::make_default_manager()};
    RawInputState state{};
    state.key_down.at(SDL_SCANCODE_SPACE) = true;
    manager.update(state);

    controller.update(manager, 1.0F);

    const float expected_speed{controller.get_move_speed()};
    const std::span<const float, 3> position{camera.get_position()};
    CHECK_EQ(position[0], doctest::Approx(0.0F));
    CHECK_EQ(position[1], doctest::Approx(expected_speed));
    CHECK_EQ(position[2], doctest::Approx(0.0F));
  }

  TEST_CASE("Diagonal movement is normalized to axis speed") {
    App::Camera camera{60.0F, 1.0F, 0.1F, 1000.0F};
    camera.set_position(0.0F, 0.0F, 0.0F);
    camera.set_rotation(0.0F, 0.0F);
    App::CameraController controller{camera};

    InputManager manager{App::Input::make_default_manager()};
    RawInputState state{};
    state.key_down.at(SDL_SCANCODE_W) = true;
    state.key_down.at(SDL_SCANCODE_D) = true;
    manager.update(state);

    controller.update(manager, 1.0F);

    // (forward + right) normalized = (-1/sqrt2, 0, 1/sqrt2), so at the
    // configured speed the position is (-speed/sqrt2, 0, speed/sqrt2).
    const float diagonal{controller.get_move_speed() / std::sqrt(2.0F)};
    const std::span<const float, 3> position{camera.get_position()};
    CHECK_EQ(position[0], doctest::Approx(-diagonal));
    CHECK_EQ(position[1], doctest::Approx(0.0F));
    CHECK_EQ(position[2], doctest::Approx(diagonal));
  }

  TEST_CASE("Mouse look applies yaw and inverts pitch") {
    App::Camera camera{60.0F, 1.0F, 0.1F, 1000.0F};
    camera.set_position(0.0F, 0.0F, 0.0F);
    camera.set_rotation(0.0F, 0.0F);
    App::CameraController controller{camera};

    InputManager manager{App::Input::make_default_manager()};
    RawInputState state{};
    state.mouse_delta_x = 10.0F;
    state.mouse_delta_y = -100.0F;  // Mouse moved up.
    manager.update(state);

    controller.update(manager, 1.0F);

    // Default sensitivity is 0.15 degrees per pixel: mouse right turns right
    // (negative yaw when facing +Z), mouse up looks up.
    CHECK_EQ(camera.get_yaw(), doctest::Approx(-1.5F));
    CHECK_EQ(camera.get_pitch(), doctest::Approx(15.0F));
  }

  TEST_CASE("Pitch is clamped before the view flips over") {
    App::Camera camera{60.0F, 1.0F, 0.1F, 1000.0F};
    camera.set_position(0.0F, 0.0F, 0.0F);
    camera.set_rotation(0.0F, 0.0F);
    App::CameraController controller{camera};

    InputManager manager{App::Input::make_default_manager()};
    RawInputState state{};
    state.mouse_delta_y = -100000.0F;  // Huge upward flick.
    manager.update(state);

    controller.update(manager, 1.0F);

    CHECK_EQ(camera.get_pitch(), doctest::Approx(89.0F));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
