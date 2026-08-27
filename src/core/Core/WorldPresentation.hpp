#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>

#include "Core/Interface/RuntimeText.hpp"
#include "Core/RuntimeMath.hpp"

namespace App {

/// Session-lifetime copy of Runtime's global cyclic 3DO texture phases.
///
/// Runtime advances both globals by 0.0004 per nominal 30 Hz tick. Expressing
/// the same rate in seconds keeps presentation deterministic and independent
/// of display refresh. WorldScene owns this state so decor renderer rebuilds
/// do not restart the phases.
class WorldUvPhaseState {
 public:
  static constexpr double k_periods_per_second{0.012};

  void update(const float delta_seconds) {
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0F) {
      return;
    }
    const double increment{static_cast<double>(delta_seconds) * k_periods_per_second};
    m_u_phase = std::fmod(m_u_phase + increment, 1.0);
    m_v_phase = std::fmod(m_v_phase + increment, 1.0);
  }

  [[nodiscard]] double u_phase() const {
    return m_u_phase;
  }

  [[nodiscard]] double v_phase() const {
    return m_v_phase;
  }

 private:
  double m_u_phase{0.0};
  double m_v_phase{0.0};
};

/// One requested live Runtime character attachment pose supplied across the
/// narrow gameplay-to-presentation capability boundary.
struct WorldCameraAttachmentPose {
  Runtime::Vec3 translation{};
  /// Native +0x1A0/+0x1A4/+0x1A8 principal actor orientation. AREA 222
  /// cameras 4290/4291/4292 use selector 0 for both eye and target.
  Runtime::Matrix3 principal_orientation{};
};

/// Stable authored character IDs retained by an attached camera command.
/// Presentation resolves these identities to live poses on every update.
struct WorldCameraAttachmentParticipants {
  std::int16_t participant_a_character_id{-1};
  std::int16_t participant_b_character_id{-1};
};

enum class WorldCameraCommandKind : std::uint8_t {
  k_authored_camera,
  k_controller_mode,
};

/// One Runtime AREA camera command waiting to be consumed by the presentation
/// layer.
///
/// Serialized AREA integers are retained for diagnostics alongside normalized
/// Runtime-native positions. Scenario code never depends on GL conventions.
struct WorldCameraCommand {
  WorldCameraCommandKind kind{WorldCameraCommandKind::k_authored_camera};
  std::uint32_t scene_id{0};
  std::uint32_t scene_generation{0};
  std::uint16_t camera_id{0};
  /// Native camera-controller selector. Character-bound SCX launches switch
  /// to mode 13 after the child is started.
  std::uint16_t controller_mode{0};
  /// Owning IAM AREA, retained so cross-layer sequence diagnostics do not have
  /// to infer provenance from globally numbered camera IDs.
  std::int32_t source_area_id{-1};
  /// Unique compact-VM operation generation for tracked 0x60 requests.
  std::optional<std::uint64_t> operation_generation{};
  WorldCameraAttachmentParticipants attachment_participants{};

  std::array<std::int32_t, 3> serialized_eye{};
  std::array<std::int32_t, 3> serialized_target{};
  /// Absolute fallback vectors. IAM camera dwords are direct Runtime camera
  /// coordinates; actor-attached vectors are re-resolved every update.
  Runtime::Vec3 runtime_eye{};
  Runtime::Vec3 runtime_target{};

  /// Original AREA duration in 30 Hz scenario units.
  std::int16_t duration_units{0};
  std::int16_t flags{0};
  bool wait_for_completion{false};

  /// Confirmed camera metadata, preserving both serialized units and
  /// normalized integer degrees.
  std::uint16_t camera_type{0};
  std::int16_t roll_units{0};
  std::int16_t horizontal_fov_units{0};
  std::int32_t roll_degrees{0};
  std::int32_t horizontal_fov_degrees{0};
  /// IAM +0x20 / +0x22. -1 is absolute; 0 uses participant A's principal
  /// orientation; 6 uses the participant A/B midpoint and relationship yaw.
  /// Selectors 1..5 and 7..9 deliberately retain the safe absolute fallback.
  std::int16_t target_attachment_selector{-1};
  std::int16_t eye_attachment_selector{-1};
  std::array<std::uint16_t, 4> tail_fields{};
};

