#include "Core/Character/CtlController.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/Animation3DA.hpp"
#include "Core/Omikron/CtlControlSet.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Character {

namespace {

/// Candidate flag switching input matching to exact equality.
constexpr std::uint32_t K_STATE_EXACT_INPUT_FLAG{0x00080000U};
/// Current-state flag enabling the owner-move fallback scan.
constexpr std::uint32_t K_STATE_OWNER_FALLBACK_FLAG{0x00200000U};
/// Required flag on fallback candidates of the owner-move scan.
constexpr std::uint32_t K_STATE_FALLBACK_CANDIDATE_FLAG{0x00400000U};
/// Current-state flag reversing child-reference traversal order.
constexpr std::uint16_t K_STATE_REVERSE_TRAVERSAL_MODE{0x0020U};
/// State flags selecting the persistent/restart end family.
constexpr std::uint32_t K_STATE_PERSISTENT_FAMILY_MASK{0x00008001U};
/// animation_mode high nibble: packed/segmented sampler segment count.
constexpr std::uint32_t K_ANIMATION_SEGMENT_SHIFT{12U};
/// animation_mode bit appending the dynamic audio-marker block.
constexpr std::uint16_t K_ANIMATION_MODE_MARKERS{0x0008U};

/// Recovered callback move IDs. These numeric IDs are part of the callbacks'
/// own recovered native behavior (0x0046C070/0x0046C0E0/0x0046BF90), not
/// generic controller constants.
constexpr std::uint32_t K_CALLBACK_MOVE_RUN_BREATHE{164U};
constexpr std::uint32_t K_CALLBACK_MOVE_WALK_BREATHE{166U};
constexpr std::uint32_t K_CALLBACK_MOVE_WAIT_SHORT{43U};
constexpr std::uint32_t K_CALLBACK_MOVE_WAIT_LONG{44U};

/// MDSTOPR/MDSTOPW autonomous-diversion restart threshold.
constexpr std::uint32_t K_RUN_SNAPSHOT_THRESHOLD{30U};
/// MDSTOPW walk-snapshot guard.
constexpr std::uint32_t K_WALK_SNAPSHOT_LIMIT{3U};
/// MDSTAND restart count above which autonomous wait moves are selected.
constexpr std::uint32_t K_STAND_RESTART_THRESHOLD{10U};

/// Wraps one Euler-degree component the way Runtime's angle updates do:
/// periodic 360-degree space represented as the signed shortest turn.
[[nodiscard]] float wrap_degrees(const float degrees) {
  return std::remainder(degrees, 360.0F);
}

}  // namespace

bool ctl_condition_matches(const std::uint32_t authored, const std::uint32_t current) {
  for (std::uint32_t slot{0}; slot < K_CTL_PROFILE_SLOT_COUNT; ++slot) {
    const std::uint32_t positive{1U << slot};
    const std::uint32_t negative{1U << (slot + 15U)};
    if ((authored & negative) != 0U && (current & positive) != 0U) {
      return false;
    }
  }
  if ((((authored ^ current) & authored) & 0x00007FFFU) != 0U) {
    return false;
  }
  if (authored == K_CTL_CONDITION_SPECIAL) {
    return current <= 0x00002000U;
  }
  return true;
}

bool ctl_timing_matches(
    const float previous, const float current, const float start, const float end) {
  if (start == 0.0F && end == 0.0F) {
    return true;
  }
  if (current < start) {
    return false;
  }
  if (previous > end) {
    return false;
  }
  if (previous >= start) {
    return true;
  }
  return current >= end;
}

