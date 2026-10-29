#include "Core/Scenario/CompactContextRegistry.hpp"

#include <span>

#include "Core/Script/AreaScriptRuntime.hpp"

namespace App {

Script::CompactContextHandle CompactContextRegistry::register_context(
    Script::AreaScriptRuntime& runtime) {
  if (runtime.compact_context_handle().has_value()) {
    return *runtime.compact_context_handle();
  }
  const Script::CompactContextHandle handle{.generation = m_next_generation++};
  runtime.set_compact_context_handle(handle);
  return handle;
}

Script::AreaScriptRuntime* CompactContextRegistry::resolve(const Script::CompactContextHandle handle,
    const std::span<Script::AreaScriptRuntime* const> live_contexts) {
  for (Script::AreaScriptRuntime* const runtime : live_contexts) {
    if (runtime != nullptr && runtime->compact_context_handle() == handle) {
      return runtime;
    }
  }
  return nullptr;
}

}  // namespace App
