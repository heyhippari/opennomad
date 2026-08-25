#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/IamScene.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/Texture3DT.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Script {
struct AreaCharacterActivationRequest;
}

namespace App::Character {

/// CPU-side model resource shared by every runtime instance using the same
/// authored character model. GPU presentation resources are owned separately
/// by WorldRenderer.
struct ModelResource {
  std::string name;
  std::string resolved_model_path;
  std::string resolved_texture_path;
  Omikron::Model3DOData model;
  std::vector<Omikron::MaterialGroup> groups;
  std::vector<Omikron::Texture3DTImage> images;
  App::Runtime::Vec3 bounds_center{};
  float bounds_radius{0.0F};
};

struct BodyAnimationObjectPose {
  std::optional<std::uint32_t> channel_index;
  std::optional<std::uint32_t> channel_id;
  std::string channel_name;
  App::Runtime::Quaternion current_quaternion{};
};

/// Instance-local playback and diagnostics for Runtime body animation.
struct BodyAnimationPlayback {
  bool active{false};
  bool completed{false};
  std::size_t selected_object_index{0};
  std::string selected_object_name;
  std::uint32_t animation_descriptor_index{0};
  std::string animation_name;
  std::uint32_t animation_id{0};
  std::uint32_t max_frame_index{0};
  float previous_progress{0.0F};
  float current_progress{1.0F};
  std::uint32_t execution_count{0};
  std::uint32_t execution_limit{0};
  std::uint32_t path_index{0};
  std::string path_name;
  std::uint32_t subpath_index{0};
  std::string subpath_name;
  App::Runtime::Vec3 sampled_path_position{};
  App::Runtime::Vec3 authored_offset{};
  App::Runtime::Vec3 final_anchor{};
  App::Runtime::Vec3 root_motion_delta{};
  App::Runtime::Vec3 accumulated_root_translation{};
  /// Runtime Script_Select*BodyAnimation arguments 4/5/6: additive XYZ Euler
  /// orientation offsets in degrees.
  App::Runtime::Vec3 body_animation_vector{};
  /// Effective presentation orientation after adding body_animation_vector to
  /// the actor's base Runtime transform. The base transform itself remains
  /// logical/controller state and is not overwritten by body choreography.
  App::Runtime::Matrix3 effective_orientation{};
  bool effective_orientation_valid{false};
};

struct DialogFaceVertexOverride {
  App::Runtime::Vec3 position{};
  App::Runtime::Vec3 normal{};
};

/// One temporary 3DM sample composed over the character's current base pose.
struct DialogPerformanceOverlay {
  std::vector<std::optional<App::Runtime::Quaternion>> object_rotations;
  std::size_t root_object_index{0};
  App::Runtime::Vec3 root_translation_delta{};
  std::optional<std::size_t> face_mesh_index;
  std::vector<DialogFaceVertexOverride> face_vertices;
};

/// Persistent logical character materialized in one world runtime.
struct RuntimeCharacter {
  std::size_t instance_id{0};
  std::int16_t character_id{0};
  std::int32_t area_id{0};
  std::optional<std::int32_t> scene_id;
  bool active{false};
  bool area_present{false};
  /// Current-body presentation state. AREA 0x4E/-1 and 0x4F/-1 change this
  /// without changing ownership or destroying the materialized character.
  bool presentation_enabled{true};

  /// Authored AREA/address placement snapshot. Animation/controller motion does
  /// not rewrite this field; live spatial consumers must use transform.translation.
  std::array<std::int32_t, 3> serialized_area_position{};
  std::int16_t serialized_orientation_units{0};
  /// Live Runtime world transform. Body root motion mutates translation and
  /// uses matrix as the actor/root orientation basis.
  App::Runtime::Transform transform{};
  std::int32_t runtime_orientation_degrees{0};
  /// Selected authored CTL move/control-bank ID. Execution is intentionally
  /// deferred until the full CTL controller is recovered.
  std::optional<std::int16_t> current_move_id;
  /// Neutral controller boolean toggled by compact 0x68/0x69.
  bool controller_enabled{false};

  std::string definition_name;
  std::string model_resource_name;
  std::shared_ptr<const ModelResource> model_resource;
  std::vector<Omikron::Model3DOData::RuntimeObjectState> runtime_objects;
  std::vector<BodyAnimationObjectPose> object_poses;
  std::vector<Omikron::MaterialGroup> posed_groups;
  std::uint64_t pose_revision{0};
  BodyAnimationPlayback body_animation;
  std::optional<DialogPerformanceOverlay> dialog_performance;

  [[nodiscard]] bool loaded() const {
    return model_resource != nullptr;
  }

  /// Character transform used by 3D presentation. Runtime keeps the base actor
  /// orientation and the body-animation Euler offsets as separate state; mirror
  /// that split instead of baking animation orientation into transform.matrix.
  [[nodiscard]] App::Runtime::Transform presentation_transform() const {
    App::Runtime::Transform result{transform};
    if (body_animation.effective_orientation_valid) {
      result.matrix = body_animation.effective_orientation;
    }
    return result;
  }