std::expected<CtlController, std::string> CtlController::create(
    std::shared_ptr<const Omikron::CtlControlSet> bank, std::string resource_name) {
  if (bank == nullptr) {
    return std::expected<CtlController, std::string>{
        std::unexpect, "cannot create a CTL controller without a control-set bank"};
  }
  CtlController controller{std::move(bank), std::move(resource_name)};
  const Omikron::CtlMove* const default_move{controller.m_bank->default_move()};
  if (default_move == nullptr) {
    return std::expected<CtlController, std::string>{std::unexpect,
        fmt::format("CTL control set '{}' has no default move (flags & 1)",
            controller.m_resource_name)};
  }
  const Omikron::CtlState* const default_state{Omikron::CtlControlSet::default_state(*default_move)};
  if (default_state == nullptr) {
    return std::expected<CtlController, std::string>{std::unexpect,
        fmt::format("CTL control set '{}' default move {} has no default state (flags & 0x20)",
            controller.m_resource_name,
            default_move->move_id)};
  }
  controller.m_current_move = default_move;
  controller.activate_state(*default_state, 1.0F, nullptr);
  return controller;
}

CtlController::CtlController(std::shared_ptr<const Omikron::CtlControlSet> bank,
    std::string resource_name)
    : m_bank{std::move(bank)}, m_resource_name{std::move(resource_name)} {
  reset_input_history();
}

void CtlController::reset_input_history() {
  m_input_history.fill(0U);
  m_input_history.at(0) = K_CTL_NO_INPUT;
  m_current_input = K_CTL_NO_INPUT;
  m_history_count = 1U;
}

std::expected<void, std::string> CtlController::select_move(const std::uint32_t move_id) {
  const Omikron::CtlMove* const move{m_bank->move_by_id(move_id)};
  if (move == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("CTL control set '{}' has no move ID {}", m_resource_name, move_id)};
  }
  const Omikron::CtlState* const state{Omikron::CtlControlSet::default_state(*move)};
  if (state == nullptr) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("CTL control set '{}' move {} has no default state (flags & 0x20)",
            m_resource_name,
            move_id)};
  }

  // Runtime 0x0045A630: transient transition state, input history and the
  // canonical input are reset before the new move's default child activates.
  reset_input_history();
  m_suppression_count = 0U;
  m_pending_transition.reset();
  m_current_move = move;
  activate_state(*state, 1.0F, nullptr);
  return {};
}

void CtlController::set_direct_control_active(const bool active) {
  m_direct_control_active = active;
}

void CtlController::set_autonomous_idle_suppressed(const bool suppressed) {
  m_autonomous_idle_suppressed = suppressed;
}

void CtlController::set_player_direct_control(const bool active) {
  m_direct_control_active = active;
  m_autonomous_idle_suppressed = active;
}

void CtlController::set_priority_mode(const bool enabled) {
  m_priority_mode = enabled;
}

void CtlController::set_priority_threshold(const std::uint16_t threshold) {
  m_priority_threshold = threshold;
}

void CtlController::set_swap_turn_slots(const bool enabled) {
  m_swap_turn_slots = enabled;
}

void CtlController::add_input_suppression(const std::uint32_t mask) {
  if (mask == 0U || m_suppression_count >= m_suppression.size()) {
    return;
  }
  m_suppression.at(m_suppression_count) = mask;
  ++m_suppression_count;
}

