#include "Core/Dialog/DialogRuntime.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Omikron/IamDialog.hpp"

namespace App::Dialog {

DialogRuntime::DialogRuntime(ConditionEvaluator condition_evaluator, ActionExecutor action_executor)
    : m_condition_evaluator(std::move(condition_evaluator)),
      m_action_executor(std::move(action_executor)) {}

void DialogRuntime::set_condition_evaluator(ConditionEvaluator evaluator) {
  m_condition_evaluator = std::move(evaluator);
}

void DialogRuntime::set_action_executor(ActionExecutor executor) {
  m_action_executor = std::move(executor);
}

std::expected<void, std::string> DialogRuntime::start(Omikron::IamDialogRecord record) {
  reset();
  m_record.emplace(std::move(record));
  return enter_node(0);
}

std::expected<void, std::string> DialogRuntime::fail(std::string error) {
  m_last_error = std::move(error);
  m_state = DialogState::k_failed;
  ++m_generation;
  return std::expected<void, std::string>{std::unexpect, m_last_error};
}

std::expected<void, std::string> DialogRuntime::enter_node(const std::int16_t node_id) {
  if (!m_record.has_value()) {
    return fail("DialogRuntime: no dialog record is loaded");
  }
  const auto node{m_record->node_by_id(node_id)};
  if (!node.has_value()) {
    return fail(fmt::format("DialogRuntime: target node {} does not exist", node_id));
  }

  std::vector<std::size_t> available;
  for (std::size_t slot{0}; slot < Omikron::IamDialogNode::k_response_count; ++slot) {
    if (m_record->response_text(node.value(), slot).empty()) {
      continue;
    }
    const std::span<const std::byte> condition{m_record->condition_program(node.value(), slot)};
    if (condition.empty()) {
      available.push_back(slot);
      continue;
    }
    if (!m_condition_evaluator) {
      return fail(fmt::format(
          "DialogRuntime node {} response {}: condition bytecode execution is unsupported",
          node_id,
          slot));
    }
    auto result{m_condition_evaluator(condition)};
    if (!result) {
      return fail(fmt::format(
          "DialogRuntime node {} response {} condition failed: {}", node_id, slot, result.error()));
    }
    if (result.value()) {
      available.push_back(slot);
    }
  }

  m_node = node;
  m_available_slots = std::move(available);
  m_state = DialogState::k_presenting_line;
  ++m_generation;
  return {};
}

std::expected<void, std::string> DialogRuntime::acknowledge_line() {
  if (!m_record.has_value() || !m_node.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "DialogRuntime: no dialog line is active"};
  }
  if (m_state == DialogState::k_presenting_line) {
    if (!m_record->automatic_player_line(m_node.value()).empty()) {
      m_state = DialogState::k_presenting_automatic_player_line;
    } else {
      m_state =
          m_available_slots.empty() ? DialogState::k_completed : DialogState::k_waiting_for_choice;
    }
    ++m_generation;
    return {};
  }
  if (m_state == DialogState::k_presenting_automatic_player_line) {
    m_state =
        m_available_slots.empty() ? DialogState::k_completed : DialogState::k_waiting_for_choice;
    ++m_generation;
    return {};
  }
  return std::expected<void, std::string>{std::unexpect,
      fmt::format(
          "DialogRuntime: cannot acknowledge a line while in state {}", static_cast<int>(m_state))};
}

std::expected<void, std::string> DialogRuntime::select_choice(const std::size_t slot) {
  if (m_state != DialogState::k_waiting_for_choice || !m_record.has_value() ||
      !m_node.has_value()) {
    return std::expected<void, std::string>{
        std::unexpect, "DialogRuntime: no response choice is currently expected"};
  }
  if (std::ranges::find(m_available_slots, slot) == m_available_slots.end()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("DialogRuntime: response slot {} is not currently available", slot)};
  }

  const std::span<const std::byte> action{m_record->action_program(m_node.value(), slot)};
  if (!action.empty()) {
    if (!m_action_executor) {
      return fail(
          fmt::format("DialogRuntime node {} response {}: action bytecode execution is unsupported",
              m_node->node_id,
              slot));
    }
    auto result{m_action_executor(action)};
    if (!result) {
      return fail(fmt::format("DialogRuntime node {} response {} action failed: {}",
          m_node->node_id,
          slot,
          result.error()));
    }
  }

  const std::int16_t target{m_node->target_node_ids.at(slot)};
  if (target < 0) {
    m_state = DialogState::k_completed;
    ++m_generation;
    return {};
  }
  return enter_node(target);
}

void DialogRuntime::reset() {
  m_record.reset();
  m_node.reset();
  m_available_slots.clear();
  m_state = DialogState::k_inactive;
  m_last_error.clear();
  ++m_generation;
}

bool DialogRuntime::active() const {
  return m_state == DialogState::k_presenting_line ||
         m_state == DialogState::k_presenting_automatic_player_line ||
         m_state == DialogState::k_waiting_for_choice;
}

bool DialogRuntime::take_completion() {
  if (!completed()) {
    return false;
  }
  reset();
  return true;
}

DialogCameraPair DialogRuntime::resolve_cameras(
    const std::array<std::int16_t, 2>& authored_ids) const {
  DialogCameraPair pair{.authored_ids = authored_ids, .cameras = {}};
  if (!m_record.has_value()) {
    return pair;
  }
  for (std::size_t index{0}; index < authored_ids.size(); ++index) {
    if (authored_ids.at(index) >= 0) {
      pair.cameras.at(index) = m_record->camera_by_id(authored_ids.at(index));
    }
  }
  return pair;
}

std::optional<DialogPresentation> DialogRuntime::presentation() const {
  if (!active() || !m_record.has_value() || !m_node.has_value()) {
    return std::nullopt;
  }

  DialogPresentation result;
  result.character_id = m_record->character_id();
  result.node_id = m_node->node_id;
  result.state = m_state;
  result.main_line = m_record->main_line(m_node.value());
  result.automatic_player_line = m_record->automatic_player_line(m_node.value());
  result.displayed_line = m_state == DialogState::k_presenting_automatic_player_line
                              ? result.automatic_player_line
                              : result.main_line;
  result.face_motion_base = m_node->face_motion_base;
  if (!result.face_motion_base.empty()) {
    result.face_motion_resource = fmt::format("{}.3dm", result.face_motion_base);
  }
  result.line_cameras = resolve_cameras(m_node->line_camera_ids);
  result.response_cameras = resolve_cameras(m_node->response_camera_ids);
  result.choices.reserve(m_available_slots.size());
  for (const std::size_t slot : m_available_slots) {
    result.choices.push_back(DialogChoice{.slot = slot,
        .text = m_record->response_text(m_node.value(), slot),
        .target_node = m_node->target_node_ids.at(slot)});
  }
  return result;
}

}  // namespace App::Dialog
