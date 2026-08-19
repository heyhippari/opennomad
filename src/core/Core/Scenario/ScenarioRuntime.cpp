#include "Core/Scenario/ScenarioRuntime.hpp"

#include <fmt/format.h>

#include <array>
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
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Sprite/SpriteInstance.hpp"
#include "Core/Sprite/SpritePool.hpp"
#include "Core/Sprite/SpriteRenderMode.hpp"
#include "Core/Sprite/SpriteResource.hpp"
#include "Core/Texture.hpp"

namespace App {

std::expected<void, std::string> ScenarioRuntime::initialize(
    const Omikron::ScxData& scx,
    const std::span<const std::byte> scx_bytes,
    const std::string_view scenario_name,
    Audio::AudioSystem* const audio,
    const bool activate_startup_scripts) {
  APP_PROFILE_FUNCTION();

  // Copy the parsed structure and its backing bytes; the parsed offsets refer
  // into m_scx_bytes, which must stay alive alongside m_scx.
  m_scx = scx;
  m_scx_bytes.assign(scx_bytes.begin(), scx_bytes.end());
  m_scenario_name = std::string{scenario_name};
  m_audio = audio;

  const std::size_t count{m_scx.models.size()};
  m_sprite_resources.resize(count);
  m_sprite_resource_ptrs.resize(count);
  m_sprite_textures.resize(count);

  auto runtime{Script::ScriptRuntime::create(m_scx, *this, m_scenario_name)};
  if (!runtime) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("Failed to initialise the script runtime: {}", runtime.error())};
  }
  m_script_runtime = std::move(runtime).value();

  if (activate_startup_scripts) {
    // Startup activation: activate every script that owns at least one command
    // group, in file order. The true startup selection is a separate
    // reverse-engineering checkpoint; file-order activation is the documented
    // inference (docs/ReverseEngineering.md).
    std::size_t active{0};
    for (std::size_t index{0}; index < m_scx.scripts.size(); ++index) {
      const Omikron::ScxScript& script{m_scx.scripts.at(index)};
      if (script.root_command_count == 0U) {
        continue;
      }
      if (auto created{m_script_runtime->create_instance(index)}; created) {
        ++active;
      } else {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format("Failed to create script instance {} '{}': {}",
                index,
                script.name,
                created.error())};
      }
    }
    App::Log::info("Script: activated {} startup script instance(s) from {} parsed scripts",
        active,
        m_scx.scripts.size());
  } else {
    // Boot configuration: all script templates are loaded but inactive.
    App::Log::info("Script: loaded {} script templates (all inactive)", m_scx.scripts.size());
  }

  App::Log::debug("Sprite system ready: {} embedded effect resources", count);
  m_initialized = true;
  return {};
}

Script::ScriptRuntime* ScenarioRuntime::script_runtime() {
  return m_script_runtime.get();
}

const Script::ScriptRuntime* ScenarioRuntime::script_runtime() const {
  return m_script_runtime.get();
}

std::string_view ScenarioRuntime::script_scenario_name() const {
  return m_scenario_name;
}

std::string_view ScenarioRuntime::scenario_name() const {
  return m_scenario_name;
}

std::expected<std::size_t, std::string> ScenarioRuntime::spawn_script_instance(
    const std::size_t source_script_index) {
  if (m_script_runtime == nullptr) {
    return std::expected<std::size_t, std::string>{
        std::unexpect, "script runtime is not initialised"};
  }
  return m_script_runtime->create_instance(source_script_index);
}

void ScenarioRuntime::tick(const float real_delta_seconds) {
  if (m_script_runtime != nullptr) {
    m_script_runtime->tick(real_delta_seconds);
  }
}

Sprite::SpritePool& ScenarioRuntime::sprite_pool() {
  return m_sprite_pool;
}

const Sprite::SpritePool& ScenarioRuntime::sprite_pool() const {
  return m_sprite_pool;
}