const Omikron::CtlState* CtlController::evaluate_transition(const std::uint32_t current_input,
    const float previous_progress,
    const float current_progress,
    const TransitionQuery& query) const {
  if (m_current_state == nullptr) {
    return nullptr;
  }

  const auto candidate_matches =
      [current_input, previous_progress, current_progress, &query](
          const Omikron::CtlState& candidate) {
        if (query.required_flags_a != -1 &&
            (candidate.flags & static_cast<std::uint32_t>(query.required_flags_a)) == 0U) {
          return false;
        }
        if (query.required_flags_b != -1 &&
            (candidate.flags & static_cast<std::uint32_t>(query.required_flags_b)) == 0U) {
          return false;
        }
        if ((candidate.flags & K_STATE_EXACT_INPUT_FLAG) != 0U) {
          if (candidate.input_condition != current_input) {
            return false;
          }
        } else if (!ctl_condition_matches(candidate.input_condition, current_input)) {
          return false;
        }
        if (query.check_timing &&
            !ctl_timing_matches(previous_progress,
                current_progress,
                candidate.window_start,
                candidate.window_end)) {
          return false;
        }
        return true;
      };

  const auto select = [this, &candidate_matches](
                          const std::vector<const Omikron::CtlState*>& candidates,
                          const bool reverse) -> const Omikron::CtlState* {
    const Omikron::CtlState* best{nullptr};
    const auto visit = [this, &candidate_matches, &best](const Omikron::CtlState* candidate) {
      if (candidate == nullptr || !candidate_matches(*candidate)) {
        return false;
      }
      if (!m_priority_mode) {
        best = candidate;
        return true;
      }
      if (candidate->priority > m_priority_threshold) {
        return false;
      }
      if (candidate->priority == m_priority_threshold) {
        best = candidate;
        return true;
      }
      if (best == nullptr || candidate->priority > best->priority) {
        best = candidate;
      }
      return false;
    };
    if (reverse) {
      for (auto cursor{candidates.rbegin()}; cursor != candidates.rend(); ++cursor) {
        if (visit(*cursor)) {
          break;
        }
      }
    } else {
      for (const Omikron::CtlState* const candidate : candidates) {
        if (visit(candidate)) {
          break;
        }
      }
    }
    return best;
  };

  const bool reverse{(m_current_state->animation_mode & K_STATE_REVERSE_TRAVERSAL_MODE) != 0U};
  if (const Omikron::CtlState* const selected{select(m_current_state->child_refs, reverse)};
      selected != nullptr) {
    return selected;
  }

  if ((m_current_state->flags & K_STATE_OWNER_FALLBACK_FLAG) != 0U &&
      m_current_state->owner_move != nullptr) {
    const auto fallback_candidates = [&candidate_matches](const Omikron::CtlState* candidate) {
      return candidate != nullptr && (candidate->flags & K_STATE_FALLBACK_CANDIDATE_FLAG) != 0U &&
             candidate_matches(*candidate);
    };
    const Omikron::CtlState* best{nullptr};
    for (const Omikron::CtlState* const candidate : m_current_state->owner_move->states) {
      if (!fallback_candidates(candidate)) {
        continue;
      }
      if (!m_priority_mode) {
        return candidate;
      }
      if (candidate->priority > m_priority_threshold) {
        continue;
      }
      if (candidate->priority == m_priority_threshold) {
        return candidate;
      }
      if (best == nullptr || candidate->priority > best->priority) {
        best = candidate;
      }
    }
    return best;
  }
  return nullptr;
}

void CtlController::service(const float delta_seconds,
    const std::uint32_t profile_input,
    RuntimeCharacter& character) {
  m_sound_events.clear();
  if (!m_motion_anchor_initialized) {
    m_candidate_translation = character.transform.translation;
    m_accepted_translation = character.transform.translation;
    m_motion_anchor_initialized = true;
  } else if (character.transform.translation.x != m_accepted_translation.x ||
             character.transform.translation.y != m_accepted_translation.y ||
             character.transform.translation.z != m_accepted_translation.z) {
    // An external system (address placement, transition, script) moved the
    // actor since the last controller service: re-anchor the motion model
    // instead of snapping the actor back to the stale candidate.
    m_candidate_translation = character.transform.translation;
    m_accepted_translation = character.transform.translation;
  }

  m_accumulator_seconds += std::max(delta_seconds, 0.0F);
  while (m_accumulator_seconds >= K_LOGIC_STEP_SECONDS) {
    m_accumulator_seconds -= K_LOGIC_STEP_SECONDS;
    tick_once(profile_input, character);
  }

  drain_callbacks(character);

  // Phase 4.1 accepts the controller candidate position directly. Phase 4.2
  // inserts gravity/collision/floor resolution between candidate and
  // accepted without redesigning this controller.
  m_accepted_translation = m_candidate_translation;
  character.transform.translation = m_accepted_translation;
}