/// Completion emitted by WorldCameraSystem only when the exact tracked
/// mode-12 transition represented by `operation_generation` reaches its end.
struct WorldCameraOperationCompletion {
  std::uint64_t operation_generation{0};
  std::uint32_t scene_id{0};
  std::uint32_t scene_generation{0};
  std::int32_t source_area_id{-1};
  std::uint16_t camera_id{0};
};

/// One session-global AREA 0x76/0x77 presentation command.
///
/// Mode 1 (0x76) fades into the authored RGB colour; mode 2 (0x77) fades out
/// of it. Duration and delay use the 30 Hz AREA clock while interpolation is
/// sampled at display rate.
struct WorldFadeCommand {
  std::uint8_t mode{0};
  std::uint32_t color{0};
  std::int16_t duration_units{0};
  std::int16_t delay_units{0};
};

/// CPU-only state for asynchronous AREA 0x76/0x77 presentation fades.
class WorldFadeState {
 public:
  static constexpr float k_frames_per_second{30.0F};

  /// Applies only a supported command accepted by Runtime's global fade
  /// arbitration rules. Rejected commands leave all active state unchanged.
  [[nodiscard]] bool apply_command(const WorldFadeCommand& command) {
    if ((command.mode != 1U && command.mode != 2U) ||
        (m_mode != 0U && !(m_mode == 1U && command.mode == 2U))) {
      return false;
    }

    m_mode = command.mode;
    m_color = command.color & 0x00FFFFFFU;
    m_duration_seconds = std::abs(static_cast<float>(command.duration_units)) / k_frames_per_second;
    m_elapsed_seconds = 0.0F;
    m_remaining_delay_seconds =
        std::max(static_cast<float>(command.delay_units), 0.0F) / k_frames_per_second;
    m_alpha = command.mode == 1U ? 0.0F : 1.0F;
    if (m_remaining_delay_seconds <= 0.0F && m_duration_seconds <= 0.0F) {
      complete_active_fade();
    }
    return true;
  }

  void update(const float delta_time) {
    float remaining_delta{std::max(delta_time, 0.0F)};
    if (m_remaining_delay_seconds > 0.0F) {
      const float consumed_delay{std::min(remaining_delta, m_remaining_delay_seconds)};
      m_remaining_delay_seconds -= consumed_delay;
      remaining_delta -= consumed_delay;
      if (m_remaining_delay_seconds > 0.0F || remaining_delta <= 0.0F) {
        return;
      }
    }
    if (m_mode == 0U) {
      return;
    }
    if (m_duration_seconds <= 0.0F) {
      complete_active_fade();
      return;
    }
    if (m_elapsed_seconds >= m_duration_seconds) {
      return;
    }
    m_elapsed_seconds += remaining_delta;
    const float progress{std::clamp(m_elapsed_seconds / m_duration_seconds, 0.0F, 1.0F)};
    m_alpha = m_mode == 1U ? progress : 1.0F - progress;
    if (progress >= 1.0F) {
      complete_active_fade();
    }
  }

  void reset() {
    m_mode = 0;
    m_color = 0;
    m_alpha = 0.0F;
    m_elapsed_seconds = 0.0F;
    m_duration_seconds = 0.0F;
    m_remaining_delay_seconds = 0.0F;
  }

  [[nodiscard]] std::uint8_t mode() const {
    return m_mode;
  }
  [[nodiscard]] std::uint32_t color() const {
    return m_color;
  }
  [[nodiscard]] float alpha() const {
    return m_alpha;
  }
  [[nodiscard]] float elapsed_seconds() const {
    return m_elapsed_seconds;
  }
  [[nodiscard]] float duration_seconds() const {
    return m_duration_seconds;
  }
  [[nodiscard]] float remaining_delay_seconds() const {
    return m_remaining_delay_seconds;
  }
  [[nodiscard]] bool transitioning() const {
    return m_remaining_delay_seconds > 0.0F ||
           (m_duration_seconds > 0.0F && m_elapsed_seconds < m_duration_seconds);
  }