std::span<const Sprite::SpriteResource* const> ScenarioRuntime::sprite_resource_ptrs() const {
  return std::span<const Sprite::SpriteResource* const>{m_sprite_resource_ptrs.data(),
      m_sprite_resource_ptrs.size()};
}

std::size_t ScenarioRuntime::sprite_resource_count() const {
  return m_sprite_resources.size();
}

std::string_view ScenarioRuntime::sprite_resource_name(const std::size_t resource_index) const {
  if (resource_index >= m_scx.sprites.size()) {
    return {};
  }
  return m_scx.sprites.at(resource_index).name;
}

const Sprite::SpriteResource* ScenarioRuntime::sprite_resource(
    const std::size_t resource_index) const {
  if (resource_index >= m_sprite_resources.size()) {
    return nullptr;
  }
  return m_sprite_resources.at(resource_index).get();
}

const Texture2D* ScenarioRuntime::sprite_texture(const std::size_t resource_index,
    const std::size_t material_index) const {
  if (resource_index >= m_sprite_textures.size() ||
      material_index >= m_sprite_textures.at(resource_index).size()) {
    return nullptr;
  }
  return &m_sprite_textures.at(resource_index).at(material_index);
}

const std::vector<std::vector<Texture2D>>& ScenarioRuntime::sprite_textures() const {
  return m_sprite_textures;
}

std::expected<void, std::string> ScenarioRuntime::ensure_sprite_resource_loaded(
    const std::size_t resource_index) {
  APP_PROFILE_FUNCTION();

  if (resource_index >= m_sprite_resources.size()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("Sprite resource index {} out of range ({} resources)",
            resource_index,
            m_sprite_resources.size())};
  }
  if (m_sprite_resources.at(resource_index) != nullptr) {
    return {};
  }

  auto resource{Sprite::SpriteResource::create(std::span<const std::byte>{m_scx_bytes},
      m_scx.models.at(resource_index),
      m_scx.sprites.at(resource_index))};
  if (!resource) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("Failed to decode sprite resource {}: {}", resource_index, resource.error())};
  }

  std::vector<Texture2D> textures;
  textures.reserve(resource->images.size());
  for (const Omikron::Texture3DTImage& image : resource->images) {
    auto texture{Texture2D::create(static_cast<int>(image.width),
        static_cast<int>(image.height),
        std::span<const std::uint8_t>{image.rgba8},
        true)};
    if (!texture) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("Failed to upload sprite resource {} texture: {}",
              resource_index,
              texture.error())};
    }
    textures.push_back(std::move(texture).value());
  }

  m_sprite_textures.at(resource_index) = std::move(textures);
  m_sprite_resources.at(resource_index) =
      std::make_unique<Sprite::SpriteResource>(std::move(resource).value());
  m_sprite_resource_ptrs.at(resource_index) = m_sprite_resources.at(resource_index).get();
  App::Log::debug("Decoded sprite resource {} '{}': {} objects, {} textures",
      resource_index,
      m_sprite_resources.at(resource_index)->name,
      m_sprite_resources.at(resource_index)->object_count(),
      m_sprite_resources.at(resource_index)->images.size());
  return {};
}

std::expected<Sprite::SpriteHandle, std::string> ScenarioRuntime::spawn_sprite(
    const std::size_t resource_index,
    const std::size_t object_index,
    const std::array<float, 3> position) {
  APP_PROFILE_FUNCTION();

  if (auto loaded{ensure_sprite_resource_loaded(resource_index)}; !loaded) {
    return std::expected<Sprite::SpriteHandle, std::string>{std::unexpect, loaded.error()};
  }
  const Sprite::SpriteResource* resource{m_sprite_resource_ptrs.at(resource_index)};
  const std::size_t frames{resource->frame_count(object_index)};

  auto handle{m_sprite_pool.create(resource_index, object_index, frames, position)};
  if (!handle) {
    return std::expected<Sprite::SpriteHandle, std::string>{std::unexpect, handle.error()};
  }
  if (auto attached{m_sprite_pool.attach(handle.value())}; !attached) {
    // Best-effort rollback; the handle was never exposed to the caller.
    if (auto destroyed{m_sprite_pool.destroy(handle.value())}; !destroyed) {
      App::Log::warn("Failed to roll back a failed spawn: {}", destroyed.error());
    }
    return std::expected<Sprite::SpriteHandle, std::string>{std::unexpect, attached.error()};
  }
  return handle;
}

