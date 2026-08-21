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
  App::Runtime::Vec3 body_animation_vector{};
};

/// Persistent logical character materialized in one world runtime.
struct RuntimeCharacter {
  std::size_t instance_id{0};
  std::int16_t character_id{0};
  std::int32_t area_id{0};
  bool active{false};
  bool area_present{false};

  std::array<std::int32_t, 3> serialized_area_position{};
  std::int16_t serialized_orientation_units{0};
  App::Runtime::Transform transform{};
  std::int32_t runtime_orientation_degrees{0};

  std::string definition_name;
  std::string model_resource_name;
  std::shared_ptr<const ModelResource> model_resource;
  std::vector<Omikron::Model3DOData::RuntimeObjectState> runtime_objects;
  std::vector<BodyAnimationObjectPose> object_poses;
  std::vector<Omikron::MaterialGroup> posed_groups;
  std::uint64_t pose_revision{0};
  BodyAnimationPlayback body_animation;

  [[nodiscard]] bool loaded() const {
    return model_resource != nullptr;
  }

  [[nodiscard]] bool renderable() const {
    return active && area_present && model_resource != nullptr && !posed_groups.empty();
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

  [[nodiscard]] RuntimeCharacter* find(std::int16_t character_id);
  [[nodiscard]] const RuntimeCharacter* find(std::int16_t character_id) const;
  [[nodiscard]] std::span<const RuntimeCharacter> characters() const;
  [[nodiscard]] std::size_t model_resource_count() const;

  /// Restores one character's mutable pose from its immutable shared model.
  void reset_pose(std::int16_t character_id);

 private:
  [[nodiscard]] static std::expected<std::shared_ptr<const ModelResource>, std::string>
  load_model_resource(std::string_view model_resource);

  ModelLoader m_model_loader;
  std::vector<RuntimeCharacter> m_characters;
  std::unordered_map<std::string, std::shared_ptr<const ModelResource>> m_model_resources;
};

}  // namespace App::Character
