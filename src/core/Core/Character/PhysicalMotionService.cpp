#include "Core/Character/PhysicalMotionService.hpp"

#include "Core/Character/CharacterRuntime.hpp"

namespace App::Character {

void PhysicalMotionService::synchronize(RuntimeCharacter& character) {
  PhysicalMotionState& motion{character.physical_motion};
  motion.candidate_translation = character.transform.translation;
  motion.accepted_translation = character.transform.translation;
  motion.initialized = true;
}

void PhysicalMotionService::synchronize_if_needed(RuntimeCharacter& character) {
  const PhysicalMotionState& motion{character.physical_motion};
  const App::Runtime::Vec3& live{character.transform.translation};
  if (!motion.initialized || live.x != motion.accepted_translation.x ||
      live.y != motion.accepted_translation.y || live.z != motion.accepted_translation.z) {
    synchronize(character);
  }
}

void PhysicalMotionService::resolve_tick(RuntimeCharacter& character) {
  PhysicalMotionState& motion{character.physical_motion};
  motion.accepted_translation = motion.candidate_translation;
  character.transform.translation = motion.accepted_translation;
  character.suppress_automatic_movement_heading = false;
}

}  // namespace App::Character