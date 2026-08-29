#pragma once

namespace App {
class Scene;
class ScenarioManager;
class ScenarioEngine;

namespace Audio {
class AudioSystem;
}

namespace Startup {
class StartupCoordinator;
class StartupTraceRecorder;
}  // namespace Startup

namespace Interface {
class InterfaceManager;
}
}  // namespace App

namespace App::Debug {

class RuntimeTimingDebugSource;

/// Bundle of non-owning pointers to the subsystems the debug UI inspects.
/// Passed once from Application; every debug window reads only what it needs,
/// so the tools no longer hardwire themselves to a particular Scene subclass.
struct DebugContext {
  Scene* scene{nullptr};
  ScenarioManager* scenario_manager{nullptr};
  ScenarioEngine* scenario_engine{nullptr};
  Interface::InterfaceManager* interface_manager{nullptr};
  Audio::AudioSystem* audio_system{nullptr};
  Startup::StartupCoordinator* startup_coordinator{nullptr};
  Startup::StartupTraceRecorder* startup_trace{nullptr};
  RuntimeTimingDebugSource* runtime_timing{nullptr};
};

}  // namespace App::Debug
