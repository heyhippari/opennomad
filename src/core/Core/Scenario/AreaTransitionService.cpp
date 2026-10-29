#include "Core/Scenario/AreaTransitionService.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/GameState.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Scenario/RuntimeAreaSlot.hpp"
#include "Core/Scenario/ScenarioManager.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Scenario/ScopeExit.hpp"
#include "Core/Script/AreaScriptRuntime.hpp"

namespace App {

namespace {

/// Runtime 0x00408220 expires a coordinator phase once 0xEA61 ms of game time have elapsed.
constexpr float K_PHASE_WATCHDOG_SECONDS{60.001F};
/// Compact departure event (zone exit); queueing it is native coordinator event 2.
constexpr std::uint16_t K_DEPARTURE_EVENT{3};

[[nodiscard]] bool is_crossing_script_state(const AreaTransitionState state) {
  return state == AreaTransitionState::k_rich_pre_running ||
         state == AreaTransitionState::k_rich_departed_first ||
         state == AreaTransitionState::k_rich_post_running;
}

[[nodiscard]] bool requester_departed(const Script::AreaScriptRuntime& requester) {
  return std::ranges::find(requester.queued_events(), K_DEPARTURE_EVENT) !=
         requester.queued_events().end();
}

[[nodiscard]] bool waits_on(
    const Script::AreaScriptRuntime& script, const Script::AreaTransitionHandle handle) {
  return script.state() == Script::AreaScriptState::k_waiting &&
         script.wait_info().kind == Script::AreaWaitKind::k_area_transition &&
         script.wait_info().area_transition_handle.has_value() &&
         script.wait_info().area_transition_handle.value() == handle;
}

}  // namespace

AreaTransitionService::AreaTransitionService(AreaTransitionHost& host) : m_host{host} {}

void AreaTransitionService::reset() {
  m_transition.reset();
  m_state = AreaTransitionState::k_idle;
  m_watchdog_requester.reset();
  m_phase_elapsed_seconds = 0.0F;
  m_timed_out_crossings.clear();
  m_next_generation = 1;
  m_defer_scene_operations = false;
  m_pending_scene_operations.clear();
}

std::expected<Script::AreaTransitionResult, std::string>
AreaTransitionService::begin(const std::size_t owner_slot,
    const Script::CompactContextHandle requester,
    const Script::AreaTransitionRequest& request) {
  if (manager() == nullptr || !m_host.area_archive_available()) {
    return std::expected<Script::AreaTransitionResult, std::string>{
        std::unexpect, "AREA transition coordinator is not initialized"};
  }
  if (owner_slot >= slot_count() || !slot_at(owner_slot).primary.has_value()) {
    return std::expected<Script::AreaTransitionResult, std::string>{
        std::unexpect, "AREA transition source is not resident"};
  }

  // Runtime 0x00408530 event 0. Only the pre-crossing script selects the rich form.
  const bool rich{request.pre_crossing_script_id != -1};
  constexpr Script::AreaTransitionResult k_continue{
      .disposition = Script::AreaTransitionDisposition::k_immediate_continue,
      .handle = std::nullopt};
  constexpr Script::AreaTransitionResult k_busy{
      .disposition = Script::AreaTransitionDisposition::k_busy_retry, .handle = std::nullopt};
  switch (m_state) {
    case AreaTransitionState::k_idle:
      break;
    case AreaTransitionState::k_rich_loading:
      return k_busy;
    case AreaTransitionState::k_seamless_loading:
      return k_continue;
    case AreaTransitionState::k_seamless_attached:
      return rich ? k_busy : k_continue;
    case AreaTransitionState::k_rich_abandoned_loading:
    case AreaTransitionState::k_rich_pre_running:
    case AreaTransitionState::k_rich_departed_first:
    case AreaTransitionState::k_rich_pre_finished_first:
    case AreaTransitionState::k_rich_post_running:
      return m_transition.has_value() && m_transition->source_slot == owner_slot &&
                     m_transition->requester == requester
                 ? k_continue
                 : k_busy;
  }
  if (m_transition.has_value()) {
    return k_busy;
  }
  if (request.target_area_id < 0) {
    return std::expected<Script::AreaTransitionResult, std::string>{
        std::unexpect, fmt::format("target AREA ID {} is negative", request.target_area_id)};
  }

  const std::size_t destination_slot{owner_slot == 0U ? 1U : 0U};
  const RuntimeAreaSlot& source_slot{slot_at(owner_slot)};
  const WorldSceneContext* const source_context{
      manager()->find_world_context(source_slot.world_scene_id)};
  if (source_context == nullptr || source_context->residency == WorldSceneResidencyState::Free) {
    return std::expected<Script::AreaTransitionResult, std::string>{
        std::unexpect, "AREA transition source world is not resident"};
  }
  const Script::AreaTransitionHandle handle{.generation = m_next_generation++};
  m_transition.emplace(PendingAreaTransition{.handle = handle,
      .request = request,
      .source_slot = owner_slot,
      .source_area_id = source_slot.primary_area_id,
      .source_world_scene_id = source_slot.world_scene_id,
      .source_world_generation = source_context->generation,
      .destination_slot = destination_slot,
      .requester = requester,
      .crossing_script_instance = std::nullopt,
      .error = {}});
  m_watchdog_requester = requester;
  enter_state(rich ? AreaTransitionState::k_rich_loading : AreaTransitionState::k_seamless_loading);
  m_host.record_trace("AreaTransition.Accepted",
      fmt::format("generation={} sourceSlot={} destinationSlot={} target={}{}",
          handle.generation,
          owner_slot,
          destination_slot,
          request.target_area_id,
          rich ? fmt::format(" pre={} post={}",
                     request.pre_crossing_script_id,
                     request.post_crossing_script_id)
               : std::string{}));
  App::Log::info(LogCategory::Scenario,
      "AREA opcode 0x2F — accepted {} transition to AREA {} from resident slot {} as generation {}",
      rich ? "rich" : "seamless",
      request.target_area_id,
      owner_slot,
      handle.generation);
  return Script::AreaTransitionResult{
      .disposition = Script::AreaTransitionDisposition::k_accepted_waiting, .handle = handle};
}

std::expected<void, std::string> AreaTransitionService::fail_transition(
    PendingAreaTransition& transition, std::string error) {
  transition.error =
      fmt::format("AREA transition to {} failed: {}", transition.request.target_area_id, error);
  m_host.report_error(transition.error);
  m_host.record_trace("AreaTransition.Failed", transition.error);
  App::Log::error(LogCategory::Scenario, "{}", transition.error);
  return std::expected<void, std::string>{std::unexpect, transition.error};
}

std::expected<void, std::string> AreaTransitionService::service(const float delta_seconds) {
  if (m_transition.has_value() && !m_transition->error.empty()) {
    return std::expected<void, std::string>{std::unexpect, m_transition->error};
  }
  if (auto crossings{service_timed_out_crossings()}; !crossings) {
    return crossings;
  }
  if (m_state == AreaTransitionState::k_idle) {
    return {};
  }

  // Runtime 0x00408220 times out only while the requesting context is still alive.
  if (m_watchdog_requester.has_value() &&
      m_host.resolve_requester(m_watchdog_requester.value()) != nullptr) {
    m_phase_elapsed_seconds += delta_seconds;
    if (m_phase_elapsed_seconds >= K_PHASE_WATCHDOG_SECONDS) {
      return expire_watchdog();
    }
  }
  if (!m_transition.has_value()) {
    return {};
  }

  PendingAreaTransition& transition{m_transition.value()};
  if (manager() == nullptr || !m_host.area_archive_available()) {
    return fail_transition(transition, "coordinator ownership is unavailable");
  }
  if (transition.source_slot >= slot_count()) {
    return fail_transition(
        transition, "requesting resident AREA context has an invalid owner slot");
  }
  const RuntimeAreaSlot& source_slot{slot_at(transition.source_slot)};
  const WorldSceneContext* const source_context{
      manager()->find_world_context(transition.source_world_scene_id)};
  if (!source_slot.primary.has_value() ||
      source_slot.primary_area_id != transition.source_area_id ||
      source_slot.world_scene_id != transition.source_world_scene_id || source_context == nullptr ||
      source_context->residency == WorldSceneResidencyState::Free ||
      source_context->generation != transition.source_world_generation) {
    return fail_transition(
        transition, "requesting resident AREA context was destroyed or recycled");
  }
  Script::AreaScriptRuntime* const requester{m_host.resolve_requester(transition.requester)};
  if (requester == nullptr) {
    const std::string error{
        fmt::format("AREA transition to {} failed: requesting compact context "
                    "no longer exists",
            transition.request.target_area_id)};
    reset_transition();
    m_host.report_error(error);
    App::Log::error(LogCategory::Scenario, "{}", error);
    return std::expected<void, std::string>{std::unexpect, error};
  }
  Script::AreaScriptRuntime& area_script{*requester};
  if (!waits_on(area_script, transition.handle)) {
    return fail_transition(
        transition, "requesting AREA context is not waiting on the accepted generation");
  }

  if (m_state == AreaTransitionState::k_rich_loading && requester_departed(area_script)) {
    enter_state(AreaTransitionState::k_rich_abandoned_loading);
    m_host.record_trace("AreaTransition.DepartedWhileLoading",
        fmt::format("generation={}", transition.handle.generation));
  }

  switch (m_state) {
    case AreaTransitionState::k_seamless_loading: {
      if (auto prepared{prepare_destination(transition, true)}; !prepared) {
        return prepared;
      }
      const Script::AreaTransitionHandle completed_handle{transition.handle};
      const std::size_t source_slot_index{transition.source_slot};
      const std::size_t destination_slot_index{transition.destination_slot};
      const std::int16_t target_area_id{transition.request.target_area_id};
      if (auto completed{area_script.complete_area_transition(completed_handle)}; !completed) {
        return fail_transition(transition, completed.error());
      }
      m_host.record_trace("AreaTransition.Prepared",
          fmt::format("generation={} sourceSlot={} destinationSlot={} target={} resumeIp={:#x}",
              completed_handle.generation,
              source_slot_index,
              destination_slot_index,
              target_area_id,
              area_script.instruction_pointer()));
      App::Log::info(LogCategory::Scenario,
          "AREA {} prepared in resident slot {} with primary context — source resumes at ip={:#x}",
          target_area_id,
          destination_slot_index,
          area_script.instruction_pointer());
      finish_accepted_transition();
      return {};
    }
    case AreaTransitionState::k_rich_loading: {
      if (auto prepared{prepare_destination(transition, true)}; !prepared) {
        return prepared;
      }
      auto launched{m_host.launch_transition_script(transition.source_slot,
          static_cast<std::uint16_t>(transition.request.pre_crossing_script_id))};
      if (!launched) {
        return fail_transition(
            transition, fmt::format("pre-crossing script: {}", launched.error()));
      }
      transition.crossing_script_instance = launched.value();
      enter_state(AreaTransitionState::k_rich_pre_running);
      m_host.record_trace("AreaTransition.PreCrossingStarted",
          fmt::format("generation={} script={} instance={}",
              transition.handle.generation,
              transition.request.pre_crossing_script_id,
              launched.value()));
      return {};
    }
    case AreaTransitionState::k_rich_abandoned_loading: {
      // Runtime still loads the destination but never attaches it. Its requester stays blocked
      // in state 10: no later coordinator event names it again.
      if (auto prepared{prepare_destination(transition, false)}; !prepared) {
        return prepared;
      }
      App::Log::warn(LogCategory::Scenario,
          "AREA transition generation {} abandoned while loading AREA {}; the requester stays "
          "blocked as in Runtime",
          transition.handle.generation,
          transition.request.target_area_id);
      m_host.record_trace(
          "AreaTransition.Abandoned", fmt::format("generation={}", transition.handle.generation));
      reset_transition();
      return {};
    }
    default:
      return service_crossing(transition, area_script);
  }
}

std::expected<void, std::string> AreaTransitionService::service_crossing(
    PendingAreaTransition& transition, Script::AreaScriptRuntime& requester) {
  if (m_state == AreaTransitionState::k_rich_pre_running && requester_departed(requester)) {
    enter_state(AreaTransitionState::k_rich_departed_first);
    m_host.record_trace(
        "AreaTransition.Departed", fmt::format("generation={}", transition.handle.generation));
  }
  if (m_state == AreaTransitionState::k_rich_pre_finished_first) {
    if (!requester_departed(requester)) {
      return {};
    }
    m_host.record_trace(
        "AreaTransition.Departed", fmt::format("generation={}", transition.handle.generation));
    return start_post_crossing(transition);
  }
  if (!is_crossing_script_state(m_state)) {
    return {};
  }
  if (!transition.crossing_script_instance.has_value()) {
    return fail_transition(transition, "rich transition has no tracked crossing script");
  }

  // Runtime 0x004067D0 message 3 -> 0x00408530 event 4 when the tracked script finishes.
  auto completed{m_host.transition_script_completed(
      transition.source_slot, transition.crossing_script_instance.value())};
  if (!completed) {
    return fail_transition(transition, fmt::format("crossing script: {}", completed.error()));
  }
  if (!completed.value()) {
    return {};
  }

  switch (m_state) {
    case AreaTransitionState::k_rich_pre_running:
      transition.crossing_script_instance.reset();
      enter_state(AreaTransitionState::k_rich_pre_finished_first);
      m_host.record_trace("AreaTransition.PreCrossingFinished",
          fmt::format("generation={}", transition.handle.generation));
      return {};
    case AreaTransitionState::k_rich_departed_first:
      m_host.record_trace("AreaTransition.PreCrossingFinished",
          fmt::format("generation={}", transition.handle.generation));
      return start_post_crossing(transition);
    default: {
      if (auto detached{detach_non_current_resident()}; !detached) {
        return fail_transition(transition, detached.error());
      }
      const Script::AreaTransitionHandle completed_handle{transition.handle};
      const std::int16_t target_area_id{transition.request.target_area_id};
      reset_transition();
      if (auto resumed{requester.complete_area_transition(completed_handle)}; !resumed) {
        return resumed;
      }
      m_host.record_trace("AreaTransition.Completed",
          fmt::format("generation={} target={} resumeIp={:#x}",
              completed_handle.generation,
              target_area_id,
              requester.instruction_pointer()));
      App::Log::info(LogCategory::Scenario,
          "AREA transition to {} completed its crossing — requester resumes at ip={:#x}",
          target_area_id,
          requester.instruction_pointer());
      return {};
    }
  }
}

std::expected<void, std::string> AreaTransitionService::start_post_crossing(
    PendingAreaTransition& transition) {
  if (transition.request.post_crossing_script_id == -1) {
    return fail_transition(transition, "rich transition has no post-crossing script");
  }
  auto launched{m_host.launch_transition_script(transition.source_slot,
      static_cast<std::uint16_t>(transition.request.post_crossing_script_id))};
  if (!launched) {
    return fail_transition(transition, fmt::format("post-crossing script: {}", launched.error()));
  }
  transition.crossing_script_instance = launched.value();
  enter_state(AreaTransitionState::k_rich_post_running);
  m_host.record_trace("AreaTransition.PostCrossingStarted",
      fmt::format("generation={} script={} instance={}",
          transition.handle.generation,
          transition.request.post_crossing_script_id,
          launched.value()));
  return {};
}

std::expected<void, std::string> AreaTransitionService::expire_watchdog() {
  m_host.record_trace(
      "AreaTransition.WatchdogExpired", fmt::format("state={}", static_cast<int>(m_state)));
  App::Log::warn(LogCategory::Scenario,
      "AREA transition coordinator state {} exceeded the Runtime watchdog; resetting",
      static_cast<int>(m_state));
  const AreaTransitionState expired_state{m_state};
  if (!m_transition.has_value()) {
    reset_transition();
    return {};
  }
  const PendingAreaTransition transition{std::move(m_transition).value()};
  reset_transition();

  std::optional<std::size_t> awaited;
  if (is_crossing_script_state(expired_state)) {
    awaited = transition.crossing_script_instance;
  } else if (transition.request.post_crossing_script_id != -1) {
    auto launched{m_host.launch_transition_script(transition.source_slot,
        static_cast<std::uint16_t>(transition.request.post_crossing_script_id))};
    if (!launched) {
      const std::string error{fmt::format("AREA transition to {} watchdog post-crossing script: {}",
          transition.request.target_area_id,
          launched.error())};
      m_host.report_error(error);
      return std::expected<void, std::string>{std::unexpect, error};
    }
    awaited = launched.value();
  }
  if (awaited.has_value()) {
    m_timed_out_crossings.push_back(TimedOutCrossing{.handle = transition.handle,
        .requester = transition.requester,
        .source_slot = transition.source_slot,
        .crossing_script_instance = awaited.value()});
    return {};
  }
  Script::AreaScriptRuntime* const requester{m_host.resolve_requester(transition.requester)};
  if (requester != nullptr && waits_on(*requester, transition.handle)) {
    return requester->complete_area_transition(transition.handle);
  }
  return {};
}

std::expected<void, std::string> AreaTransitionService::service_timed_out_crossings() {
  for (std::size_t index{0}; index < m_timed_out_crossings.size();) {
    const TimedOutCrossing crossing{m_timed_out_crossings.at(index)};
    auto completed{m_host.transition_script_completed(
        crossing.source_slot, crossing.crossing_script_instance)};
    if (!completed) {
      const std::string error{
          fmt::format("timed-out AREA transition generation {} crossing script: {}",
              crossing.handle.generation,
              completed.error())};
      m_host.report_error(error);
      return std::expected<void, std::string>{std::unexpect, error};
    }
    if (!completed.value()) {
      ++index;
      continue;
    }
    // Runtime 0x004067D0 message 3 for a context in state 11.
    m_timed_out_crossings.erase(m_timed_out_crossings.begin() + static_cast<std::ptrdiff_t>(index));
    if (auto detached{detach_non_current_resident()}; !detached) {
      return detached;
    }
    Script::AreaScriptRuntime* const requester{m_host.resolve_requester(crossing.requester)};
    if (requester != nullptr && waits_on(*requester, crossing.handle)) {
      if (auto resumed{requester->complete_area_transition(crossing.handle)}; !resumed) {
        return resumed;
      }
    }
  }
  return {};
}

std::expected<void, std::string> AreaTransitionService::detach_non_current_resident() {
  const std::size_t slot_index{m_host.transition_active_slot() == 0U ? 1U : 0U};
  const RuntimeAreaSlot& slot{slot_at(slot_index)};
  if (!slot.primary.has_value()) {
    return {};
  }
  if (auto detached{manager()->deactivate_world_context(slot.world_scene_id)}; !detached) {
    return detached;
  }
  App::Log::info(LogCategory::Scenario,
      "AREA {} decor detached in resident slot {}",
      slot.primary_area_id,
      slot_index);
  return {};
}

void AreaTransitionService::enter_state(const AreaTransitionState state) {
  m_state = state;
  m_phase_elapsed_seconds = 0.0F;
}

std::expected<void, std::string> AreaTransitionService::prepare_destination(
    PendingAreaTransition& transition, const bool attach) {
  RuntimeAreaSlot& destination_slot{slot_at(transition.destination_slot)};
  if (destination_slot.primary.has_value() &&
      destination_slot.primary_area_id == transition.request.target_area_id) {
    if (!attach) {
      return {};
    }
    if (auto attached{manager()->activate_world_context(destination_slot.world_scene_id)};
        !attached) {
      return fail_transition(
          transition, fmt::format("cached destination decor attach: {}", attached.error()));
    }
    return {};
  }

  // Parse the destination record before touching any resident state so a corrupt or missing
  // record leaves both slots exactly as they were.
  auto parsed_record{m_host.load_area_record(transition.request.target_area_id)};
  if (!parsed_record) {
    return fail_transition(transition, parsed_record.error());
  }

  // Recycling the destination slot replaces its world context. Refuse before destroying any
  // controller state when the context that would be recycled is still logically current or owns
  // the controlled body (the manager's allocator would otherwise fail after the teardown).
  if (destination_slot.primary.has_value()) {
    const WorldSceneContext* const stale_context{
        manager()->find_world_context(destination_slot.world_scene_id)};
    const std::optional<ControlledCharacterRef> controlled{manager()->controlled_character()};
    if (stale_context != nullptr &&
        (manager()->current_world_context() == stale_context ||
            (controlled.has_value() &&
                controlled->world_scene_id == destination_slot.world_scene_id))) {
      return fail_transition(transition,
          fmt::format("destination resident slot {} (AREA {}) is still the logical current AREA "
                      "or owns the controlled body",
              transition.destination_slot,
              destination_slot.primary_area_id));
    }
  }

  // Discards everything the controller holds for the destination slot, including its world
  // context. Used both to evict a stale resident and to roll back a partially built destination.
  std::string rollback_failure;
  const auto clear_destination_slot = [&](const bool unload_world) {
    m_host.erase_zone_contacts(transition.destination_slot, /*scene_source_only=*/false);
    destination_slot.scene_script.reset();
    m_host.discard_area_script(transition.destination_slot);
    if (destination_slot.primary_area_id >= 0) {
      m_host.transition_character_references().remove_area(
          transition.destination_slot, destination_slot.primary_area_id);
    }
    destination_slot.scene.reset();
    destination_slot.primary.reset();
    destination_slot.scene_id = -1;
    destination_slot.primary_area_id = -1;
    destination_slot.secondary_area_id = -1;
    if (unload_world) {
      // Unloading requires the context to be detached first.
      if (auto detached{manager()->deactivate_world_context(destination_slot.world_scene_id)};
          !detached) {
        rollback_failure = detached.error();
      } else if (auto unloaded{manager()->unload_world_context(destination_slot.world_scene_id)};
                 !unloaded) {
        rollback_failure = unloaded.error();
      }
    }
  };
  // A failure after the destination became partially resident: roll it back completely.
  const auto fail_and_roll_back = [&](const std::string& error) {
    clear_destination_slot(true);
    return fail_transition(transition,
        rollback_failure.empty() ? error
                                 : fmt::format("{}; rollback failed: {}", error, rollback_failure));
  };

  if (destination_slot.primary.has_value()) {
    clear_destination_slot(false);
  }

  Omikron::IamAreaRecord destination_record{std::move(parsed_record).value()};
  auto prepared{m_host.prepare_area_world(
      transition.destination_slot, transition.request.target_area_id, destination_record)};
  if (!prepared) {
    return fail_transition(transition, prepared.error());
  }

  m_host.record_trace("AreaTransition.TargetPrepared",
      fmt::format("generation={} area={} slot={} model='{}' scx='{}' sky='{}'",
          transition.handle.generation,
          transition.request.target_area_id,
          transition.destination_slot,
          destination_record.model3do_name(),
          destination_record.scenario_scx_name(),
          destination_record.sky_3do_name()));
  if (!prepared->sky_name.empty()) {
    m_host.record_trace("AreaTransition.SkyPreserved", prepared->sky_name);
  }

  destination_slot.primary.emplace(std::move(destination_record));
  destination_slot.primary_area_id = transition.request.target_area_id;
  destination_slot.secondary_area_id = -1;
  destination_slot.scene.reset();
  destination_slot.scene_id = -1;
  destination_slot.scene_script.reset();
  if (auto bound{m_host.bind_area_characters(transition.destination_slot, prepared->characters)};
      !bound) {
    return fail_and_roll_back(fmt::format("destination character binding: {}", bound.error()));
  }
  if (attach) {
    if (auto attached{manager()->activate_world_context(destination_slot.world_scene_id)};
        !attached) {
      return fail_and_roll_back(fmt::format("destination decor attach: {}", attached.error()));
    }
  }

  // Runtime creates the destination AREA's compact context as part of loading
  // the resident AREA and queues event 1 immediately. It does not replace the
  // source context: both remain registered until the source explicitly runs
  // 0x30. The scheduler below services the older source registration first,
  // so the concrete AREA118 -> AREA222 path performs 0x47 before AREA222's
  // event 1 launches IMPASSE script 20 and switches the camera to mode 13.
  if (auto installed{m_host.install_area_script(transition.destination_slot)}; !installed) {
    return fail_and_roll_back(fmt::format("destination primary context: {}", installed.error()));
  }
  if (auto refreshed{m_host.refresh_zones()}; !refreshed) {
    return fail_and_roll_back(fmt::format("resident zone refresh: {}", refreshed.error()));
  }
  return {};
}

void AreaTransitionService::finish_accepted_transition() {
  m_transition.reset();
  enter_state(AreaTransitionState::k_seamless_attached);
}

void AreaTransitionService::reset_transition() {
  m_transition.reset();
  m_watchdog_requester.reset();
  enter_state(AreaTransitionState::k_idle);
}

std::expected<void, std::string> AreaTransitionService::attach_scene(
    const Script::AreaSceneAttachRequest& request) {
  if (m_defer_scene_operations) {
    m_pending_scene_operations.emplace_back(request);
    return {};
  }
  return attach_scene_now(request);
}

std::expected<void, std::string> AreaTransitionService::attach_scene_now(
    const Script::AreaSceneAttachRequest& request) {
  if (manager() == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }
  if (request.scene_id < 0) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("SCENE ID {} is negative", request.scene_id)};
  }

  GameState* const game_state{manager()->game_state()};
  if (game_state == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "session game state is not initialized"};
  }

  const auto slot_index{m_host.transition_resident_slot(request.area_id)};
  if (!slot_index.has_value()) {
    // Runtime 0x00403950 always commits the AREA -> SCENE mapping through
    // 0x0040B120, even when neither of its two resident AREA slots matches.
    // The SCENE loader/materializer lives inside the resident-slot match, so a
    // nonresident AREA must not touch IAM/SCENE yet: the AREA loader consumes
    // this mapping if/when that AREA becomes resident later.
    if (auto mapped{game_state->set_area_mapping(request.area_id, request.scene_id)}; !mapped) {
      return mapped;
    }
    if (auto refreshed{m_host.refresh_zones()}; !refreshed) {
      return refreshed;
    }
    m_host.record_trace("AreaScript.AreaSceneMapped",
        fmt::format("area={} scene={} resident=false", request.area_id, request.scene_id));
    App::Log::debug(LogCategory::Scenario,
        "AREA {} selected SCENE {} for future residency",
        request.area_id,
        request.scene_id);
    return {};
  }

  auto parsed_scene{m_host.load_scene_record(request.scene_id)};
  if (!parsed_scene) {
    return std::expected<void, std::string>{std::unexpect, parsed_scene.error()};
  }

  RuntimeAreaSlot& slot{slot_at(slot_index.value())};
  if (!slot.primary.has_value()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("AREA {} resident slot has no parsed AREA record", request.area_id)};
  }
  ScenarioRuntime* scenario_runtime{manager()->world_runtime(slot.world_scene_id)};
  if (scenario_runtime == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, fmt::format("AREA {} has no prepared world runtime", request.area_id)};
  }

  if (slot.scene.has_value()) {
    if (auto detached{detach_scene_from_slot(slot_index.value())}; !detached) {
      return detached;
    }
  }

  slot.scene.emplace(std::move(parsed_scene).value());
  slot.scene_id = request.scene_id;
  m_host.transition_character_references().install_scene(
      slot_index.value(), slot.primary_area_id, slot.scene_id, *slot.scene);
  ScopeExit rollback_scene_attach{[&] {
    slot.scene_script.reset();
    m_host.transition_character_references().remove_scene(slot_index.value(), request.scene_id);
    std::optional<Character::BodyIdentity> preserved_body_identity;
    if (const auto controlled{manager()->controlled_character()};
        controlled.has_value() && controlled->world_scene_id == slot.world_scene_id) {
      preserved_body_identity = controlled->body_identity;
    }
    scenario_runtime->character_runtime().dematerialize_scene_characters(
        slot.primary_area_id, request.scene_id, preserved_body_identity);
    scenario_runtime->object_placement_runtime().dematerialize_scene_objects(
        slot.primary_area_id, request.scene_id);
    slot.scene.reset();
    slot.scene_id = -1;
  }};
  const std::optional<ControlledCharacterRef> current{manager()->controlled_character()};
  const std::optional<Character::BodyIdentity> preserved_body_identity{
      current.has_value() && current->world_scene_id == slot.world_scene_id
          ? std::optional<Character::BodyIdentity>{current->body_identity}
          : std::nullopt};
  auto materialized{scenario_runtime->character_runtime().preload_scene_characters(
      slot.primary_area_id, slot.scene_id, *slot.scene, preserved_body_identity)};
  if (!materialized) {
    const std::string preload_error{std::move(materialized).error()};
    return std::expected<void, std::string>{std::unexpect, preload_error};
  }
  for (std::size_t index{0}; index < materialized->size(); ++index) {
    if (auto bound{m_host.transition_character_references().bind_placement_body(
            CharacterReferenceSource::k_scene,
            slot_index.value(),
            slot.primary_area_id,
            slot.scene_id,
            index,
            materialized->at(index).body_identity)};
        !bound) {
      return bound;
    }
  }
  if (auto materialized_objects{
          scenario_runtime->object_placement_runtime().materialize_scene_objects(
              slot.primary_area_id, slot.scene_id, *slot.scene, *game_state)};
      !materialized_objects) {
    const std::string materialize_error{std::move(materialized_objects).error()};
    return std::expected<void, std::string>{std::unexpect, materialize_error};
  }
  if (slot.scene->primary_event_offset() != 0U) {
    slot.scene_script.emplace(slot.scene->record_bytes());
    if (auto entries{slot.scene_script->set_event_entries(
            Script::AreaScriptEventEntries{.event1 = slot.scene->primary_event_offset(),
                .event2 = std::nullopt,
                .event3 = std::nullopt})};
        !entries) {
      return std::expected<void, std::string>{std::unexpect, entries.error()};
    }
    m_host.bind_scene_script(*slot.scene_script, slot_index.value());
    slot.scene_script->queue_event(1);
    slot.scene_script->activate();
    App::Log::info(LogCategory::Script, "SCENE {} event 1 queued", request.scene_id);
  }

  if (auto mapped{game_state->set_area_mapping(request.area_id, request.scene_id)}; !mapped) {
    return mapped;
  }
  if (auto refreshed{m_host.refresh_zones()}; !refreshed) {
    return refreshed;
  }
  const Omikron::IamSceneRecord& scene{slot.scene.value()};
  App::Log::info(LogCategory::Scenario,
      "AREA {} attached SCENE {} — chars={} objects={} zones={} cameras={}",
      request.area_id,
      request.scene_id,
      scene.table_count(0),
      scene.table_count(1),
      scene.table_count(2),
      scene.table_count(6));
  rollback_scene_attach.release();
  return {};
}