 private:
  void complete_active_fade() {
    m_elapsed_seconds = m_duration_seconds;
    m_remaining_delay_seconds = 0.0F;
    m_alpha = m_mode == 1U ? 1.0F : 0.0F;
    if (m_mode == 2U) {
      m_mode = 0U;
    }
  }

  std::uint8_t m_mode{0};
  std::uint32_t m_color{0};
  float m_alpha{0.0F};
  float m_elapsed_seconds{0.0F};
  float m_duration_seconds{0.0F};
  float m_remaining_delay_seconds{0.0F};
};

/// One session-global AREA 0x84/0x85 cinematic-mask request.
struct WorldLetterboxCommand {
  bool enabled{false};
};

/// Fire-and-forget, nonspatial voice-over submitted by compact OBJECTS.
struct WorldVoiceOverCommand {
  std::uint32_t scene_id{0};
  std::uint32_t scene_generation{0};
  std::int16_t object_id{0};
  std::string audio_path;
};

enum class TextPresentationRole : std::uint8_t {
  k_unknown,
  k_spoken_subtitle,
  k_dialog_line,
  k_dialog_choice,
  k_cinematic_overlay,
  k_credit,
  k_document,
  k_interface_text,
};

enum class TextModernizationPolicy : std::uint8_t {
  k_follow_user_setting,
  k_faithful_only,
};

enum class TextSourceKind : std::uint8_t {
  k_unknown,
  k_iam_object,
};

/// Classification and source identity travel beside authored Runtime content;
/// they are not inferred from markup or audio presence.
struct WorldTextProvenance {
  TextSourceKind source_kind{TextSourceKind::k_unknown};
  std::int16_t object_id{0};
  std::string audio_resource;
  TextPresentationRole role{TextPresentationRole::k_unknown};
  TextModernizationPolicy modernization_policy{TextModernizationPolicy::k_faithful_only};
};

/// General world/cinematic text presentation request. Lifetime remains based
/// on the raw authored bytes and independent of render-frame cadence.
struct WorldTextCommand {
  std::uint32_t scene_id{0};
  std::uint32_t scene_generation{0};
  Interface::RuntimeTextDocument document;
  WorldTextProvenance provenance;
  std::uint32_t duration_ms{0};
};

/// CPU state for one current general world-text presentation.
class WorldTextState {
 public:
  [[nodiscard]] bool apply_command(
      WorldTextCommand command, const std::uint32_t scene_id, const std::uint32_t generation) {
    if (command.scene_id != scene_id || command.scene_generation != generation) {
      return false;
    }
    m_document = std::move(command.document);
    m_provenance = std::move(command.provenance);
    m_remaining_seconds = static_cast<float>(command.duration_ms) / 1000.0F;
    m_elapsed_seconds = 0.0F;
    return true;
  }

  void update(const float delta_seconds) {
    const float elapsed{std::max(delta_seconds, 0.0F)};
    m_elapsed_seconds += elapsed;
    m_remaining_seconds = std::max(0.0F, m_remaining_seconds - elapsed);
    if (m_remaining_seconds == 0.0F) {
      m_document.reset();
    }
  }

  void reset() {
    m_document.reset();
    m_provenance = {};
    m_remaining_seconds = 0.0F;
    m_elapsed_seconds = 0.0F;
  }

  [[nodiscard]] bool active() const {
    return m_document.has_value() && !m_document->authored_bytes().empty() &&
           m_remaining_seconds > 0.0F;
  }
  [[nodiscard]] const Interface::RuntimeTextDocument* document() const {
    return m_document.has_value() ? &m_document.value() : nullptr;
  }
  [[nodiscard]] const WorldTextProvenance& provenance() const {
    return m_provenance;
  }
  [[nodiscard]] float remaining_seconds() const {
    return m_remaining_seconds;
  }
  [[nodiscard]] std::uint64_t presentation_time_ms() const {
    return static_cast<std::uint64_t>(m_elapsed_seconds * 1000.0F);
  }

