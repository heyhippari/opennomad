#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "Core/Omikron/CtlControlSet.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Character {

struct RuntimeCharacter;

/// Canonical CTL input mask value when no profile slot is held. No ordinary
/// action sets this bit itself (Runtime controller initialization seeds it).
inline constexpr std::uint32_t K_CTL_NO_INPUT{0x40000000U};
/// Number of ordinary positive CTL profile action bits (slots 0..13).
inline constexpr std::size_t K_CTL_PROFILE_SLOT_COUNT{14U};
/// Low positive-action mask covered by the ordinary 14 profile slots.
inline constexpr std::uint32_t K_CTL_POSITIVE_MASK{0x00003FFFU};
/// Recovered special authored input condition (Runtime 0x004A8AD0 tail).
inline constexpr std::uint32_t K_CTL_CONDITION_SPECIAL{0x80000000U};

/// Converts raw profile slot bits into the canonical CTL input mask.
[[nodiscard]] constexpr std::uint32_t ctl_canonical_input(const std::uint32_t profile_bits) {
  return profile_bits == 0U ? K_CTL_NO_INPUT : profile_bits;
}

/// Runtime 0x004A8AD0. CTL conditions encode required-held (bit = 1 << slot)
/// and required-not-held (bit = 1 << (slot + 15)) inputs for the 14 ordinary
/// profile slots. This is deliberately not equality or a subset test.
[[nodiscard]] bool ctl_condition_matches(std::uint32_t authored, std::uint32_t current);

/// Runtime's crossing-aware transition-window predicate. Window values are
/// animation phase/frame values, not milliseconds or percentages.
[[nodiscard]] bool ctl_timing_matches(float previous, float current, float start, float end);

/// Mutable adventure CTL controller (Runtime initialization family
/// 0x0045A700/0x0045A920, update 0x004A8160). One instance is owned by the
/// RuntimeCharacter it drives; the immutable CtlControlSet bank is shared.
///
/// The controller exists and holds a current move/state while disabled;
/// Character::RuntimeCharacter::controller_enabled (compact 0x68/0x69) gates
/// whether it is serviced, so a cinematic can run over an already-initialized
/// adventure controller.
class CtlController {
 public:
  /// Independent flag filters and the timing switch of the recovered
  /// transition evaluator (Runtime 0x004A8BD0). A filter of -1 is disabled;
  /// otherwise a candidate requires candidate.flags & filter to be nonzero.
  struct TransitionQuery {
    std::int32_t required_flags_a{-1};
    std::int32_t required_flags_b{-1};
    bool check_timing{true};
  };

  /// One fired one-shot animation audio marker, drained by the world owner
  /// which resolves the hID through the scenario SCX sound table.
  struct SoundMarkerEvent {
    std::uint16_t sound_hid{0};
  };

  /// Creates a controller on a shared immutable bank: mutable state is
  /// cleared, canonical input is seeded with the no-input sentinel, the
  /// default move (move.flags & 1) and its default state (state.flags & 0x20)
  /// are selected and logically activated at phase 1. No character pose is
  /// touched: presentation is deferred until the first enabled service.
  [[nodiscard]] static std::expected<CtlController, std::string> create(
      std::shared_ptr<const Omikron::CtlControlSet> bank, std::string resource_name);

  CtlController(CtlController&&) = default;
  CtlController& operator=(CtlController&&) = default;
  CtlController(const CtlController&) = delete;
  CtlController& operator=(const CtlController&) = delete;
  ~CtlController() = default;

  /// Compact opcode 0x3F (Runtime chain 0x0041B6F0 -> 0x0046ACE0 ->
  /// 0x0045A630): exact authored move-ID lookup, input-history/transient
  /// reset, no-input reseed, restart phase 1 and logical activation of the
  /// move's default child. Works whether the controller is enabled or not;
  /// the operand is a move ID, never a state ID.
  [[nodiscard]] std::expected<void, std::string> select_move(std::uint32_t move_id);

