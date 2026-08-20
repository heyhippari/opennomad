#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

namespace App {

/// One resolved Runtime AREA camera command waiting to be consumed by the
/// presentation layer.
///
/// Coordinates deliberately remain in Runtime's logical XYZ units here.
/// WorldCameraSystem owns the engine->renderer coordinate conversion and the
/// interpolation policy, so scenario code never depends on GL/view-space
/// conventions.
struct WorldCameraCommand {
  std::uint32_t scene_id{0};
  std::uint32_t scene_generation{0};
  std::uint16_t camera_id{0};

  std::array<std::int32_t, 3> runtime_eye{};
  std::array<std::int32_t, 3> runtime_target{};

  /// Original AREA duration in 30 Hz scenario units.
  std::int16_t duration_units{0};
  std::int16_t flags{0};
  bool wait_for_completion{false};

  /// IAM camera metadata retained verbatim until its remaining projection /
  /// attachment semantics are fully recovered.
  std::uint16_t camera_type{0};
  std::int16_t angle_units{0};
  std::int16_t focal_parameter{0};
  std::int16_t field_20{0};
  std::int16_t field_22{0};
  std::array<std::uint16_t, 4> tail_fields{};
};

/// CPU-only mailbox from scenario execution to WorldScene.
///
/// ScenarioManager owns this because AREA execution can emit presentation
/// commands before or after a WorldScene frame. It intentionally contains no
/// Camera, matrices or GL objects; WorldScene drains the commands and owns the
/// actual presentation state.
class WorldPresentationState {
 public:
  void enqueue_camera(WorldCameraCommand command) {
    m_camera_commands.push_back(std::move(command));
  }

  [[nodiscard]] std::optional<WorldCameraCommand> take_camera() {
    if (m_camera_commands.empty()) {
      return std::nullopt;
    }
    WorldCameraCommand command{std::move(m_camera_commands.front())};
    m_camera_commands.pop_front();
    return command;
  }

  [[nodiscard]] std::size_t pending_camera_count() const {
    return m_camera_commands.size();
  }

  void clear() {
    m_camera_commands.clear();
  }

 private:
  std::deque<WorldCameraCommand> m_camera_commands;
};

}  // namespace App