 private:
  std::optional<Interface::RuntimeTextDocument> m_document;
  WorldTextProvenance m_provenance;
  float m_remaining_seconds{0.0F};
  float m_elapsed_seconds{0.0F};
};

/// CPU-only state for OpenNomad's cinematic top/bottom presentation mask.
///
/// Runtime confirms the global state machine, 60-unit duration, and 64/480
/// geometry. OpenNomad currently approximates the native two-tone intensity
/// ramp by interpolating visible bar height; exact raster fidelity is deferred.
class WorldLetterboxState {
 public:
  static constexpr float k_retail_bar_fraction{2.0F / 15.0F};
  static constexpr float k_transition_duration_seconds{2.0F};
  static constexpr std::uint32_t k_transition_runtime_units{60U};

  /// Runtime scales the retail 64-pixel bar from the 480-line reference
  /// viewport. Width/aspect ratio does not participate in native mask geometry.
  [[nodiscard]] static float target_bar_height(float width, float height) {
    if (width <= 0.0F || height <= 0.0F) {
      return 0.0F;
    }
    return height * k_retail_bar_fraction;
  }

  [[nodiscard]] float current_bar_height(float width, float height) const {
    return target_bar_height(width, height) * m_amount;
  }

  /// Applies a session-global cinematic-mask command.
  [[nodiscard]] bool apply_command(const WorldLetterboxCommand& command) {
    set_enabled(command.enabled);
    return true;
  }

  void set_enabled(bool enabled) {
    if (!enabled && !m_requested) {
      return;
    }
    m_requested = enabled;
    m_start_amount = m_amount;
    m_target_amount = enabled ? 1.0F : 0.0F;
    m_elapsed = 0.0F;
  }

  void update(float delta_time) {
    if (!transitioning()) {
      return;
    }
    m_elapsed += std::max(delta_time, 0.0F);
    const float progress{std::clamp(m_elapsed / k_transition_duration_seconds, 0.0F, 1.0F)};
    // Modern approximation: Runtime animates a two-tone intensity ramp over
    // fixed geometry rather than interpolating the geometry itself.
    m_amount = m_start_amount + ((m_target_amount - m_start_amount) * progress);
  }

  void reset() {
    m_amount = 0.0F;
    m_start_amount = 0.0F;
    m_target_amount = 0.0F;
    m_elapsed = 0.0F;
    m_requested = false;
  }

  [[nodiscard]] float amount() const {
    return m_amount;
  }

  [[nodiscard]] bool requested() const {
    return m_requested;
  }

  [[nodiscard]] bool transitioning() const {
    return m_amount != m_target_amount;
  }

 private:
  float m_amount{0.0F};
  float m_start_amount{0.0F};
  float m_target_amount{0.0F};
  float m_elapsed{0.0F};
  bool m_requested{false};
};

/// Observes the explicit session-level presentation reset epoch. Active-world
/// identity changes never reach this helper and therefore cannot reset the
/// session-global fade or cinematic mask.
class WorldPresentationResetObserver {
 public:
  [[nodiscard]] bool synchronize(
      const std::uint64_t reset_generation, WorldFadeState& fade, WorldLetterboxState& letterbox) {
    if (reset_generation == m_observed_generation) {
      return false;
    }
    fade.reset();
    letterbox.reset();
    m_observed_generation = reset_generation;
    return true;
  }

  [[nodiscard]] std::uint64_t observed_generation() const {
    return m_observed_generation;
  }

 private:
  std::uint64_t m_observed_generation{0};
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

  void enqueue_camera_completion(WorldCameraOperationCompletion completion) {
    m_camera_completions.push_back(std::move(completion));
  }