  /// Recovered player direct-control flags (native 0x81 family). Direct
  /// control forces the same-state restart count to zero; the autonomous
  /// idle suppression bit (0x80) suppresses MDSTAND wait diversion.
  void set_direct_control_active(bool active);
  void set_autonomous_idle_suppressed(bool suppressed);
  /// Convenience for the normal player path: native bits 0x81 together.
  void set_player_direct_control(bool active);
  /// Controller flag 0x00000400: priority-based candidate selection.
  void set_priority_mode(bool enabled);
  void set_priority_threshold(std::uint16_t threshold);
  /// Controller/input flag 0x08: swaps profile slots 0 and 1.
  void set_swap_turn_slots(bool enabled);

  /// Adds one mask to the fixed 20-entry transition/input suppression set.
  /// Kept separate from the 16-entry canonical input history.
  void add_input_suppression(std::uint32_t mask);

  /// Advances the controller in the recovered 30 Hz logical domain using a
  /// deterministic accumulator; display rate never drives animation speed.
  /// `profile_input` carries the raw 14 profile slot bits. Sound marker
  /// events accumulate and are drained by the caller afterwards.
  void service(float delta_seconds, std::uint32_t profile_input, RuntimeCharacter& character);

  /// Runtime 0x004A8BD0 against the controller's current state. Timing uses
  /// the exact interval most recently traversed by the active animation.
  [[nodiscard]] const Omikron::CtlState* evaluate_transition(std::uint32_t current_input,
      float previous_progress,
      float current_progress,
      const TransitionQuery& query) const;

  /// Dispatches one deferred callback by name (Runtime 0x0045D0E0 queue
  /// consumer). Unknown names log once and remain nonfatal.
  void dispatch_callback(std::string_view name, RuntimeCharacter& character);

  [[nodiscard]] std::vector<SoundMarkerEvent> take_sound_marker_events();

  // --- Diagnostics ---------------------------------------------------------
  [[nodiscard]] std::string_view resource_name() const {
    return m_resource_name;
  }
  [[nodiscard]] const Omikron::CtlMove* current_move() const {
    return m_current_move;
  }
  [[nodiscard]] const Omikron::CtlState* current_state() const {
    return m_current_state;
  }
  [[nodiscard]] float previous_progress() const {
    return m_previous_progress;
  }
  [[nodiscard]] float current_progress() const {
    return m_current_progress;
  }
  [[nodiscard]] float effective_animation_end() const {
    return m_effective_end;
  }
  [[nodiscard]] std::uint32_t current_input() const {
    return m_current_input;
  }
  [[nodiscard]] std::span<const std::uint32_t> input_history() const {
    return {m_input_history.data(), m_history_count};
  }
  [[nodiscard]] std::size_t suppression_count() const {
    return m_suppression_count;
  }
  [[nodiscard]] bool transition_pending() const {
    return m_pending_transition.has_value();
  }
  [[nodiscard]] std::uint32_t pending_ticks() const {
    return m_pending_transition.has_value() ? m_pending_transition->ticks : 0U;
  }
  [[nodiscard]] std::size_t callback_queue_size() const {
    return m_callback_queue.size();
  }
  [[nodiscard]] std::uint32_t same_state_restart_count() const {
    return m_same_state_restart_count;
  }
  [[nodiscard]] std::uint32_t walk_restart_snapshot() const {
    return m_walk_restart_snapshot;
  }
  [[nodiscard]] std::uint32_t run_restart_snapshot() const {
    return m_run_restart_snapshot;
  }
  [[nodiscard]] std::uint16_t input_profile() const {
    return m_input_profile;
  }
  [[nodiscard]] bool direct_control_active() const {
    return m_direct_control_active;
  }
  [[nodiscard]] bool autonomous_idle_suppressed() const {
    return m_autonomous_idle_suppressed;
  }
  [[nodiscard]] const App::Runtime::Vec3& candidate_translation() const {
    return m_candidate_translation;
  }
  [[nodiscard]] const App::Runtime::Vec3& accepted_translation() const {
    return m_accepted_translation;
  }
  [[nodiscard]] std::size_t markers_fired_this_execution() const;

 private:
  struct PendingTransition {
    const Omikron::CtlState* candidate{nullptr};
    std::uint32_t ticks{0};
  };

