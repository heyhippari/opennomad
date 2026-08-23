#include "Core/Dialog/DialogPerformanceRuntime.hpp"

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

#include "Core/Audio/AudioSystem.hpp"
#include "Core/Audio/DialogVoiceCodec.hpp"
#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Dialog/DialogRuntime.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/ThreeDM.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Dialog {

std::expected<ThreeDmBinding, std::string> bind_three_dm(
    const Omikron::ThreeDM& clip, const Omikron::Model3DOData& model) {
  if (model.root_mesh_index < 0 ||
      static_cast<std::size_t>(model.root_mesh_index) >= model.meshes.size()) {
    return std::expected<ThreeDmBinding, std::string>{
        std::unexpect, "3DM target model has no resolved root mesh"};
  }
  ThreeDmBinding binding;
  binding.root_mesh_index = static_cast<std::size_t>(model.root_mesh_index);
  binding.object_mesh_indices.reserve(clip.header().object_ids.size());
  std::vector<std::uint32_t> seen_object_ids;
  seen_object_ids.reserve(clip.header().object_ids.size());
  for (const std::uint32_t object_id : clip.header().object_ids) {
    if (std::ranges::find(seen_object_ids, object_id) != seen_object_ids.end()) {
      return std::expected<ThreeDmBinding, std::string>{std::unexpect,
          fmt::format("3DM authored object ID {} occurs more than once", object_id)};
    }
    seen_object_ids.push_back(object_id);
    std::optional<std::size_t> match;
    for (std::size_t mesh_index{0}; mesh_index < model.meshes.size(); ++mesh_index) {
      if (model.meshes.at(mesh_index).script_id != object_id) {
        continue;
      }
      if (match.has_value()) {
        return std::expected<ThreeDmBinding, std::string>{std::unexpect,
            fmt::format("3DM object ID {} matches multiple model script IDs", object_id)};
      }
      match = mesh_index;
    }
    if (!match.has_value()) {
      return std::expected<ThreeDmBinding, std::string>{std::unexpect,
          fmt::format("3DM object ID {} has no model script-ID binding", object_id)};
    }
    binding.object_mesh_indices.push_back(match.value_or(0U));
  }

  const std::uint32_t root_script_id{model.meshes.at(binding.root_mesh_index).script_id};
  std::optional<std::size_t> root_slot;
  for (std::size_t slot{0}; slot < clip.header().object_ids.size(); ++slot) {
    if (clip.header().object_ids.at(slot) != root_script_id) {
      continue;
    }
    if (root_slot.has_value()) {
      return std::expected<ThreeDmBinding, std::string>{std::unexpect,
          fmt::format("3DM root script ID {} occurs more than once", root_script_id)};
    }
    root_slot = slot;
  }
  if (!root_slot.has_value()) {
    return std::expected<ThreeDmBinding, std::string>{std::unexpect,
        fmt::format("3DM does not contain model root script ID {}", root_script_id)};
  }
  binding.root_object_slot = root_slot.value_or(0U);

  std::vector<std::size_t> face_meshes;
  for (std::size_t index{0}; index < model.meshes.size(); ++index) {
    if (Omikron::has_flag(model.meshes.at(index).flags, Omikron::MeshFlags::k_face_morph)) {
      face_meshes.push_back(index);
    }
  }
  if (face_meshes.size() != 1U) {
    binding.face_diagnostic = fmt::format(
        "model has {} face-morph meshes; facial stream disabled", face_meshes.size());
  } else if (model.meshes.at(face_meshes.front()).vertex_count !=
             clip.header().morph_vertex_count) {
    binding.face_diagnostic = fmt::format("face-morph mesh has {} vertices but 3DM has {}",
        model.meshes.at(face_meshes.front()).vertex_count,
        clip.header().morph_vertex_count);
  } else {
    binding.face_mesh_index = face_meshes.front();
  }
  return binding;
}

void DialogPerformanceClock::start(const std::size_t frame_count) {
  m_frame_count = frame_count;
  m_elapsed_seconds = 0.0;
  m_active = frame_count != 0U;
}

void DialogPerformanceClock::reset() {
  m_frame_count = 0;
  m_elapsed_seconds = 0.0;
  m_active = false;
}

