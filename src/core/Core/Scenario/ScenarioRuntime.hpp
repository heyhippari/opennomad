#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Omikron/Animation3DA.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Path3DP.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/SFX.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Sfx/SfxRuntime.hpp"
#include "Core/Sprite/SpritePool.hpp"
#include "Core/Sprite/SpriteRenderMode.hpp"
#include "Core/Sprite/SpriteResource.hpp"
#include "Core/Texture.hpp"

namespace App::Audio {
class AudioSystem;
}

namespace App {

/// Scene-independent owner of the SCX gameplay subsystem: the sprite instance
/// pool, the decoded sprite resources and their GPU textures, the SCX script
/// runtime and the scenario sound resources.
///
/// Implements `Script::ScriptWorld` so the script runtime can drive sprites
/// and audio without a 3D scene. Both `ModelViewerScene` (when a 3D world exists)
/// and the debug tools read from this owner; `Sprite::SpriteRenderer` (the GL
/// billboard queue builder/drawer) stays in the presentation scene/renderer
/// because billboards need a 3D camera basis.
class ScenarioRuntime final : public Script::ScriptWorld, private Sfx::Host {
 public:
  ScenarioRuntime() = default;
  ~ScenarioRuntime() override;

  ScenarioRuntime(const ScenarioRuntime&) = delete;
  ScenarioRuntime(ScenarioRuntime&&) = delete;
  ScenarioRuntime& operator=(const ScenarioRuntime&) = delete;
  ScenarioRuntime& operator=(ScenarioRuntime&&) = delete;

  /// Copies the parsed scenario and its backing bytes, then builds the script
  /// runtime. CPU-only: no GL work happens here. When
  /// `activate_startup_scripts` is true every script owning at least one
  /// command group is instantiated in file order (the standalone/SCX load
  /// path); otherwise parsed SCX source scripts remain inactive in OpenNomad's
  /// current execution model (the scenario-manager path).
  [[nodiscard]] std::expected<void, std::string> initialize(const Omikron::ScxData& scx,
      std::span<const std::byte> scx_bytes,
      std::string_view scenario_name,
      Audio::AudioSystem* audio,
      bool activate_startup_scripts,
      const Omikron::SfxData* sfx = nullptr);

  [[nodiscard]] bool initialized() const {
    return m_initialized;
  }

  // --- Script runtime ------------------------------------------------------

  /// The SCX script runtime (null before initialize()).
  [[nodiscard]] Script::ScriptRuntime* script_runtime();
  [[nodiscard]] const Script::ScriptRuntime* script_runtime() const;
  [[nodiscard]] std::string_view script_scenario_name() const;
  /// Creates a runtime instance for one parsed source script.
  [[nodiscard]] std::expected<std::size_t, std::string> spawn_script_instance(
      std::size_t source_script_index);
  /// Creates an SCX instance explicitly bound to an existing active character.
  [[nodiscard]] std::expected<std::size_t, std::string> spawn_character_script_instance(
      std::size_t source_script_index, std::int16_t character_id, std::int16_t parameter);
  /// Advances the script runtime with the real application delta in seconds.
  void tick(float real_delta_seconds);

  /// Optional retail SFX runtime and its concise lifecycle diagnostics.
  [[nodiscard]] Sfx::Runtime* sfx_runtime();
  [[nodiscard]] const Sfx::Runtime* sfx_runtime() const;
  [[nodiscard]] Sfx::Diagnostics sfx_diagnostics() const;

  // --- Runtime characters --------------------------------------------------

  /// Resolves and materializes an AREA character activation in this world.
  [[nodiscard]] std::expected<void, std::string> activate_character(std::int32_t area_id,
      const Omikron::IamAreaRecord& area,
      const Script::AreaCharacterActivationRequest& request);
  [[nodiscard]] Character::Runtime& character_runtime();
  [[nodiscard]] const Character::Runtime& character_runtime() const;

  // --- Sprite instances -----------------------------------------------------