std::expected<void, std::string> ScenarioRuntime::attach_sprite(
    const Sprite::SpriteHandle handle) {
  return m_sprite_pool.attach(handle);
}

std::expected<void, std::string> ScenarioRuntime::detach_sprite(
    const Sprite::SpriteHandle handle) {
  return m_sprite_pool.detach(handle);
}

std::expected<void, std::string> ScenarioRuntime::destroy_sprite(
    const Sprite::SpriteHandle handle) {
  return m_sprite_pool.destroy(handle);
}

std::expected<void, std::string> ScenarioRuntime::set_sprite_frame(
    const Sprite::SpriteHandle handle, const std::uint16_t frame_index) {
  return m_sprite_pool.set_frame(handle, frame_index);
}

void ScenarioRuntime::set_sprite_render_mode(
    const Sprite::SpriteHandle handle, const Sprite::SpriteRenderMode mode) {
  m_sprite_pool.set_render_mode(handle, mode);
}

void ScenarioRuntime::set_sprite_type(
    const Sprite::SpriteHandle handle, const std::uint16_t type) {
  m_sprite_pool.set_type(handle, type);
}

void ScenarioRuntime::set_sprite_position(
    const Sprite::SpriteHandle handle, const std::array<float, 3> position) {
  m_sprite_pool.set_position(handle, position);
}

void ScenarioRuntime::set_sprite_scale(
    const Sprite::SpriteHandle handle, const float scale_x, const float scale_y) {
  m_sprite_pool.set_scale(handle, scale_x, scale_y);
}

void ScenarioRuntime::set_sprite_scale_x(
    const Sprite::SpriteHandle handle, const float scale_x) {
  m_sprite_pool.set_scale_x(handle, scale_x);
}

void ScenarioRuntime::set_sprite_scale_y(
    const Sprite::SpriteHandle handle, const float scale_y) {
  m_sprite_pool.set_scale_y(handle, scale_y);
}

void ScenarioRuntime::set_sprite_rotation(
    const Sprite::SpriteHandle handle, const float rotation) {
  m_sprite_pool.set_rotation(handle, rotation);
}

void ScenarioRuntime::set_sprite_tint(
    const Sprite::SpriteHandle handle, const std::array<float, 3> tint) {
  m_sprite_pool.set_tint(handle, tint);
}

void ScenarioRuntime::set_sprite_texture_offset(const Sprite::SpriteHandle handle,
    const float offset_u,
    const float offset_v) {
  m_sprite_pool.set_texture_offset(handle, offset_u, offset_v);
}

void ScenarioRuntime::set_sprite_unknown_24(
    const Sprite::SpriteHandle handle, const float value) {
  m_sprite_pool.set_unknown_24(handle, value);
}

void ScenarioRuntime::reset_sprite_to_defaults(const Sprite::SpriteHandle handle) {
  m_sprite_pool.reset_to_defaults(handle);
}

std::array<float, 3> ScenarioRuntime::world_anchor() const {
  return m_world_anchor;
}

void ScenarioRuntime::set_world_anchor(const std::array<float, 3> anchor) {
  m_world_anchor = anchor;
}

