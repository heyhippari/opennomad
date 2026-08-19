#pragma once

#include <cmath>
#include <cstdint>

namespace App::Interface {

/// Scalar endpoint of Runtime's I2D bump state at one 30 Hz boundary.
///
/// This is the small, recovered piece of `I2D_Bump.c` state that changes
/// every effect update: the four warp phases and the moving light position.
/// It deliberately owns no buffers (no gradients, no warp tables, no lit
/// field) so the production renderer can carry two of these (current + next)
/// and advance them cheaply, while the full CPU `I2DBumpEffect` remains the
/// reference/reverse-engineering oracle.
///
/// Runtime addresses (see I2DBumpEffect for details):
///   light/warp state update  ~0x004B1B00
///   warp-table generation     ~0x004B1F40
struct I2DBumpEndpointState {
  /// Number of original effect updates applied (0 = the pre-first-tick
  /// construction state).
  std::uint64_t tick{0};

  double phase_a{0.0};
  double phase_b{1.0};
  double phase_c{2.0};
  double phase_d{0.5};

  int light_x{128};
  int light_y{128};

  /// Internal accumulator feeding light_x/light_y. Runtime derives the light
  /// position from the current angle, then advances the angle by +0.0785.
  /// This is not a recovered output value; it is kept so iterative
  /// accumulation stays bit-identical to Runtime's frame sequence rather than
  /// being recomputed as `10.0 + tick * 0.0785` (which would drift).
  double light_angle{10.0};
};

/// Advances `state` by exactly one authentic Runtime effect update.
///
/// Mirrors the recovered per-tick math verbatim: the light position is
/// derived from the current angle via x87-style truncation toward zero, the
/// angle is then advanced, and the four warp phases are incremented by their
/// recovered constants. `static_cast<int>` on a double truncates toward zero,
/// matching MSVCRT `_ftol`.
inline void advance_endpoint(I2DBumpEndpointState& state) {
  state.light_x = static_cast<int>(std::cos(state.light_angle) * 64.0) + 128;
  state.light_y = static_cast<int>(std::sin(state.light_angle) * 64.0) + 128;
  state.light_angle += 0.0785;

  state.phase_a += 0.009925;
  state.phase_b -= 0.013915;
  state.phase_c -= 0.007685;
  state.phase_d += 0.015635;

  state.tick += 1;
}

/// Advances `state` by `ticks` authentic updates (scalar state only — no
/// per-pixel work). Used for catch-up when the host stalls between frames.
inline void advance_endpoint(I2DBumpEndpointState& state, const std::uint64_t ticks) {
  for (std::uint64_t tick{0}; tick < ticks; ++tick) {
    advance_endpoint(state);
  }
}

}  // namespace App::Interface