  [[nodiscard]] Sprite::SpritePool& sprite_pool();
  [[nodiscard]] const Sprite::SpritePool& sprite_pool() const;
  /// Decoded resource pointer table consumed by ModelViewerScene's sprite renderer.
  [[nodiscard]] std::span<const Sprite::SpriteResource* const> sprite_resource_ptrs() const;
  /// Number of sprite effect resources indexed from the scenario.
  [[nodiscard]] std::size_t sprite_resource_count() const;
  /// Maps an authored SCX sprite ID to its resource index.
  [[nodiscard]] std::expected<std::size_t, std::string> resolve_authored_sprite_id(
      std::uint16_t authored_sprite_id) const;
  /// The SCX sprite name of a resource (known even before decoding).
  [[nodiscard]] std::string_view sprite_resource_name(std::size_t resource_index) const;
  /// A registered resource, or nullptr when not decoded yet or out of range.
  [[nodiscard]] const Sprite::SpriteResource* sprite_resource(std::size_t resource_index) const;
  /// A decoded resource's GPU texture, or nullptr when not available.
  [[nodiscard]] const Texture2D* sprite_texture(
      std::size_t resource_index, std::size_t material_index) const;
  /// GPU texture sets per resource; ModelViewerScene passes this to
  /// SpriteRenderer::draw_pass.
  [[nodiscard]] const std::vector<std::vector<Texture2D>>& sprite_textures() const;

  /// Decodes the resource on demand, creates an instance and attaches it to
  /// the render list.
  [[nodiscard]] std::expected<Sprite::SpriteHandle, std::string> spawn_sprite(
      std::size_t resource_index, std::size_t object_index, std::array<float, 3> position);
  [[nodiscard]] std::expected<void, std::string> attach_sprite(Sprite::SpriteHandle handle);
  [[nodiscard]] std::expected<void, std::string> detach_sprite(Sprite::SpriteHandle handle);
  [[nodiscard]] std::expected<void, std::string> destroy_sprite(Sprite::SpriteHandle handle);
  void set_sprite_render_mode(Sprite::SpriteHandle handle, Sprite::SpriteRenderMode mode);
  void set_sprite_scale(Sprite::SpriteHandle handle, float scale_x, float scale_y);
  void set_sprite_tint(Sprite::SpriteHandle handle, std::array<float, 3> tint);
  void set_sprite_texture_offset(Sprite::SpriteHandle handle, float offset_u, float offset_v);
  void set_sprite_unknown_24(Sprite::SpriteHandle handle, float value);
  void reset_sprite_to_defaults(Sprite::SpriteHandle handle);

  // --- World anchor ----------------------------------------------------------

  /// Runtime-native inch anchor used as the fallback position for script-driven
  /// sprites and owner resolution until a real XYZ pool is parsed.
  [[nodiscard]] std::array<float, 3> world_anchor() const;
  void set_world_anchor(std::array<float, 3> anchor);

  // --- Script::ScriptWorld ------------------------------------------------
  [[nodiscard]] std::expected<Sprite::SpriteHandle, std::string> ensure_sprite(
      std::uint32_t source_sprite_index) override;
  [[nodiscard]] std::expected<void, std::string> set_sprite_frame(
      Sprite::SpriteHandle handle, std::uint16_t frame_index) override;
  void set_sprite_type(Sprite::SpriteHandle handle, std::uint16_t type) override;
  void set_sprite_position(Sprite::SpriteHandle handle, std::array<float, 3> position) override;
  void set_sprite_scale_x(Sprite::SpriteHandle handle, float scale_x) override;
  void set_sprite_scale_y(Sprite::SpriteHandle handle, float scale_y) override;
  void set_sprite_rotation(Sprite::SpriteHandle handle, float rotation) override;
  [[nodiscard]] std::expected<std::array<float, 3>, std::string> resolve_position(
      std::uint32_t xyz_index) override;
  [[nodiscard]] std::expected<Audio::SoundDescriptor, std::string> resolve_sound(
      std::uint32_t sound_table_index) override;
  [[nodiscard]] std::expected<Audio::AudioOwnerToken, std::string> resolve_audio_owner(
      std::int32_t object_index) override;
  [[nodiscard]] std::expected<Audio::Vec3, std::string> resolve_owner_position(
      const Audio::AudioOwnerToken& owner) override;
  [[nodiscard]] std::expected<Audio::VoiceHandle, std::string> play_sound(
      const Audio::SoundPlayRequest& request) override;
  void stop_sound(Audio::SoundResourceId sound, const Audio::AudioOwnerToken& owner) override;
  [[nodiscard]] Audio::AudioContextInfo audio_context() const override;
  [[nodiscard]] std::expected<Script::RelativeBodyAnimationResult, std::string>
  select_relative_body_animation(const Script::RelativeBodyAnimationRequest& request) override;
  void reset_body_animation(std::int16_t character_id) override;
  [[nodiscard]] std::string_view scenario_name() const override;