std::optional<std::size_t> DialogPerformanceClock::advance(const float real_delta_seconds) {
  if (!m_active) {
    return std::nullopt;
  }
  m_elapsed_seconds += std::max(0.0, static_cast<double>(real_delta_seconds));
  const auto target{static_cast<std::size_t>(std::floor(m_elapsed_seconds * 30.0))};
  if (target >= m_frame_count) {
    m_active = false;
    return std::nullopt;
  }
  return target;
}

DialogPerformanceRuntime::DialogPerformanceRuntime() : m_loader(&load_clip) {}

DialogPerformanceRuntime::DialogPerformanceRuntime(ClipLoader loader) : m_loader(std::move(loader)) {}

std::expected<std::shared_ptr<const Omikron::ThreeDM>, std::string>
DialogPerformanceRuntime::load_clip(const std::string_view basename) {
  auto loaded{load_game_file(fmt::format("MORPH/{}.3dm", basename))};
  if (!loaded) {
    return std::expected<std::shared_ptr<const Omikron::ThreeDM>, std::string>{
        std::unexpect, std::move(loaded).error()};
  }
  auto parsed{Omikron::ThreeDM::load(loaded->bytes)};
  if (!parsed) {
    return std::expected<std::shared_ptr<const Omikron::ThreeDM>, std::string>{
        std::unexpect, std::move(parsed).error()};
  }
  return std::make_shared<const Omikron::ThreeDM>(std::move(parsed).value());
}

void DialogPerformanceRuntime::tick(const float real_delta_seconds,
    const DialogRuntime& dialog,
    Character::Runtime* characters,
    const std::uint64_t world_identity,
    Audio::AudioSystem* audio) {
  const auto presentation{dialog.presentation()};
  const bool should_present{presentation.has_value() &&
                            presentation->state == DialogState::k_presenting_line &&
                            !presentation->face_motion_base.empty()};
  if (!should_present) {
    stop(true);
    m_generation = dialog.generation();
    return;
  }
  if (characters == nullptr) {
    stop(true);
    m_world_identity = world_identity;
    return;
  }
  if (dialog.generation() != m_generation || characters != m_characters ||
      world_identity != m_world_identity) {
    stop(true);
    start_generation(dialog, characters, world_identity, audio);
    return;
  }
  if (!m_clock.active()) {
    return;
  }
  const auto frame{m_clock.advance(real_delta_seconds)};
  if (!frame.has_value()) {
    stop(false);
    return;
  }
  apply_frame(frame.value_or(0U));
}

void DialogPerformanceRuntime::reset() {
  stop(true);
  m_generation = 0;
  m_cache.clear();
}

void DialogPerformanceRuntime::stop_for_world_change() {
  stop(true);
}

void DialogPerformanceRuntime::stop(const bool stop_voice) {
  if (m_characters != nullptr) {
    m_characters->clear_dialog_performance(m_character_id);
  }
  if (stop_voice && m_audio != nullptr) {
    m_audio->stop_dialog_voice();
  }
  m_clip.reset();
  m_binding.reset();
  m_characters = nullptr;
  m_audio = nullptr;
  m_clock.reset();
  m_current_frame.reset();
}