void CtlController::tick_once(const std::uint32_t profile_input, RuntimeCharacter& character) {
  if (m_current_state == nullptr) {
    return;
  }

  if (m_presentation_pending) {
    present_current_state(character);
    m_presentation_pending = false;
  }

  std::uint32_t input{ctl_canonical_input(profile_input)};
  if (m_swap_turn_slots) {
    input = (input & ~0x3U) | ((input & 0x1U) << 1U) | ((input >> 1U) & 0x1U);
  }

  // Transition/input suppression set: an entry whose authored condition still
  // matches removes its bits from the current input; an entry that no longer
  // matches is removed from the set.
  for (std::size_t index{0}; index < m_suppression_count;) {
    const std::uint32_t mask{m_suppression.at(index)};
    if (ctl_condition_matches(mask, input)) {
      input &= ~mask;
      ++index;
    } else {
      --m_suppression_count;
      m_suppression.at(index) = m_suppression.at(m_suppression_count);
      m_suppression.at(m_suppression_count) = 0U;
    }
  }
  // A fully suppressed profile is the canonical no-input state again.
  if (input == 0U) {
    input = K_CTL_NO_INPUT;
  }

  // The controller records canonical input changes rather than appending the
  // same mask forever.
  if (input != m_current_input) {
    for (std::size_t index{m_input_history.size() - 1U}; index > 0U; --index) {
      m_input_history.at(index) = m_input_history.at(index - 1U);
    }
    m_input_history.at(0) = input;
    m_history_count = std::min(m_history_count + 1U, m_input_history.size());
    m_current_input = input;
  }

  // A deferred candidate only counts controller-service ticks; the transition
  // fires once the counter passes the authored defer count.
  if (m_pending_transition.has_value()) {
    PendingTransition& pending{m_pending_transition.value()};
    ++pending.ticks;
    if (pending.ticks > pending.candidate->defer_ticks) {
      const Omikron::CtlState* const target{pending.candidate};
      m_pending_transition.reset();
      activate_state(*target, 1.0F, &character);
    }
    return;
  }

  // Runtime 0x0045C680: one logical animation phase per controller tick.
  m_previous_progress = m_current_progress;
  m_current_progress += 1.0F;

  service_audio_markers(*m_current_state, m_previous_progress, m_current_progress);

  if (m_animation != nullptr) {
    if (auto applied{apply_animation_pose(character, *m_animation, m_current_progress)}; !applied) {
      App::Log::warn(LogCategory::Scenario,
          "CTL '{}' state {} pose: {}",
          m_resource_name,
          m_current_state->state_id,
          applied.error());
    }

    // Animation-driven root motion: interval increments through the live
    // actor orientation into the candidate position. Sample 0 is a reference
    // value and never anchors the actor.
    const Omikron::Model3DOData& model{character.model_resource->model};
    if (model.root_mesh_index >= 0 &&
        std::cmp_less(model.root_mesh_index, character.object_poses.size())) {
      const BodyAnimationObjectPose& root_pose{
          character.object_poses.at(static_cast<std::size_t>(model.root_mesh_index))};
      if (root_pose.channel_index.has_value()) {
        const Omikron::Animation3DAChannel& root_channel{
            m_animation->channels.at(root_pose.channel_index.value())};
        if (const std::optional<App::Runtime::Vec3> local_delta{root_channel.integrate_translation(
                m_previous_progress, m_current_progress)};
            local_delta.has_value()) {
          const App::Runtime::Vec3 world_delta{
              App::Runtime::transform_vector(local_delta.value(), character.live_root_orientation())};
          m_candidate_translation.x += world_delta.x;
          m_candidate_translation.y += world_delta.y;
          m_candidate_translation.z += world_delta.z;
        }
      }
    }
  }

  const Omikron::CtlState* const candidate{
      evaluate_transition(m_current_input, m_previous_progress, m_current_progress, {})};
  if (candidate != nullptr) {
    if (candidate->defer_ticks != 0U) {
      m_pending_transition = PendingTransition{.candidate = candidate, .ticks = 1U};
    } else {
      activate_state(*candidate, 1.0F, &character);
    }
    return;
  }

  if (m_animation == nullptr || m_current_progress < m_effective_end) {
    return;
  }

  // State end behavior: the persistent family re-enters itself at phase 1
  // (this is how standing/walk/run loop — there is no 3DA loop boolean); the
  // end/goto family follows the authored goto with its transition value.
  if ((m_current_state->flags & K_STATE_PERSISTENT_FAMILY_MASK) == 0U) {
    activate_state(*m_current_state, 1.0F, &character);
  } else if (m_current_state->goto_state != nullptr) {
    activate_state(*m_current_state->goto_state, m_current_state->transition_value, &character);
  } else {
    App::Log::warn(LogCategory::Scenario,
        "CTL '{}' state {} ended without a goto target; re-entering at phase 1",
        m_resource_name,
        m_current_state->state_id);
    activate_state(*m_current_state, 1.0F, &character);
  }
}

