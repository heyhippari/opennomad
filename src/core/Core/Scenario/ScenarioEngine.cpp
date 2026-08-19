#include "Core/Scenario/ScenarioEngine.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <expected>
#include <string>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Startup/StartupTraceRecorder.hpp"

namespace App {

ScenarioEngine::ScenarioEngine(ScenarioManager& manager, Startup::StartupTraceRecorder& trace)
    : m_manager(manager), m_trace(trace) {
  m_startup.set_trace_recorder(&m_trace);
}

std::expected<void, std::string> ScenarioEngine::enter_mode(
    const ScenarioMode mode, [[maybe_unused]] const std::int32_t argument) {
  APP_PROFILE_FUNCTION();

  switch (mode) {
    case ScenarioMode::k_initial:
      m_trace.record("ScenarioMode0.Complete");
      return {};

    case ScenarioMode::k_teardown: {
      m_startup.dispatcher().close_preliminary_29();
      m_trace.record("PreliminaryInterface29.Closed");
      m_trace.record("ScenarioMode3.Complete");
      if (auto result{select_permanent_mode_script_impl("ModeScript.Aventure.Reselected")};
          !result) {
        return result;
      }
      return {};
    }

    case ScenarioMode::k_new_session: {
      m_trace.record("ScenarioMode2.Begin");
      m_trace.record("ScenarioState.Cleared");
      m_trace.record("ScriptContexts.Cleared");
      m_trace.record("AreaSlots.Cleared");
      if (auto result{m_manager.reset_for_new_session()}; !result) {
        return result;
      }
      m_startup.reset_session();
      if (auto result{m_startup.initialize_new_session(m_manager)}; !result) {
        return result;
      }
      m_trace.record("ScenarioMode2.Complete");
      return {};
    }

    case ScenarioMode::k_tick: {
      m_trace.record("ScenarioMode1.Begin");
      if (auto result{m_startup.tick()}; !result) {
        return result;
      }
      m_trace.record("ScenarioMode1.Complete");
      return {};
    }
  }

  return std::expected<void, std::string>{
      std::unexpect, fmt::format("unknown scenario mode {}", static_cast<int>(mode))};
}

std::expected<void, std::string> ScenarioEngine::select_permanent_mode_script() {
  APP_PROFILE_FUNCTION();

  return select_permanent_mode_script_impl("ModeScript.Aventure.Selected");
}

std::expected<void, std::string> ScenarioEngine::select_permanent_mode_script_impl(
    std::string event_name) {
  if (auto result{m_startup.select_permanent_mode_script(m_manager)}; !result) {
    return result;
  }
  m_trace.record(std::move(event_name));
  return {};
}

}  // namespace App