std::expected<void, std::string> AreaTransitionService::detach_scene_from_slot(
    const std::size_t slot_index) {
  if (manager() == nullptr || slot_index >= slot_count()) {
    return std::expected<void, std::string>{std::unexpect, "invalid resident AREA slot"};
  }
  RuntimeAreaSlot& slot{slot_at(slot_index)};
  if (!slot.scene.has_value()) {
    return {};
  }
  // Zone-contact scripts borrow bytes from the SCENE record that is about to be destroyed.
  m_host.erase_zone_contacts(slot_index, /*scene_source_only=*/true);
  if (ScenarioRuntime* runtime{manager()->world_runtime(slot.world_scene_id)}; runtime != nullptr) {
    std::optional<Character::BodyIdentity> preserved;
    if (const auto current{manager()->controlled_character()};
        current.has_value() && current->world_scene_id == slot.world_scene_id) {
      preserved = current->body_identity;
    }
    slot.scene_script.reset();
    runtime->character_runtime().dematerialize_scene_characters(
        slot.primary_area_id, slot.scene_id, preserved);
    runtime->object_placement_runtime().dematerialize_scene_objects(
        slot.primary_area_id, slot.scene_id);
  }
  m_host.transition_character_references().remove_scene(slot_index, slot.scene_id);
  slot.scene_script.reset();
  slot.scene.reset();
  slot.scene_id = -1;
  return {};
}