void CtlController::activate_state(const Omikron::CtlState& state,
    const float phase,
    RuntimeCharacter* const character) {
  if (m_current_state == &state) {
    ++m_same_state_restart_count;
  } else {
    m_same_state_restart_count = 0U;
  }
  // Native direct-control flags 0x81 force the restart count to zero.
  if (m_direct_control_active && m_autonomous_idle_suppressed) {
    m_same_state_restart_count = 0U;
  }

  m_current_state = &state;
  m_previous_progress = phase;
  m_current_progress = phase;
  m_animation = state.animation;
  m_effective_end =
      state.animation != nullptr ? static_cast<float>(state.animation->max_frame_index) : 0.0F;
  m_marker_fired.assign(state.audio_markers.size(), false);

  if ((state.flags & 0x10U) != 0U && !state.callback_name.empty()) {
    if (m_callback_queue.size() >= K_CALLBACK_CAPACITY) {
      App::Log::warn(LogCategory::Scenario,
          "CTL '{}' callback queue is full; dropping '{}'",
          m_resource_name,
          state.callback_name);
    } else {
      m_callback_queue.push_back(state.callback_name);
    }
  }

  if (character != nullptr) {
    present_current_state(*character);
    m_presentation_pending = false;
  } else {
    // Logical-only activation (construction, compact 0x3F): no cinematic pose
    // may be overwritten. The first enabled service presents the state.
    m_presentation_pending = true;
  }
}

