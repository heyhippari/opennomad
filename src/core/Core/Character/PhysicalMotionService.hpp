#pragma once

#include "Core/RuntimeMath.hpp"

namespace App::Character {

struct RuntimeCharacter;

/// OpenNomad actor-owned representation of Runtime's distinct candidate and
/// accepted physical positions. This models recovered semantics, not native
/// actor offsets or layout.
struct PhysicalMotionState {
  App::Runtime::Vec3 candidate_translation{};
  App::Runtime::Vec3 accepted_translation{};
  float accumulator_seconds{0.0F};
  bool initialized{false};
};

class PhysicalMotionService {
 public:
  static constexpr float K_LOGIC_STEP_SECONDS{1.0F / 30.0F};

  static void synchronize(RuntimeCharacter& character);
  static void synchronize_if_needed(RuntimeCharacter& character);
  static void resolve_tick(RuntimeCharacter& character);
};

}  // namespace App::Character