std::expected<Sprite::SpriteHandle, std::string> ScenarioRuntime::ensure_sprite(
    const std::uint32_t source_sprite_index) {
  if (source_sprite_index >= m_scx.sprites.size()) {
    return std::expected<Sprite::SpriteHandle, std::string>{std::unexpect,
        fmt::format("source sprite index {} out of range ({} sprites)",
            source_sprite_index,
            m_scx.sprites.size())};
  }
  if (auto loaded{ensure_sprite_resource_loaded(source_sprite_index)}; !loaded) {
    return std::expected<Sprite::SpriteHandle, std::string>{std::unexpect, loaded.error()};
  }
  const Sprite::SpriteResource* resource{m_sprite_resource_ptrs.at(source_sprite_index)};
  return spawn_sprite(source_sprite_index, resource->default_object_index(), m_world_anchor);
}

std::expected<std::array<float, 3>, std::string> ScenarioRuntime::resolve_position(
    const std::uint32_t xyz_index) {
  // POC simplification: the XYZ pointer pool is not yet parsed from the
  // scenario (it is not part of DEAD0002). Fall back to the world anchor so
  // script-driven sprites stay visible. Documented in docs/ReverseEngineering.md.
  if (!m_xyz_fallback_logged) {
    m_xyz_fallback_logged = true;
    App::Log::debug(
        "Script: XYZ pointer pool not parsed; resolving index {} via the world-anchor fallback",
        xyz_index);
  }
  return m_world_anchor;
}

void ScenarioRuntime::set_audio_system(Audio::AudioSystem* const audio) {
  m_audio = audio;
  if (m_audio == nullptr) {
    return;
  }
  // The emitter resolver maps an owner token back to the sprite position it
  // wraps. It is re-resolved by the audio system once per real frame; the
  // sprite generation makes a destroyed/recreated object detectably invalid.
  m_audio->set_emitter_resolver([this](const Audio::AudioOwnerToken& owner) {
    if (owner.is_null() || owner.scenario != this) {
      return std::optional<Audio::Vec3>{std::nullopt};
    }
    const Sprite::SpriteInstance* sprite{m_sprite_pool.find(
        Sprite::SpriteHandle{.index = owner.object_index, .generation = owner.generation})};
    if (sprite == nullptr) {
      return std::optional<Audio::Vec3>{std::nullopt};
    }
    return std::optional<Audio::Vec3>{sprite->position};
  });
}

Audio::AudioSystem* ScenarioRuntime::audio_system() {
  return m_audio;
}

const Audio::AudioSystem* ScenarioRuntime::audio_system() const {
  return m_audio;
}

std::expected<Audio::SoundDescriptor, std::string> ScenarioRuntime::resolve_sound(
    const std::uint32_t sound_table_index) {
  if (sound_table_index >= m_scx.sounds.size()) {
    return std::expected<Audio::SoundDescriptor, std::string>{std::unexpect,
        fmt::format("scenario sound index {} out of range ({} sounds)",
            sound_table_index,
            m_scx.sounds.size())};
  }

  if (m_sound_resources.size() != m_scx.sounds.size()) {
    m_sound_resources.assign(m_scx.sounds.size(), Audio::SoundResourceId{});
  }

  const Omikron::ScxSoundRecord& record{m_scx.sounds.at(sound_table_index)};
  Audio::SoundDescriptor descriptor{
      .resource = m_sound_resources.at(sound_table_index),
      .name = record.name,
      .h_id = record.h_id,
      .loaded = false};

  // Already loaded (or previously failed): report the cached result.
  if (descriptor.resource.valid()) {
    descriptor.loaded = true;
    return descriptor;
  }

  // Provisional mapping: the DEAD0003 sound table and the appended WAV stream
  // are both in file order, so sound record i corresponds to wave i. A table
  // with more entries than WAV resources leaves the extra records invalid
  // (0xFFFF), matching the original load-failure semantics.
  if (sound_table_index >= m_scx.waves.size() || m_audio == nullptr) {
    descriptor.resource = Audio::SoundResourceId{};
    return descriptor;
  }

  const Omikron::ScxWaveResource& wave{m_scx.waves.at(sound_table_index)};
  if ((wave.payload_offset + wave.payload_size) > m_scx_bytes.size()) {
    App::Log::warn("Audio: sound '{}' wave span out of range", record.name);
    return descriptor;
  }
  const std::span<const std::byte> wav_bytes{
      m_scx_bytes.data() + wave.payload_offset, wave.payload_size};

  const std::string canonical_key{
      m_scenario_name + '#' + std::to_string(sound_table_index) + '#' + record.name};
  auto loaded{m_audio->load_sound(canonical_key,
      m_scenario_name,
      sound_table_index,
      record.name,
      record.h_id,
      wav_bytes)};
  if (!loaded) {
    App::Log::warn("Audio: scenario sound {} '{}' failed to load: {}",
        sound_table_index,
        record.name,
        loaded.error());
    return descriptor;
  }

  descriptor.resource = loaded.value();
  descriptor.loaded = true;
  m_sound_resources.at(sound_table_index) = loaded.value();
  // Mirror the runtime resource handle back into the serialized record so the
  // inspector can correlate it (0xFFFF stays invalid).
  m_scx.sounds.at(sound_table_index).runtime_sound_id = loaded->index;
  return descriptor;
}