  // --- Audio subsystem ------------------------------------------------------

  /// Injects the non-owning audio subsystem (Application owns it).
  void set_audio_system(Audio::AudioSystem* audio);
  [[nodiscard]] Audio::AudioSystem* audio_system();
  [[nodiscard]] const Audio::AudioSystem* audio_system() const;

 private:
  [[nodiscard]] std::expected<std::size_t, std::string> resolve_sfx_sprite_id(
      std::uint16_t authored_sprite_id) const override;
  [[nodiscard]] std::expected<Sfx::SpawnedSprite, std::string> spawn_sfx_sprite(
      std::size_t resource_index, Runtime::Vec3 position) override;
  [[nodiscard]] Sprite::SpriteInstance* find_sfx_sprite(Sprite::SpriteHandle handle) override;
  void destroy_sfx_sprite(Sprite::SpriteHandle handle) override;
  [[nodiscard]] std::optional<Runtime::Transform> resolve_sfx_character_anchor(
      std::int32_t packed_reference_id) const override;

  /// Decodes one embedded sprite resource and uploads its GPU textures on
  /// first use (idempotent).
  [[nodiscard]] std::expected<void, std::string> ensure_sprite_resource_loaded(
      std::size_t resource_index);
  [[nodiscard]] std::expected<const Omikron::Animation3DA*, std::string> animation_resource(
      std::size_t resource_index);
  [[nodiscard]] std::expected<const Omikron::Path3DP*, std::string> path_resource(
      std::size_t resource_index);

  std::vector<std::byte> m_scx_bytes;
  Omikron::ScxData m_scx;
  std::string m_scenario_name;
  std::array<float, 3> m_world_anchor{0.0F, 0.0F, 0.0F};  ///< Runtime XYZ inches.

  Character::Runtime m_character_runtime;
  std::vector<std::unique_ptr<const Omikron::Animation3DA>> m_animation_resources;
  std::vector<std::unique_ptr<const Omikron::Path3DP>> m_path_resources;

  Sprite::SpritePool m_sprite_pool;
  /// Decoded embedded effect resources, indexed like the SCX sprite table.
  std::vector<std::unique_ptr<Sprite::SpriteResource>> m_sprite_resources;
  /// Non-owning pointers parallel to m_sprite_resources (span-friendly).
  std::vector<const Sprite::SpriteResource*> m_sprite_resource_ptrs;
  /// GPU textures per resource, parallel to m_sprite_resources.
  std::vector<std::vector<Texture2D>> m_sprite_textures;
  std::unordered_map<std::uint16_t, std::size_t> m_sprite_id_lookup;

  std::unique_ptr<Script::ScriptRuntime> m_script_runtime;
  std::optional<Omikron::SfxData> m_sfx_data;
  std::unique_ptr<Sfx::Runtime> m_sfx_runtime;
  /// Runtime sound resources parallel to `m_scx.sounds` (lazily loaded).
  std::vector<Audio::SoundResourceId> m_sound_resources;
  /// Non-owning audio subsystem injected by the application.
  Audio::AudioSystem* m_audio{nullptr};
  /// One-shot guard for the XYZ-pool fallback diagnostic (POC).
  bool m_xyz_fallback_logged{false};
  bool m_initialized{false};
};

}  // namespace App