void CtlController::present_current_state(RuntimeCharacter& character) {
  const Omikron::CtlState& state{*m_current_state};
  m_presented_state = m_current_state;
  // The analyzer cannot see that tick_once already guarantees a model; keep
  // the invariant explicit for the presentation path.
  if (character.model_resource == nullptr) {
    App::Log::warn(LogCategory::Scenario,
        "CTL '{}' state {} cannot present without a character model",
        m_resource_name,
        state.state_id);
    return;
  }
  const Omikron::Model3DOData& model{character.model_resource->model};

  if (m_animation != nullptr) {
    const std::uint32_t segment_count{
        static_cast<std::uint32_t>(state.animation_mode) >> K_ANIMATION_SEGMENT_SHIFT};
    if (segment_count > 1U) {
      // Packed/segmented samplers (0x6xxx/0x9xxx modes) belong to advanced
      // interaction states outside Phase 4.1. Fail safely: keep the current
      // pose and treat the state as transition-only.
      if (!m_segmented_animation_warned) {
        m_segmented_animation_warned = true;
        App::Log::error(LogCategory::Scenario,
            "CTL '{}' state {} uses unsupported segmented animation mode {:#06x}",
            m_resource_name,
            state.state_id,
            state.animation_mode);
      }
      m_animation = nullptr;
      m_effective_end = 0.0F;
    } else {
      // Fresh instance-local pose hierarchy from the immutable model
      // defaults; channels map to model objects by script_id, never by
      // vector index or mesh_id.
      character.runtime_objects = model.runtime_objects;
      character.object_poses.assign(model.meshes.size(), BodyAnimationObjectPose{});
      for (std::size_t object_index{0}; object_index < model.meshes.size(); ++object_index) {
        const std::uint32_t script_id{model.meshes.at(object_index).script_id};
        for (std::size_t channel_index{0}; channel_index < m_animation->channels.size();
            ++channel_index) {
          const Omikron::Animation3DAChannel& channel{m_animation->channels.at(channel_index)};
          if (channel.channel_id != script_id) {
            continue;
          }
          BodyAnimationObjectPose& pose{character.object_poses.at(object_index)};
          pose.channel_index = static_cast<std::uint32_t>(channel_index);
          pose.channel_id = channel.channel_id;
          pose.channel_name = channel.name;
          break;
        }
      }
      if (auto applied{apply_animation_pose(character, *m_animation, m_current_progress)};
          !applied) {
        App::Log::warn(LogCategory::Scenario,
            "CTL '{}' state {} pose: {}",
            m_resource_name,
            state.state_id,
            applied.error());
      }
    }
  }

  // Authored auxiliary blocks (Runtime helpers 0x0045C080/0x0045C1B0 and
  // 0x0045C2F0). Orientation applies before local movement so the movement
  // transforms through the updated live orientation.
  if (state.orientation_block.has_value()) {
    const App::Runtime::Vec3& delta{state.orientation_block->orientation_delta};
    const App::Runtime::Vec3 current{character.principal_orientation_degrees};
    character.set_principal_orientation(App::Runtime::Vec3{.x = wrap_degrees(current.x + delta.x),
        .y = wrap_degrees(current.y + delta.y),
        .z = wrap_degrees(current.z + delta.z)});
  }
  if (state.movement_block.has_value()) {
    const App::Runtime::Vec3 world_delta{App::Runtime::transform_vector(
        state.movement_block->local_delta, character.live_root_orientation())};
    m_candidate_translation.x += world_delta.x;
    m_candidate_translation.y += world_delta.y;
    m_candidate_translation.z += world_delta.z;
  }
}

std::expected<void, std::string> CtlController::apply_animation_pose(
    RuntimeCharacter& character, const Omikron::Animation3DA& animation, const float phase) {
  const Omikron::Model3DOData& model{character.model_resource->model};
  const float clamped_phase{std::clamp(phase, 0.0F, static_cast<float>(animation.max_frame_index))};
  for (std::size_t object_index{0}; object_index < character.object_poses.size(); ++object_index) {
    BodyAnimationObjectPose& pose{character.object_poses.at(object_index)};
    if (!pose.channel_index.has_value()) {
      continue;
    }
    const Omikron::Animation3DAChannel& channel{animation.channels.at(pose.channel_index.value())};
    if (const std::optional<App::Runtime::Quaternion> rotation{channel.sample_rotation(clamped_phase)};
        rotation.has_value()) {
      pose.current_quaternion = rotation.value();
      character.runtime_objects.at(object_index).animation_matrix =
          App::Runtime::quaternion_matrix(rotation.value());
    }
  }

  if (auto resolved{Omikron::Model3DO::resolve_runtime_transforms(
          model, std::span{character.runtime_objects})};
      !resolved) {
    return std::expected<void, std::string>{std::unexpect, std::move(resolved).error()};
  }
  auto groups{Omikron::Model3DO::build_posed_geometry(
      model, std::span<const Omikron::Model3DOData::RuntimeObjectState>{character.runtime_objects})};
  if (!groups) {
    return std::expected<void, std::string>{std::unexpect, std::move(groups).error()};
  }
  character.posed_groups = std::move(groups).value();
  character.pose_revision += 1U;
  character.pose_owner = PoseOwner::k_ctl_controller;
  return {};
}

void CtlController::service_audio_markers(const Omikron::CtlState& state,
    const float previous,
    const float current) {
  if ((state.animation_mode & K_ANIMATION_MODE_MARKERS) == 0U) {
    return;
  }
  const std::size_t count{std::min(state.audio_markers.size(), m_marker_fired.size())};
  for (std::size_t index{0}; index < count; ++index) {
    if (m_marker_fired.at(index)) {
      continue;
    }
    const Omikron::CtlAudioMarker& marker{state.audio_markers.at(index)};
    // Ordinary one-shot locomotion mode: fire once per state execution when
    // the current animation interval crosses the authored phase.
    if (previous < marker.one_shot_phase && current >= marker.one_shot_phase) {
      m_marker_fired.at(index) = true;
      m_sound_events.push_back(SoundMarkerEvent{.sound_hid = marker.one_shot_sound_hid});
    }
  }
}