std::expected<void, std::string> AreaTransitionService::detach_scene(
    const Script::AreaSceneDetachRequest& request) {
  if (m_defer_scene_operations) {
    m_pending_scene_operations.emplace_back(request);
    return {};
  }
  return detach_scene_now(request);
}

std::expected<void, std::string> AreaTransitionService::service_pending_scene_operations() {
  // Operations are applied in request order. New operations cannot be queued while draining
  // because deferral is off here.
  std::vector<PendingSceneOperation> operations;
  operations.swap(m_pending_scene_operations);
  for (const PendingSceneOperation& operation : operations) {
    const auto applied{std::visit(
        [this](const auto& request) -> std::expected<void, std::string> {
          if constexpr (std::is_same_v<std::decay_t<decltype(request)>,
                            Script::AreaSceneAttachRequest>) {
            return attach_scene_now(request);
          } else {
            return detach_scene_now(request);
          }
        },
        operation)};
    if (!applied) {
      return applied;
    }
  }
  return {};
}

std::expected<void, std::string> AreaTransitionService::detach_scene_now(
    const Script::AreaSceneDetachRequest& request) {
  const auto slot_index{m_host.transition_resident_slot(request.area_id)};
  if (!slot_index.has_value()) {
    return {};
  }
  if (auto detached{detach_scene_from_slot(*slot_index)}; !detached) {
    return detached;
  }
  if (GameState* const game_state{manager()->game_state()}; game_state != nullptr) {
    if (auto mapped{game_state->set_area_mapping(request.area_id, -1)}; !mapped) {
      return mapped;
    }
  }
  return m_host.refresh_zones();
}

