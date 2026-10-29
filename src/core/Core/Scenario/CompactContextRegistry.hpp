#pragma once

#include <cstdint>
#include <span>

#include "Core/Script/AreaScriptRuntime.hpp"

namespace App {

/// Allocates stable identities for compact IAM contexts. Handles are monotonic for the process,
/// so a handle that outlived its context can never alias a newer one, even across sessions.
class CompactContextRegistry {
 public:
  /// Assigns a fresh handle to `runtime` unless it already has one; returns the handle.
  [[nodiscard]] Script::CompactContextHandle register_context(Script::AreaScriptRuntime& runtime);

  /// The context among `live_contexts` carrying `handle`, or nullptr when it no longer exists.
  [[nodiscard]] static Script::AreaScriptRuntime* resolve(Script::CompactContextHandle handle,
      std::span<Script::AreaScriptRuntime* const> live_contexts);

 private:
  std::uint64_t m_next_generation{1};
};

}  // namespace App