void DialogPerformanceRuntime::start_generation(const DialogRuntime& dialog,
    Character::Runtime* characters,
    const std::uint64_t world_identity,
    Audio::AudioSystem* audio) {
  m_generation = dialog.generation();
  m_world_identity = world_identity;
  const auto presentation{dialog.presentation()};
  if (!presentation.has_value()) {
    return;
  }
  m_character_id = presentation->character_id;
  m_characters = characters;
  m_audio = audio;

  const std::string basename{presentation->face_motion_base};
  const auto cached{m_cache.find(basename)};
  if (cached != m_cache.end()) {
    m_clip = cached->second;
  } else {
    auto loaded{m_loader(basename)};
    if (!loaded) {
      App::Log::warn(LogCategory::Scenario,
          "dialogue performance '{}' unavailable: {}",
          basename,
          loaded.error());
      return;
    }
    m_clip = loaded.value();
    m_cache.emplace(basename, m_clip);
  }
  if (m_clip->frames().empty()) {
    App::Log::warn(LogCategory::Scenario, "dialogue performance '{}' has no visual frames", basename);
    return;
  }

  Audio::DialogAdpcmState decoder_state;
  std::vector<std::int16_t> samples;
  for (std::size_t index{0}; index < m_clip->frames().size(); ++index) {
    if (auto decoded_audio{
            Audio::decode_dialog_adpcm(m_clip->audio_chunk(index), decoder_state, samples)};
        !decoded_audio) {
      App::Log::warn(LogCategory::Audio, "dialogue performance decode failed: {}",
          decoded_audio.error());
      samples.clear();
      break;
    }
  }
  if (m_audio != nullptr && !samples.empty()) {
    if (auto played{m_audio->play_dialog_voice(basename, std::move(samples))}; !played) {
      App::Log::warn(LogCategory::Audio, "dialogue performance voice unavailable: {}", played.error());
    }
  }

  if (m_characters != nullptr) {
    const Character::RuntimeCharacter* character{m_characters->find(m_character_id)};
    if (character != nullptr && character->model_resource != nullptr) {
      auto binding{bind_three_dm(*m_clip, character->model_resource->model)};
      if (binding) {
        m_binding = std::move(binding).value();
        if (!m_binding->face_diagnostic.empty()) {
          App::Log::warn(LogCategory::Scenario, "3DM facial stream: {}", m_binding->face_diagnostic);
        }
      } else {
        App::Log::warn(LogCategory::Scenario, "3DM visual binding failed: {}", binding.error());
      }
    } else {
      App::Log::warn(LogCategory::Scenario,
          "3DM character {} is not active and loaded",
          m_character_id);
    }
  }

  if (m_binding.has_value()) {
    auto first{m_clip->decode_frame(0U, m_binding->root_object_slot)};
    if (first) {
      m_root_origin = first->root_translation;
    } else {
      App::Log::warn(LogCategory::Scenario, "3DM frame zero failed: {}", first.error());
      m_binding.reset();
    }
  }
  m_clock.start(m_clip->frames().size());
  m_current_frame = 0U;
  apply_frame(0U);
}

void DialogPerformanceRuntime::apply_frame(const std::size_t frame_index) {
  m_current_frame = frame_index;
  if (!m_binding.has_value() || m_characters == nullptr || m_clip == nullptr) {
    return;
  }
  const Character::RuntimeCharacter* character{m_characters->find(m_character_id)};
  if (character == nullptr || character->model_resource == nullptr) {
    return;
  }
  auto decoded{m_clip->decode_frame(frame_index, m_binding->root_object_slot)};
  if (!decoded) {
    App::Log::warn(LogCategory::Scenario, "3DM frame {} failed: {}", frame_index, decoded.error());
    m_binding.reset();
    return;
  }
  Character::DialogPerformanceOverlay overlay;
  overlay.object_rotations.resize(character->model_resource->model.meshes.size());
  for (std::size_t slot{0}; slot < m_binding->object_mesh_indices.size(); ++slot) {
    overlay.object_rotations.at(m_binding->object_mesh_indices.at(slot)) =
        decoded->object_rotations.at(slot);
  }
  overlay.root_object_index = m_binding->root_mesh_index;
  overlay.root_translation_delta = Runtime::Vec3{
      .x = decoded->root_translation.x - m_root_origin.x,
      .y = decoded->root_translation.y - m_root_origin.y,
      .z = decoded->root_translation.z - m_root_origin.z};
  overlay.face_mesh_index = m_binding->face_mesh_index;
  if (overlay.face_mesh_index.has_value()) {
    overlay.face_vertices.reserve(decoded->morph_vertices.size());
    for (const Omikron::ThreeDmMorphVertex& vertex : decoded->morph_vertices) {
      overlay.face_vertices.push_back(Character::DialogFaceVertexOverride{
          .position = vertex.position, .normal = vertex.normal});
    }
  }
  if (auto applied{m_characters->apply_dialog_performance(m_character_id, std::move(overlay))};
      !applied) {
    App::Log::warn(LogCategory::Scenario, "3DM pose application failed: {}", applied.error());
    m_binding.reset();
  }
}

}  // namespace App::Dialog