  [[nodiscard]] bool renderable() const {
    return active && area_present && presentation_enabled && model_resource != nullptr &&
           !posed_groups.empty();
  }
};

/// World-owned character lifecycle and CPU resource cache.
class Runtime {
 public:
  using ModelLoader =
      std::function<std::expected<std::shared_ptr<const ModelResource>, std::string>(
          std::string_view model_resource)>;

  Runtime();
  explicit Runtime(ModelLoader model_loader);

  /// Replaces the CPU model loader. Intended for embedding/tests before any
  /// character resources are materialized; already-cached resources remain.
  void set_model_loader(ModelLoader model_loader);

  /// Resolves and activates one AREA request. Successful activation is
  /// immediate and reuses both an existing logical instance and cached model
  /// resources.
  [[nodiscard]] std::expected<void, std::string> activate(std::int32_t area_id,
      const Omikron::IamAreaRecord& area,
      const Script::AreaCharacterActivationRequest& request);

  /// Ensures one AREA table-0 character exists for current-character
  /// selection. An existing body is reactivated in place without resetting
  /// its live transform, animation, overlays, or immutable resource.
  [[nodiscard]] std::expected<void, std::string> ensure_area_character(
      std::int32_t area_id, const Omikron::IamAreaRecord& area, std::int16_t character_id);

  /// Ensures one SCENE table-0 character exists for current-character
  /// selection, using the SCENE's matching table-4 definition on first
  /// materialization only.
  [[nodiscard]] std::expected<void, std::string> ensure_scene_character(std::int32_t area_id,
      std::int32_t scene_id,
      const Omikron::IamSceneRecord& scene,
      std::int16_t character_id);

  /// Materializes every SCENE table-0 character against its SCENE-first
  /// table-4 definitions. This never rematerializes AREA-local characters.
  [[nodiscard]] std::expected<void, std::string> materialize_scene_characters(
      std::int32_t area_id, std::int32_t scene_id, const Omikron::IamSceneRecord& scene);

  /// Deactivates only characters currently owned by one attached SCENE.
  void dematerialize_scene_characters(std::int32_t area_id,
      std::int32_t scene_id,
      std::optional<std::int16_t> preserved_character_id = std::nullopt);

  /// Changes only the presentation bit of an already materialized body.
  [[nodiscard]] std::expected<void, std::string> set_presentation_enabled(
      std::int16_t character_id, bool enabled);

  /// Removes a non-current body from live AREA presentation while retaining
  /// its durable runtime record for a later authored activation.
  [[nodiscard]] std::expected<void, std::string> deactivate_character(std::int16_t character_id);

  /// Moves one complete logical body out of this world. The extracted value
  /// retains every mutable runtime field and its immutable shared resource.
  [[nodiscard]] std::expected<RuntimeCharacter, std::string> extract_character(
      std::int16_t character_id);

  /// Adopts a body moved from another world without invoking the model loader.
  /// The target-local instance ID is assigned on adoption.
  [[nodiscard]] std::expected<void, std::string> adopt_character(RuntimeCharacter character);

  /// Transfers one body to a target world without reloading or duplicating it.
  [[nodiscard]] std::expected<void, std::string> transfer_character_to(
      Runtime& target, std::int16_t character_id);

  /// Applies a named AREA address transform to one established runtime character.
  [[nodiscard]] std::expected<void, std::string> place_character_at_address(
      std::int16_t character_id, const Omikron::IamAreaAddressRecord& address);

  [[nodiscard]] RuntimeCharacter* find(std::int16_t character_id);
  [[nodiscard]] const RuntimeCharacter* find(std::int16_t character_id) const;
  [[nodiscard]] std::span<const RuntimeCharacter> characters() const;
  [[nodiscard]] std::size_t model_resource_count() const;

  /// Restores one character's mutable pose from its immutable shared model.
  void reset_pose(std::int16_t character_id);

  /// Composes a temporary dialogue sample over the current base animation.
  [[nodiscard]] std::expected<void, std::string> apply_dialog_performance(
      std::int16_t character_id, DialogPerformanceOverlay overlay);

  /// Removes only the dialogue overlay and rebuilds the current base pose.
  void clear_dialog_performance(std::int16_t character_id);

 private:
  [[nodiscard]] static std::expected<std::shared_ptr<const ModelResource>, std::string>
  load_model_resource(std::string_view model_resource);

  [[nodiscard]] std::expected<void, std::string> materialize_character(std::int32_t area_id,
      std::optional<std::int32_t> scene_id,
      std::int16_t character_id,
      const std::array<std::int32_t, 3>& serialized_position,
      std::int16_t orientation_units,
      std::string_view definition_name,
      std::string_view model_resource,
      bool apply_transform);

  ModelLoader m_model_loader;
  std::vector<RuntimeCharacter> m_characters;
  std::unordered_map<std::string, std::shared_ptr<const ModelResource>> m_model_resources;
};

}  // namespace App::Character
