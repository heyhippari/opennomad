#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Omikron/IamCamera.hpp"
#include "Core/Omikron/IamDialog.hpp"

namespace App::Dialog {

enum class DialogState : std::uint8_t {
  k_inactive,
  k_presenting_line,
  k_presenting_automatic_player_line,
  k_waiting_for_choice,
  k_completed,
  k_failed,
};

struct DialogChoice {
  std::size_t slot{0};
  std::string_view text;
  std::int16_t target_node{0};
};

struct DialogCameraPair {
  std::array<std::int16_t, 2> authored_ids{-1, -1};
  std::array<std::optional<Omikron::IamCameraRecord>, 2> cameras;
};

struct DialogPresentation {
  std::int16_t character_id{0};
  std::int16_t node_id{0};
  DialogState state{DialogState::k_inactive};
  std::string_view main_line;
  std::string_view automatic_player_line;
  std::string_view displayed_line;
  std::string_view face_motion_base;
  std::string face_motion_resource;
  DialogCameraPair line_cameras;
  DialogCameraPair response_cameras;
  std::vector<DialogChoice> choices;
};

using ConditionEvaluator =
    std::function<std::expected<bool, std::string>(std::span<const std::byte> program)>;
using ActionExecutor =
    std::function<std::expected<void, std::string>(std::span<const std::byte> program)>;

/// CPU-only progression state for one active immutable dialog record.
class DialogRuntime {
 public:
  DialogRuntime() = default;
  explicit DialogRuntime(
      ConditionEvaluator condition_evaluator, ActionExecutor action_executor = {});

  void set_condition_evaluator(ConditionEvaluator evaluator);
  void set_action_executor(ActionExecutor executor);

  [[nodiscard]] std::expected<void, std::string> start(Omikron::IamDialogRecord record);
  [[nodiscard]] std::expected<void, std::string> acknowledge_line();
  [[nodiscard]] std::expected<void, std::string> select_choice(std::size_t slot);

  void reset();
  [[nodiscard]] bool active() const;
  [[nodiscard]] bool completed() const {
    return m_state == DialogState::k_completed;
  }
  [[nodiscard]] bool take_completion();
  [[nodiscard]] DialogState state() const {
    return m_state;
  }
  [[nodiscard]] std::uint64_t generation() const {
    return m_generation;
  }
  [[nodiscard]] std::optional<DialogPresentation> presentation() const;
  [[nodiscard]] const std::string& last_error() const {
    return m_last_error;
  }

 private:
  [[nodiscard]] std::expected<void, std::string> enter_node(std::int16_t node_id);
  [[nodiscard]] std::expected<void, std::string> fail(std::string error);
  [[nodiscard]] DialogCameraPair resolve_cameras(
      const std::array<std::int16_t, 2>& authored_ids) const;

  std::optional<Omikron::IamDialogRecord> m_record;
  std::optional<Omikron::IamDialogNode> m_node;
  std::vector<std::size_t> m_available_slots;
  ConditionEvaluator m_condition_evaluator;
  ActionExecutor m_action_executor;
  DialogState m_state{DialogState::k_inactive};
  std::uint64_t m_generation{0};
  std::string m_last_error;
};

}  // namespace App::Dialog