  explicit CtlController(
      std::shared_ptr<const Omikron::CtlControlSet> bank, std::string resource_name);

  /// Runtime 0x0045A9A0: fixed 16-entry history reset, seeding the canonical
  /// no-input sentinel as the only entry.
  void reset_input_history();
  /// Runtime 0x004A7B80: central state activation. Same-state restart
  /// bookkeeping, animation install, phase seeding, callback queueing and
  /// per-execution marker reset. With a null character the presentation half
  /// is deferred to the first enabled service.
  void activate_state(const Omikron::CtlState& state, float phase, RuntimeCharacter* character);
  /// Installs the current state's authored animation as the character's base
  /// pose (fresh instance-local hierarchy from immutable model defaults,
  /// channels matched by MeshDescriptor::script_id) and applies the authored
  /// auxiliary orientation/local-movement blocks.
  void present_current_state(RuntimeCharacter& character);
  /// Samples object rotations at `phase`, resolves runtime transforms and
  /// rebuilds posed geometry.
  [[nodiscard]] static std::expected<void, std::string> apply_animation_pose(
      RuntimeCharacter& character, const Omikron::Animation3DA& animation, float phase);
  /// One logical 30 Hz controller tick.
  void tick_once(std::uint32_t profile_input, RuntimeCharacter& character);
  /// Services one-shot animation markers crossed by [previous, current].
  void service_audio_markers(const Omikron::CtlState& state, float previous, float current);
  void drain_callbacks(RuntimeCharacter& character);

  static constexpr float K_LOGIC_STEP_SECONDS{1.0F / 30.0F};
  static constexpr std::size_t K_INPUT_HISTORY_CAPACITY{16U};
  static constexpr std::size_t K_SUPPRESSION_CAPACITY{20U};
  static constexpr std::size_t K_CALLBACK_CAPACITY{10U};

  std::shared_ptr<const Omikron::CtlControlSet> m_bank;
  std::string m_resource_name;

  const Omikron::CtlMove* m_current_move{nullptr};
  const Omikron::CtlState* m_current_state{nullptr};
  const Omikron::Animation3DA* m_animation{nullptr};
  float m_previous_progress{1.0F};
  float m_current_progress{1.0F};
  float m_effective_end{0.0F};

  std::uint32_t m_current_input{K_CTL_NO_INPUT};
  std::array<std::uint32_t, K_INPUT_HISTORY_CAPACITY> m_input_history{};
  std::size_t m_history_count{0};
  std::array<std::uint32_t, K_SUPPRESSION_CAPACITY> m_suppression{};
  std::size_t m_suppression_count{0};
  std::optional<PendingTransition> m_pending_transition;
  std::uint32_t m_same_state_restart_count{0};
  std::uint16_t m_priority_threshold{0xFFFFU};

  bool m_direct_control_active{false};
  bool m_autonomous_idle_suppressed{false};
  bool m_priority_mode{false};
  bool m_swap_turn_slots{false};
  std::uint16_t m_input_profile{0};

  std::deque<std::string> m_callback_queue;
  std::uint32_t m_walk_restart_snapshot{0};
  std::uint32_t m_run_restart_snapshot{0};
  /// MDSTAND wait-move alternation; the initial selection is the first move.
  bool m_wait_alternation{false};

  /// Controller-motion position model (Phase 4.1): root motion updates the
  /// candidate full XYZ; Phase 4.2 inserts collision/floor resolution before
  /// acceptance. Both anchor from the live actor transform when controller
  /// ownership starts.
  App::Runtime::Vec3 m_candidate_translation{};
  App::Runtime::Vec3 m_accepted_translation{};
  bool m_motion_anchor_initialized{false};

  const Omikron::CtlState* m_presented_state{nullptr};
  bool m_presentation_pending{false};
  std::vector<bool> m_marker_fired;
  std::vector<SoundMarkerEvent> m_sound_events;
  float m_accumulator_seconds{0.0F};
  std::unordered_set<std::string> m_warned_callbacks;
  bool m_segmented_animation_warned{false};
};

}  // namespace App::Character