std::expected<void, std::string> AreaTransitionService::release(
    const Script::AreaReleaseRequest& request) {
  if (manager() == nullptr) {
    return std::expected<void, std::string>{std::unexpect, "scenario manager is not available"};
  }

  if (m_state == AreaTransitionState::k_seamless_loading) {
    // Native state 3 advances to a "release pending" state 4 here, whose load completion detaches
    // the non-current decor without attaching the destination (docs/reverse-engineering/
    // iam-scenario-vm.md section 108). State 4 is not modelled; the request is a no-op.
    App::Log::warn(LogCategory::Scenario,
        "AREA release (area={}) requested while the destination is still loading; ignored",
        request.area_id);
    return {};
  }
  if (m_state != AreaTransitionState::k_seamless_attached) {
    return {};
  }

  if (request.area_id == -1) {
    if (auto detached{detach_non_current_resident()}; !detached) {
      return detached;
    }
    reset_transition();
    return {};
  }
  const auto slot_index{m_host.transition_resident_slot(request.area_id)};
  if (!slot_index.has_value() || !slot_at(slot_index.value()).primary.has_value()) {
    reset_transition();
    return {};
  }
  RuntimeAreaSlot& slot{slot_at(slot_index.value())};
  if (auto detached{manager()->deactivate_world_context(slot.world_scene_id)}; !detached) {
    return detached;
  }
  reset_transition();
  App::Log::info(LogCategory::Scenario,
      "AREA {} decor detached in resident slot {}",
      slot.primary_area_id,
      slot_index.value());
  return {};
}

}  // namespace App