std::vector<CtlController::SoundMarkerEvent> CtlController::take_sound_marker_events() {
  std::vector<SoundMarkerEvent> events;
  events.swap(m_sound_events);
  return events;
}

std::size_t CtlController::markers_fired_this_execution() const {
  return static_cast<std::size_t>(std::ranges::count(m_marker_fired, true));
}

void CtlController::drain_callbacks(RuntimeCharacter& character) {
  while (!m_callback_queue.empty()) {
    const std::string name{std::move(m_callback_queue.front())};
    m_callback_queue.pop_front();
    dispatch_callback(name, character);
  }
}

void CtlController::dispatch_callback(const std::string_view name, RuntimeCharacter& character) {
  const auto switch_move = [this, &name](const std::uint32_t move_id) {
    if (auto selected{select_move(move_id)}; !selected) {
      App::Log::warn(LogCategory::Scenario,
          "CTL '{}' callback '{}': {}",
          m_resource_name,
          name,
          selected.error());
    }
  };

  if (name == "MDWALK") {
    m_walk_restart_snapshot = m_same_state_restart_count;
  } else if (name == "MDRUN") {
    m_run_restart_snapshot = m_same_state_restart_count;
  } else if (name == "MDSTOPR") {
    if (m_run_restart_snapshot > K_RUN_SNAPSHOT_THRESHOLD) {
      switch_move(K_CALLBACK_MOVE_RUN_BREATHE);
    }
    m_run_restart_snapshot = 0U;
  } else if (name == "MDSTOPW") {
    if (m_run_restart_snapshot > K_RUN_SNAPSHOT_THRESHOLD &&
        m_walk_restart_snapshot < K_WALK_SNAPSHOT_LIMIT) {
      switch_move(K_CALLBACK_MOVE_WALK_BREATHE);
    }
    m_run_restart_snapshot = 0U;
    m_walk_restart_snapshot = 0U;
  } else if (name == "MDSTAND") {
    // Direct-control bit 0x80 suppresses the autonomous wait diversion; the
    // restart count is the only inactivity measure — there is no timer.
    if (!m_autonomous_idle_suppressed && character.adventure_mode == 1 &&
        m_same_state_restart_count > K_STAND_RESTART_THRESHOLD) {
      switch_move(m_wait_alternation ? K_CALLBACK_MOVE_WAIT_LONG : K_CALLBACK_MOVE_WAIT_SHORT);
      m_wait_alternation = !m_wait_alternation;
    }
    m_run_restart_snapshot = 0U;
    m_walk_restart_snapshot = 0U;
  } else if (name == "MDROT000") {
    // 0x0046C170 does NOT rotate; it suppresses the physical stage's
    // automatic movement-heading rewrite so authored CTL turn states keep
    // control of the heading.
    character.suppress_automatic_movement_heading = true;
  } else if (name == "RSTAVNT") {
    character.adventure_mode = 1;
    const App::Runtime::Vec3 orientation{character.principal_orientation_degrees};
    // Pitch resets; yaw deliberately survives.
    character.set_principal_orientation(
        App::Runtime::Vec3{.x = 0.0F, .y = orientation.y, .z = orientation.z});
    // Input profile 0; selecting a profile reseeds the canonical no-input
    // history.
    m_input_profile = 0U;
    reset_input_history();
  } else if (m_warned_callbacks.emplace(name).second) {
    App::Log::warn(LogCategory::Scenario,
        "CTL '{}' has no implementation for callback '{}' (nonfatal)",
        m_resource_name,
        name);
  }
}

}  // namespace App::Character
