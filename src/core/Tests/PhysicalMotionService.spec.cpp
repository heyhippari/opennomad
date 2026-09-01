#include "Core/Character/PhysicalMotionService.hpp"

#include <doctest/doctest.h>

#include "Core/Character/CharacterRuntime.hpp"

TEST_SUITE("Core::Character::PhysicalMotionService") {
  TEST_CASE("synchronization initializes both actor-owned positions from the live transform") {
    App::Character::RuntimeCharacter character;
    character.transform.translation = {.x = 11.0F, .y = 22.0F, .z = 33.0F};

    App::Character::PhysicalMotionService::synchronize_if_needed(character);

    CHECK(character.physical_motion.initialized);
    CHECK_EQ(character.physical_motion.candidate_translation.x, 11.0F);
    CHECK_EQ(character.physical_motion.candidate_translation.y, 22.0F);
    CHECK_EQ(character.physical_motion.candidate_translation.z, 33.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.x, 11.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.y, 22.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.z, 33.0F);
  }

  TEST_CASE("neutral resolution accepts the candidate and clears the physical transient") {
    App::Character::RuntimeCharacter character;
    character.transform.translation = {.x = 1.0F, .y = 2.0F, .z = 3.0F};
    App::Character::PhysicalMotionService::synchronize(character);
    character.physical_motion.candidate_translation = {.x = 4.0F, .y = 5.0F, .z = 6.0F};
    character.suppress_automatic_movement_heading = true;

    App::Character::PhysicalMotionService::resolve_tick(character);

    CHECK_EQ(character.physical_motion.accepted_translation.x, 4.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.y, 5.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.z, 6.0F);
    CHECK_EQ(character.transform.translation.x, 4.0F);
    CHECK_EQ(character.transform.translation.y, 5.0F);
    CHECK_EQ(character.transform.translation.z, 6.0F);
    CHECK_FALSE(character.suppress_automatic_movement_heading);
  }

  TEST_CASE("external live-transform changes re-anchor both positions") {
    App::Character::RuntimeCharacter character;
    character.transform.translation = {.x = 1.0F, .y = 2.0F, .z = 3.0F};
    App::Character::PhysicalMotionService::synchronize(character);
    character.physical_motion.candidate_translation = {.x = 50.0F, .y = 60.0F, .z = 70.0F};
    character.transform.translation = {.x = 8.0F, .y = 9.0F, .z = 10.0F};

    App::Character::PhysicalMotionService::synchronize_if_needed(character);

    CHECK_EQ(character.physical_motion.candidate_translation.x, 8.0F);
    CHECK_EQ(character.physical_motion.candidate_translation.y, 9.0F);
    CHECK_EQ(character.physical_motion.candidate_translation.z, 10.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.x, 8.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.y, 9.0F);
    CHECK_EQ(character.physical_motion.accepted_translation.z, 10.0F);
  }
}
