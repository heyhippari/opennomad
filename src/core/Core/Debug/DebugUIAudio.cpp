#include <fmt/format.h>
#include <imgui.h>

#include <string>

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/DebugUI.hpp"
#include "Core/Debug/DebugUIInternal.hpp"

namespace App::Debug {

void DebugUI::show_audio_inspector() {
  ImGui::Begin("Audio Inspector", &m_show_audio_inspector);

  Audio::AudioSystem* audio{m_context.audio_system};
  if (audio == nullptr) {
    ImGui::TextUnformatted("Audio system not available.");
    ImGui::End();
    return;
  }

  const Audio::AudioDebugSnapshot& snapshot{audio->debug_snapshot()};

  // --- Device / subsystem ---
  ImGui::SeparatorText("Device / subsystem");
  const char* state{"not initialized"};
  if (snapshot.unavailable) {
    state = "unavailable";
  } else if (snapshot.initialized) {
    state = "initialized";
  }
  ImGui::Text("State: %s", state);
  if (!snapshot.state_note.empty()) {
    ImGui::TextColored(K_WARNING_COLOR, "%s", snapshot.state_note.c_str());
  }
  ImGui::Text("SDL3_mixer: %s", snapshot.mixer_version.c_str());
  ImGui::Text("Device: %s", snapshot.device_name.c_str());
  ImGui::Text("Requested: %s", snapshot.requested_format.c_str());
  ImGui::Text("Negotiated: %s", snapshot.negotiated_format.c_str());

  ImGui::SeparatorText("Debug Overrides");
  ImGui::TextDisabled("Gain and transport controls modify live audio state.");
  float master{audio->master_gain()};
  float sfx{audio->sfx_gain()};
  float music{audio->music_gain()};
  if (ImGui::SliderFloat("Master gain", &master, 0.0F, 2.0F)) {
    audio->set_master_gain(master);
  }
  if (ImGui::SliderFloat("SFX gain", &sfx, 0.0F, 2.0F)) {
    audio->set_sfx_gain(sfx);
  }
  if (ImGui::SliderFloat("Music gain", &music, 0.0F, 2.0F)) {
    audio->set_music_gain(music);
  }
  ImGui::Text("Voices: %lu active / %lu free / 16 total",
      static_cast<unsigned long>(snapshot.active_voices),
      static_cast<unsigned long>(snapshot.free_voices));
  ImGui::Text("Cache: %lu / %lu resources",
      static_cast<unsigned long>(snapshot.cached_resources),
      static_cast<unsigned long>(snapshot.cache_capacity));
  if (ImGui::Button("Stop all SFX")) {
    audio->stop_all_sfx();
  }
  ImGui::SameLine();
  if (ImGui::Button("Stop music")) {
    audio->music().stop(0);
  }

  // --- Listener ---
  ImGui::SeparatorText("Listener");
  ImGui::Text("Position: (%.2f, %.2f, %.2f)",
      static_cast<double>(snapshot.listener_position.at(0)),
      static_cast<double>(snapshot.listener_position.at(1)),
      static_cast<double>(snapshot.listener_position.at(2)));
  ImGui::Text("Velocity: (%.2f, %.2f, %.2f)",
      static_cast<double>(snapshot.listener_velocity.at(0)),
      static_cast<double>(snapshot.listener_velocity.at(1)),
      static_cast<double>(snapshot.listener_velocity.at(2)));
  ImGui::Text("Forward: (%.2f, %.2f, %.2f)",
      static_cast<double>(snapshot.listener_forward.at(0)),
      static_cast<double>(snapshot.listener_forward.at(1)),
      static_cast<double>(snapshot.listener_forward.at(2)));
  ImGui::Text("Up: (%.2f, %.2f, %.2f)",
      static_cast<double>(snapshot.listener_up.at(0)),
      static_cast<double>(snapshot.listener_up.at(1)),
      static_cast<double>(snapshot.listener_up.at(2)));
  ImGui::Text("Right: (%.2f, %.2f, %.2f)",
      static_cast<double>(snapshot.listener_right.at(0)),
      static_cast<double>(snapshot.listener_right.at(1)),
      static_cast<double>(snapshot.listener_right.at(2)));
  ImGui::Text(
      "Last audio update delta: %.6f s", static_cast<double>(snapshot.last_update_delta_seconds));
  if (snapshot.listener_degenerate) {
    ImGui::TextColored(K_WARNING_COLOR, "Degenerate listener transform (fallback basis used).");
  }

  // --- Resource cache ---
  ImGui::SeparatorText("Resource cache");
  if (ImGui::BeginChild("##AudioResources", ImVec2(0.0F, 140.0F), ImGuiChildFlags_Borders)) {
    for (const Audio::ResourceDebugInfo& resource : snapshot.resources) {
      ImGui::Text("%u: '%s' (%s) %s %d ch %d Hz %lld ms %lu bytes refs %lu",
          resource.resource.index,
          resource.name.c_str(),
          resource.scenario_name.c_str(),
          resource.loaded ? resource.format.c_str() : "FAILED",
          resource.channels,
          resource.frequency,
          static_cast<long long>(resource.duration_ms),
          static_cast<unsigned long>(resource.byte_size),
          static_cast<unsigned long>(resource.ref_count));
      if (!resource.load_error.empty()) {
        ImGui::TextDisabled("    error: %s", resource.load_error.c_str());
      }
      ImGui::SameLine();
      if (resource.loaded &&
          ImGui::SmallButton(
              fmt::format("Audition (debug override)##{}", resource.resource.index).c_str())) {
        static_cast<void>(audio->audition(resource.resource));
      }
    }
  }
  ImGui::EndChild();

  // --- 16-slot voice table ---
  ImGui::SeparatorText("Voice slots");
  if (ImGui::BeginChild("##AudioVoices", ImVec2(0.0F, 220.0F), ImGuiChildFlags_Borders)) {
    for (const Audio::VoiceDebugInfo& voice : snapshot.voices) {
      const char* state_name{"Unknown"};
      switch (voice.state) {
        case Audio::VoiceState::k_free:
          state_name = "Free";
          break;
        case Audio::VoiceState::k_queued:
          state_name = "Queued";
          break;
        case Audio::VoiceState::k_playing:
          state_name = "Playing";
          break;
        case Audio::VoiceState::k_stopping:
          state_name = "Stopping";
          break;
      }
      ImGui::Text("slot %u gen %u [%s] sound %u '%s' idx %u owner '%s'",
          voice.index,
          voice.generation,
          state_name,
          voice.resource.index,
          voice.sound_name.c_str(),
          voice.scenario_sound_index,
          voice.owner_description.c_str());
      if (voice.state != Audio::VoiceState::k_free) {
        ImGui::SameLine();
        if (ImGui::SmallButton(fmt::format("Stop (debug override)##{}", voice.index).c_str())) {
          static_cast<void>(audio->stop_voice(
              Audio::VoiceHandle{.index = voice.index, .generation = voice.generation}));
        }
        ImGui::Indent();
        ImGui::Text(
            "%s%s%s pos %.0f/%.0f ms dist %.1f prev %.1f gain %.2f pan %.2f l/r "
            "%.2f/%.2f freq %.0f ratio %.3f",
            voice.looping ? "loop " : "once ",
            voice.nonspatial ? "nonspatial " : "spatial ",
            voice.unknown_flag ? "unknown-flag " : "",
            static_cast<double>(voice.playback_position_ms),
            static_cast<double>(voice.remaining_ms),
            static_cast<double>(voice.distance),
            static_cast<double>(voice.previous_distance),
            static_cast<double>(voice.attenuation_gain),
            static_cast<double>(voice.pan),
            static_cast<double>(voice.left_gain),
            static_cast<double>(voice.right_gain),
            static_cast<double>(voice.base_frequency_hz),
            static_cast<double>(voice.frequency_ratio));
        ImGui::Text("emitter pos (%.1f, %.1f, %.1f) vel (%.1f, %.1f, %.1f) min %.0f max %.0f",
            static_cast<double>(voice.emitter_position.at(0)),
            static_cast<double>(voice.emitter_position.at(1)),
            static_cast<double>(voice.emitter_position.at(2)),
            static_cast<double>(voice.emitter_velocity.at(0)),
            static_cast<double>(voice.emitter_velocity.at(1)),
            static_cast<double>(voice.emitter_velocity.at(2)),
            static_cast<double>(voice.minimum_distance),
            static_cast<double>(voice.maximum_distance));
        ImGui::Unindent();
      }
    }
  }
  ImGui::EndChild();

  // --- Music ---
  ImGui::SeparatorText("Music");
  const Audio::MusicDebugInfo& music_info{snapshot.music};
  ImGui::Text(
      "Source: %s", music_info.source_name.empty() ? "(none)" : music_info.source_name.c_str());
  const char* music_state{"stopped"};
  if (music_info.playing) {
    music_state = music_info.paused ? "paused" : "playing";
  }
  ImGui::Text("State: %s%s", music_state, music_info.loop ? " (loop)" : "");
  ImGui::Text("Loop start: %lld ms", static_cast<long long>(music_info.loop_start_ms));
  ImGui::Text("Position: %lld / %lld ms",
      static_cast<long long>(music_info.playback_position_ms),
      static_cast<long long>(music_info.duration_ms));
  ImGui::TextDisabled("Debug Overrides");
  if (ImGui::Button("Pause")) {
    audio->music().pause();
  }
  ImGui::SameLine();
  if (ImGui::Button("Resume")) {
    audio->music().resume();
  }
  ImGui::TextDisabled("%s", music_info.status_note.c_str());

  // --- Event log ---
  ImGui::SeparatorText("Event log");
  if (ImGui::BeginChild("##AudioEvents", ImVec2(0.0F, 140.0F), ImGuiChildFlags_Borders)) {
    for (const Audio::AudioEvent& event : snapshot.events) {
      ImGui::Text("%s", event.message.c_str());
    }
  }
  ImGui::EndChild();

  ImGui::End();
}

}  // namespace App::Debug
