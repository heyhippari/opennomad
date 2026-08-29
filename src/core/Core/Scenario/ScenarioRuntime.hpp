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
#include <unordered_set>
#include <vector>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Object/ObjectPlacementRuntime.hpp"
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

struct CinSfxChannelPlayback {
  bool active{false};
  bool enabled{false};
  bool in_window{false};
  std::int32_t definition_id{0};
  std::string definition_name;
  std::int32_t object_reference{0};
  float elapsed{0.0F};
  float start{0.0F};
  float end{0.0F};
  Runtime::Vec3 cached_position{};
  std::optional<std::size_t> resolved_object_index;
  std::string resolved_object_name;
  std::uint32_t resolved_object_script_id{0};
  std::size_t emissions_this_execution{0};
  bool attachment_missing{false};
};

struct CinSfxPlayback {
  std::size_t character_instance_id{0};
  std::int16_t character_id{0};
  std::size_t script_instance_id{0};
  std::size_t animation_index{0};
  std::uint32_t animation_id{0};
  std::string animation_name;
  std::size_t association_record_index{0};
  std::uint32_t association_id{0};
  std::uint64_t last_service_sequence{0};
  float body_previous_progress{0.0F};
  float body_current_progress{0.0F};
  std::array<CinSfxChannelPlayback, 2> channels;
};

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
  [[nodiscard]] std::span<const CinSfxPlayback> cin_sfx_playbacks() const {
    return m_cin_sfx_playbacks;
  }

  // --- Runtime characters --------------------------------------------------

  /// Resolves and materializes an AREA character activation in this world.
  [[nodiscard]] std::expected<void, std::string> activate_character(std::int32_t area_id,
      const Omikron::IamAreaRecord& area,
      const Script::AreaCharacterActivationRequest& request);
  [[nodiscard]] Character::Runtime& character_runtime();
  [[nodiscard]] const Character::Runtime& character_runtime() const;

  // --- Runtime AREA/SCENE object placements --------------------------------

  [[nodiscard]] ObjectPlacement::Runtime& object_placement_runtime();
  [[nodiscard]] const ObjectPlacement::Runtime& object_placement_runtime() const;

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
  [[nodiscard]] const std::vector<std::vector<GameColorTexture>>& sprite_textures() const;

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
  void set_sprite_diffuse_alpha(Sprite::SpriteHandle handle, float value);
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
  [[nodiscard]] std::expected<void, std::string> select_camera(
      std::string_view camera_name) override;
  [[nodiscard]] std::expected<void, std::string> interpolate_cameras(
      const Script::CameraInterpolationRequest& request) override;
  [[nodiscard]] std::expected<void, std::string> apply_camera_editing_pose(
      const Script::CameraEditingPose& pose) override;
  [[nodiscard]] std::expected<Script::BodyAnimationResult, Script::BodyAnimationFailure>
  select_body_animation(const Script::BodyAnimationRequest& request) override;
  [[nodiscard]] std::expected<Script::RelativeBodyAnimationResult,
      Script::RelativeBodyAnimationFailure>
  select_relative_body_animation(const Script::RelativeBodyAnimationRequest& request) override;
  void reset_body_animation(std::int16_t character_id) override;
  [[nodiscard]] std::expected<Script::MoveObjectOnPathResult, Script::MoveObjectOnPathFailure>
  move_object_on_path_max_parameter(
      std::uint32_t path_descriptor_index, std::uint32_t subpath_index) override;
  [[nodiscard]] std::expected<Script::MoveObjectOnPathResult, Script::MoveObjectOnPathFailure>
  move_object_on_path(const Script::MoveObjectOnPathRequest& request) override;
  [[nodiscard]] std::string_view scenario_name() const override;

  /// Resolves one authored SCX DEAD0003 sound hID (never a sound-table
  /// index) and plays it as a one-shot at the given world position for a CTL
  /// animation audio marker. Missing or unloadable hIDs are nonfatal.
  void play_ctl_sound_marker(std::uint16_t sound_hid, Runtime::Vec3 position);

  /// Binds the context-owned immutable decor descriptor to this scenario's
  /// mutable instance state after transactional world load.
  void bind_decor_model(const Omikron::Model3DOData* decor_model);
  [[nodiscard]] const Omikron::Model3DOData* decor_model() const;
  [[nodiscard]] std::span<const Omikron::Model3DOData::RuntimeObjectState> decor_runtime_objects()
      const;
  [[nodiscard]] std::uint64_t decor_pose_revision() const;
  /// Runtime scene+0x178 equivalent selected by structured SCX SelectCamera.
  /// The returned pointer refers to this runtime's mutable 3DO camera copy.
  [[nodiscard]] const Omikron::CameraRecord* selected_structured_camera() const;

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
  [[nodiscard]] std::expected<void, std::string> play_sfx_sound(
      std::int32_t authored_h_id, Runtime::Vec3 position) override;
  [[nodiscard]] std::string_view sfx_scenario_name() const override {
    return m_scenario_name;
  }
  [[nodiscard]] std::string_view sfx_sound_name(std::int32_t authored_h_id) const override;

  /// Decodes one embedded sprite resource and uploads its GPU textures on
  /// first use (idempotent).
  [[nodiscard]] std::expected<void, std::string> ensure_sprite_resource_loaded(
      std::size_t resource_index);
  [[nodiscard]] std::expected<const Omikron::Animation3DA*, std::string> animation_resource(
      std::size_t resource_index);
  [[nodiscard]] bool should_log_body_animation_identity(
      std::int16_t character_id, std::uint32_t animation_index, std::size_t selected_object_index);
  [[nodiscard]] CinSfxPlayback& ensure_cin_sfx_playback(
      const Character::RuntimeCharacter& character,
      std::size_t script_instance_id,
      std::size_t animation_index,
      std::size_t record_index,
      const Omikron::SfxData& sfx_data);
  [[nodiscard]] static std::optional<std::size_t> find_cin_sfx_attachment(
      const Character::RuntimeCharacter& character, std::uint32_t script_id);
  void service_cin_sfx_channel(
      Character::RuntimeCharacter& character, CinSfxPlayback& playback, std::size_t channel_index);
  [[nodiscard]] std::expected<const Omikron::Path3DP*, std::string> path_resource(
      std::size_t resource_index);
  void service_cin_sfx(Character::RuntimeCharacter& character,
      std::size_t script_instance_id,
      std::size_t animation_index);

  std::vector<std::byte> m_scx_bytes;
  Omikron::ScxData m_scx;
  std::string m_scenario_name;
  std::array<float, 3> m_world_anchor{0.0F, 0.0F, 0.0F};  ///< Runtime XYZ inches.

  Character::Runtime m_character_runtime;
  ObjectPlacement::Runtime m_object_placement_runtime;
  std::vector<std::unique_ptr<const Omikron::Animation3DA>> m_animation_resources;
  std::vector<std::unique_ptr<const Omikron::Path3DP>> m_path_resources;

  Sprite::SpritePool m_sprite_pool;
  /// Decoded embedded effect resources, indexed like the SCX sprite table.
  std::vector<std::unique_ptr<Sprite::SpriteResource>> m_sprite_resources;
  /// Non-owning pointers parallel to m_sprite_resources (span-friendly).
  std::vector<const Sprite::SpriteResource*> m_sprite_resource_ptrs;
  /// GPU textures per resource, parallel to m_sprite_resources.
  std::vector<std::vector<GameColorTexture>> m_sprite_textures;
  std::unordered_map<std::uint16_t, std::size_t> m_sprite_id_lookup;

  std::unique_ptr<Script::ScriptRuntime> m_script_runtime;
  std::optional<Omikron::SfxData> m_sfx_data;
  std::unique_ptr<Sfx::Runtime> m_sfx_runtime;
  std::vector<std::optional<std::size_t>> m_cin_sfx_bindings;
  std::vector<CinSfxPlayback> m_cin_sfx_playbacks;
  std::uint64_t m_cin_sfx_service_sequence{0};
  std::unordered_set<std::uint64_t> m_logged_body_animation_identities;
  /// Runtime sound resources parallel to `m_scx.sounds` (lazily loaded).
  std::vector<Audio::SoundResourceId> m_sound_resources;
  /// Non-owning audio subsystem injected by the application.
  Audio::AudioSystem* m_audio{nullptr};
  /// One-shot guard for the XYZ-pool fallback diagnostic (POC).
  bool m_xyz_fallback_logged{false};
  const Omikron::Model3DOData* m_decor_model{nullptr};
  std::vector<Omikron::Model3DOData::RuntimeObjectState> m_decor_runtime_objects;
  std::vector<Omikron::CameraRecord> m_decor_cameras;
  std::optional<std::size_t> m_selected_decor_camera_index;

  /// Distinguishes between decor cameras and generated editing cameras.
  enum class StructuredCameraSource {
    k_none,
    k_decor,
    k_camera_editing,
  };
  StructuredCameraSource m_structured_camera_source{StructuredCameraSource::k_none};

  /// Generated camera from DEAD000A camera-editing evaluation.
  std::optional<Omikron::CameraRecord> m_camera_editing_camera;

  std::uint64_t m_decor_pose_revision{0};
  bool m_initialized{false};
};

}  // namespace App
