#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

#include "Core/Scenario/RuntimeAreaSlot.hpp"
#include "Core/Scenario/ScenarioManager.hpp"

namespace App {

/// Services the controller provides to the residency service.
class AreaResidencyHost {
 public:
  AreaResidencyHost() = default;
  AreaResidencyHost(const AreaResidencyHost&) = delete;
  AreaResidencyHost(AreaResidencyHost&&) = delete;
  AreaResidencyHost& operator=(const AreaResidencyHost&) = delete;
  AreaResidencyHost& operator=(AreaResidencyHost&&) = delete;

  /// Null before a session is initialized.
  [[nodiscard]] virtual ScenarioManager* residency_manager() const = 0;
  [[nodiscard]] virtual std::span<const RuntimeAreaSlot> residency_slots() const = 0;
  /// Called after a physical handoff moved logical ownership: rebinds session-owned state that
  /// follows the controlled body (trigger proxy) and rebuilds the active-zone set.
  [[nodiscard]] virtual std::expected<void, std::string> residency_handoff_committed(
      const ControlledCharacterRef& controlled) = 0;

 protected:
  ~AreaResidencyHost() = default;
};

/// Owns which resident AREA slot is the logical current AREA and commits native event 9, the
/// physical handoff that moves current AREA ownership (and, when needed, the controlled body's
/// storage) to the world that supplies the actor's accepted support.
///
/// The logical current AREA is held in three views that must agree: this service's slot index,
/// `ScenarioManager::current_world_context()`, and `GameState::current_area()`. Only this service
/// changes the first; `current_slot()` reports any drift instead of papering over it.
class AreaResidencyService {
 public:
  explicit AreaResidencyService(AreaResidencyHost& host);

  void reset();

  /// Index of the logical current AREA slot (Runtime globals 0x69BC60/0x69BC64).
  [[nodiscard]] std::size_t active_slot() const {
    return m_active_slot;
  }

  /// The resident slot holding `area_id` as its primary AREA, if any.
  [[nodiscard]] std::optional<std::size_t> resident_slot(std::int32_t area_id) const;

  /// Validates that the slot, manager, and resident record agree on the logical current AREA.
  [[nodiscard]] std::expected<std::size_t, std::string> current_slot() const;

  /// Commits native event 9 when the accepted support belongs to a world other than the logical
  /// current AREA. Returns true when ownership moved. Body storage moves only when necessary.
  ///
  /// Everything up to the body transfer is rolled back on failure. Once the transfer succeeded the
  /// handoff is committed; later errors are reported to the caller but the new ownership stands.
  [[nodiscard]] std::expected<bool, std::string> commit_physical_handoff(
      std::uint32_t accepted_support_world);

 private:
  AreaResidencyHost& m_host;
  std::size_t m_active_slot{0};
};

}  // namespace App
