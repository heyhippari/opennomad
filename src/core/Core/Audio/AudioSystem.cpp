#include "Core/Audio/AudioSystem.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Audio/LegacySpatializer.hpp"
#include "Core/Audio/SoundResourceCache.hpp"
#include "Core/Audio/VoicePool.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"

namespace App::Audio {

namespace {

/// Clamps a user-facing gain to a documented, sensible range.
[[nodiscard]] float clamp_gain(const float gain) {
  return std::clamp(gain, 0.0F, 4.0F);
}

/// Human-readable event severity label.
[[nodiscard]] const char* severity_name(const AudioEventSeverity severity) {
  switch (severity) {
    case AudioEventSeverity::k_debug: return "debug";
    case AudioEventSeverity::k_info:  return "info";
    case AudioEventSeverity::k_warn:  return "warn";
    case AudioEventSeverity::k_error: return "error";
    default:                          return "info";
  }
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// StopEventQueue
// ─────────────────────────────────────────────────────────────────────────────

void AudioSystem::StopEventQueue::push(const std::uint32_t index, const std::uint32_t generation) {
  const std::scoped_lock guard{mutex};
  if (count >= slots.size()) {
    return;  // Bounded: drop the newest event rather than grow unboundedly.
  }
  slots.at(count) = std::pair<std::uint32_t, std::uint32_t>{index, generation};
  count += 1;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> AudioSystem::StopEventQueue::drain() {
  const std::scoped_lock guard{mutex};
  std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
  result.reserve(count);
  for (std::size_t index{0}; index < count; ++index) {
    result.push_back(slots.at(index));
  }
  count = 0;
  return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

std::expected<std::unique_ptr<AudioSystem>, std::string> AudioSystem::create() {
  APP_PROFILE_FUNCTION();

  if (!MIX_Init()) {
    return std::expected<std::unique_ptr<AudioSystem>, std::string>{
        std::unexpect, fmt::format("MIX_Init failed: {}", SDL_GetError())};
  }

  auto system{std::unique_ptr<AudioSystem>{
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      new AudioSystem()}};
  system->m_initialized = true;
  system->m_mixer_version =
      fmt::format("{}.{}.{}",
          SDL_MIXER_MAJOR_VERSION,
          SDL_MIXER_MINOR_VERSION,
          SDL_MIXER_MICRO_VERSION);

  // Request a modern stereo format hint; SDL3_mixer negotiates/ converts to
  // the actual device format (a nullptr hint is also valid).
  SDL_AudioSpec requested{};
  requested.format = SDL_AUDIO_F32;
  requested.channels = 2;
  requested.freq = 48000;
  system->m_requested_format = fmt::format("{} ch, {} Hz, {}",
      requested.channels,
      requested.freq,
      SDL_GetAudioFormatName(requested.format));

  MIX_Mixer* mixer{MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &requested)};
  if (mixer == nullptr) {
    system->m_unavailable = true;
    system->m_state_note = fmt::format("MIX_CreateMixerDevice failed: {}", SDL_GetError());
    App::Log::warn("Audio: unavailable ({})", system->m_state_note);
    system->rebuild_snapshot();
    return system;
  }
  system->m_mixer.reset(mixer);

  SDL_AudioSpec negotiated{};
  if (MIX_GetMixerFormat(mixer, &negotiated)) {
    system->m_negotiated_format = fmt::format("{} ch, {} Hz, {}",
        negotiated.channels,
        negotiated.freq,
        SDL_GetAudioFormatName(negotiated.format));
  } else {
    system->m_negotiated_format = "unknown (MIX_GetMixerFormat failed)";
  }
  const char* device_name{SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)};
  system->m_device_name = device_name != nullptr ? device_name : "default";
  App::Log::info("Audio: initialized SDL3_mixer {}, device '{}', requested {}, negotiated {}",
      system->m_mixer_version,
      system->m_device_name,
      system->m_requested_format,
      system->m_negotiated_format);

  // 16 persistent SFX tracks, tagged "sfx", each with a stopped callback.
  for (std::size_t index{0}; index < system->m_tracks.size(); ++index) {
    MIX_Track* track{MIX_CreateTrack(mixer)};
    if (track == nullptr) {
      system->m_unavailable = true;
      system->m_state_note = fmt::format("MIX_CreateTrack failed: {}", SDL_GetError());
      App::Log::warn("Audio: {} (SFX track {})", system->m_state_note, index);
      system->rebuild_snapshot();
      return system;
    }
    if (!MIX_TagTrack(track, "sfx")) {
      App::Log::warn("Audio: failed to tag SFX track {}: {}", index, SDL_GetError());
    }
    system->m_tracks.at(index) = track;
    system->m_contexts.at(index).system = system.get();
    system->m_contexts.at(index).index = static_cast<std::uint32_t>(index);
    MIX_SetTrackStoppedCallback(track, &AudioSystem::stopped_callback, &system->m_contexts.at(index));
  }

  system->m_cache = std::make_unique<SoundResourceCache>(mixer);
  system->m_music.attach(mixer);
  system->set_master_gain(1.0F);
  system->set_sfx_gain(1.0F);
  system->set_music_gain(1.0F);
  system->rebuild_snapshot();
  return system;
}

AudioSystem::~AudioSystem() {
  APP_PROFILE_FUNCTION();

  if (!m_initialized) {
    return;
  }

  // 1. Prevent new play requests (no external flag needed: this is the last
  //    owner; script/scene code is torn down before this runs).
  // 2. Stop music and every SFX track.
  m_music.stop(0);
  for (MIX_Track* track : m_tracks) {
    if (track != nullptr) {
      MIX_StopTrack(track, 0);
    }
  }

  // 3. Drain pending completion events (they are moot now).
  static_cast<void>(m_stop_events.drain());

  // 4. Disable stopped callbacks.
  for (MIX_Track* track : m_tracks) {
    if (track != nullptr) {
      MIX_SetTrackStoppedCallback(track, nullptr, nullptr);
    }
  }

  // 5. Destroy tracks (explicit, before the mixer).
  for (MIX_Track*& track : m_tracks) {
    if (track != nullptr) {
      MIX_DestroyTrack(track);
      track = nullptr;
    }
  }
  m_music.shutdown();

  // 6. Destroy cached MIX_Audio objects.
  m_cache.reset();

  // 7. Destroy the mixer.
  m_mixer.reset();

  // 8. MIX_Quit exactly once for the successful initialization.
  MIX_Quit();
}

// ─────────────────────────────────────────────────────────────────────────────
// Availability / cache
// ─────────────────────────────────────────────────────────────────────────────

bool AudioSystem::available() const {
  return m_initialized && !m_unavailable && m_mixer != nullptr;
}

std::expected<SoundResourceId, std::string> AudioSystem::load_sound(
    const std::string& canonical_key,
    const std::string_view scenario_name,
    const std::size_t record_index,
    const std::string_view name,
    const std::uint16_t h_id,
    const std::span<const std::byte> wav_bytes) {
  if (!available()) {
    return std::expected<SoundResourceId, std::string>{
        std::unexpect, "audio subsystem unavailable"};
  }
  return m_cache->load(
      canonical_key, scenario_name, record_index, name, h_id, wav_bytes);
}

// ─────────────────────────────────────────────────────────────────────────────
// Playback
// ─────────────────────────────────────────────────────────────────────────────

std::optional<VoiceHandle> AudioSystem::play_sound(const SoundPlayRequest& request) {
  if (!available()) {
    append_event(AudioEventSeverity::k_warn,
        fmt::format("play rejected: audio unavailable (sound '{}')", request.sound_name));
    return std::nullopt;
  }
  if (!request.resource.valid() || m_cache->audio(request.resource) == nullptr) {
    append_event(AudioEventSeverity::k_warn,
        fmt::format("play rejected: invalid resource {} (sound '{}')",
            request.resource.index,
            request.sound_name));
    return std::nullopt;
  }

  auto allocated{m_pool.allocate()};
  if (!allocated) {
    append_event(AudioEventSeverity::k_warn,
        fmt::format("play rejected: {} (sound '{}')", allocated.error(), request.sound_name));
    App::Log::warn("Audio: {}", allocated.error());
    return std::nullopt;
  }

  const VoiceHandle handle{allocated.value()};
  m_pool.configure(handle, request);
  append_event(AudioEventSeverity::k_info,
      fmt::format("queued {}:{} sound '{}' resource {} {} {}",
          handle.index,
          handle.generation,
          request.sound_name,
          request.resource.index,
          request.loop ? "loop" : "once",
          request.emitter.has_value() ? "spatial" : "nonspatial"));
  return handle;
}

bool AudioSystem::stop_first(const SoundResourceId sound, const AudioOwnerToken& owner) {
  if (!available()) {
    return false;
  }
  const std::optional<VoiceHandle> handle{m_pool.find_first_active(sound, owner)};
  if (!handle.has_value()) {
    return false;
  }
  SoundVoice* voice{m_pool.find(handle.value())};
  if (voice == nullptr) {
    return false;
  }
  MIX_Track* track{m_tracks.at(static_cast<std::size_t>(handle->index))};
  if (track != nullptr) {
    MIX_StopTrack(track, 0);
  }
  voice->state = VoiceState::k_stopping;
  append_event(AudioEventSeverity::k_info,
      fmt::format("stop requested {}:{} (sound {}, owner {})",
          handle->index,
          handle->generation,
          sound.index,
          owner.describe()));
  return true;
}

bool AudioSystem::stop_voice(const VoiceHandle handle) {
  if (!available()) {
    return false;
  }
  SoundVoice* voice{m_pool.find(handle)};
  if (voice == nullptr || voice->state == VoiceState::k_free) {
    return false;
  }
  MIX_Track* track{m_tracks.at(static_cast<std::size_t>(handle.index))};
  if (track != nullptr) {
    MIX_StopTrack(track, 0);
  }
  voice->state = VoiceState::k_stopping;
  append_event(AudioEventSeverity::k_info,
      fmt::format("stop requested {}:{} (direct)", handle.index, handle.generation));
  return true;
}

void AudioSystem::stop_owned_by(const AudioOwnerToken& owner) {
  if (!available()) {
    return;
  }
  for (std::size_t index{0}; index < m_pool.size(); ++index) {
    SoundVoice& voice{m_pool.at(index)};
    if (voice.state == VoiceState::k_free || voice.owner != owner) {
      continue;
    }
    MIX_Track* track{m_tracks.at(index)};
    if (track != nullptr) {
      MIX_StopTrack(track, 0);
    }
    voice.state = VoiceState::k_stopping;
  }
}

void AudioSystem::stop_all_sfx() {
  if (!available()) {
    return;
  }
  for (std::size_t index{0}; index < m_pool.size(); ++index) {
    SoundVoice& voice{m_pool.at(index)};
    if (voice.state == VoiceState::k_free) {
      continue;
    }
    MIX_Track* track{m_tracks.at(index)};
    if (track != nullptr) {
      MIX_StopTrack(track, 0);
    }
    voice.state = VoiceState::k_stopping;
  }
}

std::optional<VoiceHandle> AudioSystem::audition(const SoundResourceId resource) {
  return play_sound(SoundPlayRequest{.resource = resource,
      .loop = false,
      .emitter = std::nullopt,
      .owner = AudioOwnerToken{},
      .scenario_sound_index = 0xFFFFU,
      .sound_name = "audition",
      .raw_flags = 0});
}

// ─────────────────────────────────────────────────────────────────────────────
// Listener / emitters / update
// ─────────────────────────────────────────────────────────────────────────────

void AudioSystem::set_listener(const AudioListenerState& listener) { m_listener = listener; }

void AudioSystem::set_emitter_resolver(EmitterResolver resolver) {
  m_emitter_resolver = std::move(resolver);
}

void AudioSystem::stopped_callback(void* userdata, MIX_Track* /*track*/) {
  auto* context{static_cast<SlotContext*>(userdata)};
  context->system->m_stop_events.push(
      context->index, context->generation.load(std::memory_order_relaxed));
}

void AudioSystem::release_slot(const VoiceHandle handle, const char* reason) {
  if (!m_pool.generation_matches(handle)) {
    return;  // Stale completion event for a reused generation.
  }
  SoundVoice* voice{m_pool.find(handle)};
  if (voice == nullptr || voice->state == VoiceState::k_free) {
    return;  // Already released once.
  }
  if (voice->resource.valid()) {
    m_cache->remove_reference(voice->resource);
  }
  append_event(AudioEventSeverity::k_info,
      fmt::format("{} {}:{} (sound {})",
          reason,
          handle.index,
          handle.generation,
          voice->resource.index));
  m_pool.release(handle);
}

void AudioSystem::update(const float real_delta_seconds) {
  APP_PROFILE_FUNCTION();

  m_last_update_delta_seconds = real_delta_seconds;
  if (!available()) {
    rebuild_snapshot();
    return;
  }

  // 1. Drain stopped-callback events (compact, main-thread release).
  for (const auto& [index, generation] : m_stop_events.drain()) {
    release_slot(VoiceHandle{.index = index, .generation = generation}, "completed");
  }

  // 2. Start queued voices from the beginning.
  for (std::size_t index{0}; index < m_pool.size(); ++index) {
    SoundVoice& voice{m_pool.at(index)};
    if (voice.state != VoiceState::k_queued) {
      continue;
    }
    const VoiceHandle slot_handle{.index = static_cast<std::uint32_t>(index),
        .generation = voice.generation};
    MIX_Track* track{m_tracks.at(index)};
    MIX_Audio* audio{m_cache->audio(voice.resource)};
    if (track == nullptr || audio == nullptr) {
      m_pool.release(slot_handle);
      continue;
    }

    // Reset all track-local state from any prior use.
    MIX_SetTrackGain(track, 1.0F);
    MIX_SetTrackStereo(track, nullptr);
    MIX_SetTrackFrequencyRatio(track, 1.0F);
    MIX_SetTrackPlaybackPosition(track, 0);
    if (!MIX_SetTrackAudio(track, audio)) {
      App::Log::warn("Audio: MIX_SetTrackAudio failed for slot {}: {}", index, SDL_GetError());
      m_pool.release(slot_handle);
      continue;
    }

    // Retain the source's native/base frequency for diagnostics.
    SDL_AudioSpec spec{};
    if (MIX_GetAudioFormat(audio, &spec)) {
      voice.base_frequency_hz = static_cast<float>(spec.freq);
    }

    const SDL_PropertiesID props{SDL_CreateProperties()};
    SDL_SetNumberProperty(
        props, MIX_PROP_PLAY_LOOPS_NUMBER, voice.looping ? -1 : 0);
    const bool started{MIX_PlayTrack(track, props)};
    SDL_DestroyProperties(props);
    if (!started) {
      App::Log::warn("Audio: MIX_PlayTrack failed for slot {}: {}", index, SDL_GetError());
      m_pool.release(slot_handle);
      continue;
    }

    m_contexts.at(index).generation.store(voice.generation, std::memory_order_relaxed);
    m_cache->add_reference(voice.resource);
    voice.state = VoiceState::k_playing;
    append_event(AudioEventSeverity::k_info,
        fmt::format("started {}:{} (sound '{}', {})",
            index,
            voice.generation,
            voice.resource.index,
            voice.looping ? "loop" : "once"));
  }

  // 3. Resolve emitters and spatialize active voices.
  const float delta{real_delta_seconds};
  const bool delta_valid{std::isfinite(delta) && delta > 0.0F};
  for (std::size_t index{0}; index < m_pool.size(); ++index) {
    SoundVoice& voice{m_pool.at(index)};
    if (voice.state != VoiceState::k_playing && voice.state != VoiceState::k_queued) {
      continue;
    }

    // Owner-attached voices resolve their object transform once per frame.
    if (!voice.owner.is_null()) {
      if (!m_emitter_resolver) {
        // No resolver: keep the static emitter captured at queue time.
      } else {
        const std::optional<Vec3> position{m_emitter_resolver(voice.owner)};
        if (!position.has_value()) {
          append_event(AudioEventSeverity::k_warn,
              fmt::format("OwnerInvalidated {}:{} (owner {})",
                  index,
                  voice.generation,
                  voice.owner.describe()));
          MIX_Track* track{m_tracks.at(index)};
          if (track != nullptr) {
            MIX_StopTrack(track, 0);
          }
          voice.state = VoiceState::k_stopping;
          continue;
        }
        if (voice.emitter.has_value()) {
          if (delta_valid) {
            voice.emitter->velocity = Vec3{
                (position->at(0) - voice.emitter->position.at(0)) / delta,
                (position->at(1) - voice.emitter->position.at(1)) / delta,
                (position->at(2) - voice.emitter->position.at(2)) / delta};
          } else {
            voice.emitter->velocity = Vec3{0.0F, 0.0F, 0.0F};
          }
          voice.emitter->position = position.value();
        }
      }
    }

    MIX_Track* track{m_tracks.at(index)};
    if (track == nullptr) {
      continue;
    }

    if (voice.nonspatial || !voice.emitter.has_value()) {
      MIX_SetTrackStereo(track, nullptr);
      MIX_SetTrackGain(track, 1.0F);
      MIX_SetTrackFrequencyRatio(track, 1.0F);
      voice.attenuation_gain = 1.0F;
      voice.pan = 0.0F;
      voice.left_gain = 1.0F;
      voice.right_gain = 1.0F;
      voice.frequency_ratio = 1.0F;
      voice.distance = 0.0F;
      continue;
    }

    const SpatialResult result{
        spatialize(m_listener, voice.emitter.value(), voice.previous_distance, delta)};
    MIX_SetTrackGain(track, result.attenuation_gain);
    const MIX_StereoGains gains{.left = result.left_gain, .right = result.right_gain};
    MIX_SetTrackStereo(track, &gains);
    MIX_SetTrackFrequencyRatio(track, result.frequency_ratio);

    voice.previous_distance = voice.distance;
    voice.distance = result.distance;
    voice.attenuation_gain = result.attenuation_gain;
    voice.pan = result.pan;
    voice.left_gain = result.left_gain;
    voice.right_gain = result.right_gain;
    voice.frequency_ratio = result.frequency_ratio;
  }

  // 4. Release k_stopping voices whose track has actually halted (covers
  //    callbacks that never fired; release_slot guards double-release).
  for (std::size_t index{0}; index < m_pool.size(); ++index) {
    const SoundVoice& voice{m_pool.at(index)};
    if (voice.state != VoiceState::k_stopping) {
      continue;
    }
    MIX_Track* track{m_tracks.at(index)};
    if (track == nullptr || !MIX_TrackPlaying(track)) {
      release_slot(VoiceHandle{.index = static_cast<std::uint32_t>(index),
                       .generation = voice.generation},
          "stopped");
    }
  }

  rebuild_snapshot();
}

// ─────────────────────────────────────────────────────────────────────────────
// Gains
// ─────────────────────────────────────────────────────────────────────────────

void AudioSystem::set_master_gain(const float gain) {
  m_master_gain = clamp_gain(gain);
  if (available()) {
    MIX_SetMixerGain(m_mixer.get(), m_master_gain);
  }
}

void AudioSystem::set_sfx_gain(const float gain) {
  m_sfx_gain = clamp_gain(gain);
  if (available()) {
    MIX_SetTagGain(m_mixer.get(), "sfx", m_sfx_gain);
  }
}

void AudioSystem::set_music_gain(const float gain) {
  m_music_gain = clamp_gain(gain);
  if (available()) {
    MIX_SetTagGain(m_mixer.get(), "music", m_music_gain);
  }
}

float AudioSystem::master_gain() const { return m_master_gain; }
float AudioSystem::sfx_gain() const { return m_sfx_gain; }
float AudioSystem::music_gain() const { return m_music_gain; }

// ─────────────────────────────────────────────────────────────────────────────
// Music / inspection
// ─────────────────────────────────────────────────────────────────────────────

MusicPlayer& AudioSystem::music() { return m_music; }
const MusicPlayer& AudioSystem::music() const { return m_music; }

void AudioSystem::append_event(const AudioEventSeverity severity, std::string message) {
  m_events.push_back(AudioEvent{.severity = severity, .message = std::move(message)});
  if (m_events.size() > k_event_capacity) {
    m_events.erase(m_events.begin(), m_events.begin() + static_cast<std::ptrdiff_t>(
        m_events.size() - k_event_capacity));
  }
}

void AudioSystem::rebuild_snapshot() {
  AudioDebugSnapshot snapshot;
  snapshot.initialized = m_initialized;
  snapshot.unavailable = m_unavailable;
  snapshot.state_note = m_state_note;
  snapshot.mixer_version = m_mixer_version;
  snapshot.device_name = m_device_name;
  snapshot.requested_format = m_requested_format;
  snapshot.negotiated_format = m_negotiated_format;
  snapshot.master_gain = m_master_gain;
  snapshot.sfx_gain = m_sfx_gain;
  snapshot.music_gain = m_music_gain;
  snapshot.active_voices = m_pool.active_count();
  snapshot.free_voices = m_pool.free_count();
  snapshot.cached_resources = m_cache != nullptr ? m_cache->count() : 0;
  snapshot.cache_capacity = m_cache != nullptr ? m_cache->capacity() : SoundResourceTable::k_capacity;
  snapshot.last_update_delta_seconds = m_last_update_delta_seconds;
  snapshot.listener_position = m_listener.position;
  snapshot.listener_velocity = m_listener.velocity;
  snapshot.listener_forward = m_listener.forward;
  snapshot.listener_up = m_listener.up;

  // Derive the listener right vector defensively (mirrors the spatializer).
  const Vec3& forward{m_listener.forward};
  const Vec3& up{m_listener.up};
  Vec3 right{(forward.at(1) * up.at(2)) - (forward.at(2) * up.at(1)),
      (forward.at(2) * up.at(0)) - (forward.at(0) * up.at(2)),
      (forward.at(0) * up.at(1)) - (forward.at(1) * up.at(0))};
  const float right_length{vec_length(right)};
  if (!std::isfinite(right_length) || right_length < 1.0e-6F) {
    right = Vec3{1.0F, 0.0F, 0.0F};
    snapshot.listener_degenerate = true;
  } else {
    right = Vec3{right.at(0) / right_length, right.at(1) / right_length,
        right.at(2) / right_length};
  }
  snapshot.listener_right = right;

  snapshot.voices.reserve(m_pool.size());
  for (std::size_t index{0}; index < m_pool.size(); ++index) {
    const SoundVoice& voice{m_pool.at(index)};
    VoiceDebugInfo info;
    info.index = static_cast<std::uint32_t>(index);
    info.generation = voice.generation;
    info.state = voice.state;
    info.resource = voice.resource;
    info.scenario_sound_index = voice.scenario_sound_index;
    if (m_cache != nullptr) {
      if (const SoundResourceTable::Entry* entry{m_cache->find_entry(voice.resource)};
          entry != nullptr) {
        info.sound_name = entry->name;
      }
    }
    info.owner_description = voice.owner.describe();
    info.looping = voice.looping;
    info.nonspatial = voice.nonspatial;
    info.unknown_flag = voice.unknown_flag;
    if (voice.emitter.has_value()) {
      info.emitter_position = voice.emitter->position;
      info.emitter_velocity = voice.emitter->velocity;
      info.minimum_distance = voice.emitter->minimum_distance;
      info.maximum_distance = voice.emitter->maximum_distance;
    }
    info.distance = voice.distance;
    info.previous_distance = voice.previous_distance;
    info.attenuation_gain = voice.attenuation_gain;
    info.pan = voice.pan;
    info.left_gain = voice.left_gain;
    info.right_gain = voice.right_gain;
    info.base_frequency_hz = voice.base_frequency_hz;
    info.frequency_ratio = voice.frequency_ratio;

    if (available() && (voice.state == VoiceState::k_playing ||
                        voice.state == VoiceState::k_stopping)) {
      MIX_Track* track{m_tracks.at(index)};
      if (track != nullptr) {
        const Sint64 frames{MIX_GetTrackPlaybackPosition(track)};
        if (frames >= 0) {
          info.playback_position_ms = MIX_TrackFramesToMS(track, frames);
        }
        const Sint64 remaining{MIX_GetTrackRemaining(track)};
        if (remaining >= 0) {
          info.remaining_ms = MIX_TrackFramesToMS(track, remaining);
        }
      }
    }
    snapshot.voices.push_back(info);
  }

  if (m_cache != nullptr) {
    snapshot.resources = m_cache->debug_info();
  }
  snapshot.music = m_music.debug_info();
  snapshot.music.status_note = "original Omikron music script/format support not yet implemented";
  snapshot.events = m_events;
  m_snapshot = std::move(snapshot);
}

const AudioDebugSnapshot& AudioSystem::debug_snapshot() const { return m_snapshot; }

AudioContextInfo AudioSystem::context_info() const {
  const AudioDebugSnapshot& snapshot{m_snapshot};
  AudioContextInfo info;
  info.available = available();
  info.negotiated_format = snapshot.negotiated_format;
  info.active_voices = snapshot.active_voices;
  info.free_voices = snapshot.free_voices;
  if (!snapshot.music.playing) {
    info.music_state = "stopped";
  } else if (snapshot.music.paused) {
    info.music_state = "paused";
  } else {
    info.music_state = "playing";
  }
  const std::size_t event_count{snapshot.events.size()};
  const std::size_t start{event_count > 8 ? event_count - 8 : 0};
  for (std::size_t index{start}; index < event_count; ++index) {
    const AudioEvent& event{snapshot.events.at(index)};
    info.recent_events.push_back(fmt::format("[{}] {}",
        severity_name(event.severity),
        event.message));
  }
  return info;
}

}  // namespace App::Audio
