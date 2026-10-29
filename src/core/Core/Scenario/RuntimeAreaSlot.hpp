#pragma once

#include <cstdint>
#include <optional>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"

namespace App {

/// One runtime area slot, mirroring Runtime's `RuntimeAreaSlot` layout with
/// safe ownership: the parsed AREA and optional attached SCENE are owned here;
/// IDs are ordinary values rather than serialized pointers.
struct RuntimeAreaSlot {
  std::optional<Omikron::IamAreaRecord> primary;
  std::optional<Omikron::IamSceneRecord> scene;
  std::int32_t primary_area_id{-1};
  /// START's linked-AREA relationship, not an attached SCENE ID.
  std::int32_t secondary_area_id{-1};
  std::int32_t scene_id{-1};
  /// Stable ScenarioManager world-scene identity owned by this resident slot.
  /// This is not an IAM AREA ID or an array index.
  std::uint32_t world_scene_id{0};
  /// Independent compact IAM context for the attached SCENE top-level script.
  std::optional<Script::AreaScriptRuntime> scene_script;
};

}  // namespace App