std::expected<Audio::AudioOwnerToken, std::string> ScenarioRuntime::resolve_audio_owner(
    const std::int32_t object_index) {
  if (object_index == -1) {
    return Audio::AudioOwnerToken{};  // Null owner (nonspatial playback).
  }
  if (object_index < 0) {
    return std::expected<Audio::AudioOwnerToken, std::string>{std::unexpect,
        fmt::format("negative object index {}", object_index)};
  }
  const std::uint32_t source_index{static_cast<std::uint32_t>(object_index)};
  auto sprite{ensure_sprite(source_index)};
  if (!sprite) {
    return std::expected<Audio::AudioOwnerToken, std::string>{std::unexpect, sprite.error()};
  }
  return Audio::AudioOwnerToken{
      .scenario = this, .object_index = sprite->index, .generation = sprite->generation};
}

std::expected<Audio::Vec3, std::string> ScenarioRuntime::resolve_owner_position(
    const Audio::AudioOwnerToken& owner) {
  if (owner.is_null() || owner.scenario != this) {
    return std::expected<Audio::Vec3, std::string>{std::unexpect, "invalid audio owner"};
  }
  const Sprite::SpriteInstance* sprite{m_sprite_pool.find(
      Sprite::SpriteHandle{.index = owner.object_index, .generation = owner.generation})};
  if (sprite == nullptr) {
    return std::expected<Audio::Vec3, std::string>{
        std::unexpect, fmt::format("owner {} no longer exists", owner.describe())};
  }
  return sprite->position;
}

std::expected<Audio::VoiceHandle, std::string> ScenarioRuntime::play_sound(
    const Audio::SoundPlayRequest& request) {
  if (m_audio == nullptr) {
    return std::expected<Audio::VoiceHandle, std::string>{
        std::unexpect, "audio subsystem unavailable"};
  }
  const std::optional<Audio::VoiceHandle> handle{m_audio->play_sound(request)};
  if (!handle.has_value()) {
    return std::expected<Audio::VoiceHandle, std::string>{
        std::unexpect, "voice allocation/queue failed"};
  }
  return handle.value();
}

void ScenarioRuntime::stop_sound(
    const Audio::SoundResourceId sound, const Audio::AudioOwnerToken& owner) {
  if (m_audio != nullptr) {
    static_cast<void>(m_audio->stop_first(sound, owner));
  }
}

Audio::AudioContextInfo ScenarioRuntime::audio_context() const {
  if (m_audio == nullptr) {
    return Audio::AudioContextInfo{};
  }
  return m_audio->context_info();
}

}  // namespace App