  void enqueue_fade(WorldFadeCommand command) {
    m_fade_commands.push_back(std::move(command));
  }

  void enqueue_letterbox(WorldLetterboxCommand command) {
    m_letterbox_commands.push_back(std::move(command));
  }

  void enqueue_voice_over(WorldVoiceOverCommand command) {
    m_voice_over_commands.push_back(std::move(command));
  }

  void enqueue_world_text(WorldTextCommand command) {
    m_world_text_commands.push_back(std::move(command));
  }

  [[nodiscard]] std::optional<WorldCameraCommand> take_camera() {
    if (m_camera_commands.empty()) {
      return std::nullopt;
    }
    WorldCameraCommand command{std::move(m_camera_commands.front())};
    m_camera_commands.pop_front();
    return command;
  }

  [[nodiscard]] std::optional<WorldCameraOperationCompletion> take_camera_completion() {
    if (m_camera_completions.empty()) {
      return std::nullopt;
    }
    WorldCameraOperationCompletion completion{std::move(m_camera_completions.front())};
    m_camera_completions.pop_front();
    return completion;
  }

  [[nodiscard]] std::optional<WorldFadeCommand> take_fade() {
    if (m_fade_commands.empty()) {
      return std::nullopt;
    }
    WorldFadeCommand command{std::move(m_fade_commands.front())};
    m_fade_commands.pop_front();
    return command;
  }

  [[nodiscard]] std::optional<WorldLetterboxCommand> take_letterbox() {
    if (m_letterbox_commands.empty()) {
      return std::nullopt;
    }
    WorldLetterboxCommand command{std::move(m_letterbox_commands.front())};
    m_letterbox_commands.pop_front();
    return command;
  }

  [[nodiscard]] std::optional<WorldVoiceOverCommand> take_voice_over() {
    if (m_voice_over_commands.empty()) {
      return std::nullopt;
    }
    WorldVoiceOverCommand command{std::move(m_voice_over_commands.front())};
    m_voice_over_commands.pop_front();
    return command;
  }

  [[nodiscard]] std::optional<WorldTextCommand> take_world_text() {
    if (m_world_text_commands.empty()) {
      return std::nullopt;
    }
    WorldTextCommand command{std::move(m_world_text_commands.front())};
    m_world_text_commands.pop_front();
    return command;
  }

  [[nodiscard]] std::size_t pending_camera_count() const {
    return m_camera_commands.size();
  }

  [[nodiscard]] std::size_t pending_camera_completion_count() const {
    return m_camera_completions.size();
  }

  [[nodiscard]] std::size_t pending_fade_count() const {
    return m_fade_commands.size();
  }

  [[nodiscard]] std::size_t pending_letterbox_count() const {
    return m_letterbox_commands.size();
  }
  [[nodiscard]] std::size_t pending_voice_over_count() const {
    return m_voice_over_commands.size();
  }
  [[nodiscard]] std::size_t pending_world_text_count() const {
    return m_world_text_commands.size();
  }

  /// Monotonic epoch incremented only at the global presentation/session
  /// reset boundary, never by world-context changes or ordinary queue use.
  [[nodiscard]] std::uint64_t reset_generation() const {
    return m_reset_generation;
  }

  void clear() {
    m_camera_commands.clear();
    m_camera_completions.clear();
    m_fade_commands.clear();
    m_letterbox_commands.clear();
    m_voice_over_commands.clear();
    m_world_text_commands.clear();
    ++m_reset_generation;
  }

 private:
  std::deque<WorldCameraCommand> m_camera_commands;
  std::deque<WorldCameraOperationCompletion> m_camera_completions;
  std::deque<WorldFadeCommand> m_fade_commands;
  std::deque<WorldLetterboxCommand> m_letterbox_commands;
  std::deque<WorldVoiceOverCommand> m_voice_over_commands;
  std::deque<WorldTextCommand> m_world_text_commands;
  std::uint64_t m_reset_generation{0};
};

}  // namespace App
