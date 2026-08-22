#include "Core/Debug/SceneDebugView.hpp"

#include <vector>

#include "Core/Sprite/SpriteRenderer.hpp"

namespace App::Debug {

SpriteRenderDebugState make_sprite_render_debug_state(const ScenarioRuntime* const runtime,
    const Sprite::SpriteQueueStats& stats,
    const std::vector<Sprite::SpriteDrawCommand>& commands) {
  SpriteRenderDebugState result{.runtime = runtime,
      .attached = stats.attached,
      .visible = stats.visible,
      .drawn = stats.drawn,
      .culled = stats.culled,
      .invalid = stats.invalid,
      .batches = stats.batches,
      .draw_calls = stats.draw_calls,
      .commands = {},
      .skipped = {}};
  result.commands.reserve(commands.size());
  for (const Sprite::SpriteDrawCommand& command : commands) {
    result.commands.push_back(SpriteDrawCommandDebugState{.resource_index = command.resource_index,
        .material_index = command.material_index,
        .vertex_count = command.vertex_count,
        .render_mode = command.pipeline_key.render_mode});
  }
  result.skipped.reserve(stats.skipped.size());
  for (const auto& [handle, reason] : stats.skipped) {
    result.skipped.push_back(
        SpriteSkipDebugState{.handle = handle, .reason = Sprite::skip_reason_name(reason)});
  }
  return result;
}

}  // namespace App::Debug
