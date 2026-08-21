#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace App::Debug {

struct WorldMeshHierarchyDebugState {
  std::uint32_t mesh_id{0};
  std::string name;

  std::int32_t parent_id{-1};
  std::int32_t first_child_id{-1};
  std::int32_t next_sibling_id{-1};

  bool reachable{false};
  bool root{false};

  std::array<float, 3> position{};
  std::array<float, 3> bone_position{};
  std::array<float, 3> runtime_local_offset{};
  std::array<float, 9> runtime_local_matrix{};
  std::array<float, 3> runtime_world_translation{};
  std::array<float, 9> runtime_world_matrix{};
};

struct RuntimeCharacterObjectPoseDebugState {
  std::string object_name;
  std::uint32_t script_id{0};
  bool channel_bound{false};
  std::uint32_t channel_id{0};
  std::string channel_name;
  std::array<float, 4> quaternion{};
  std::array<float, 9> local_matrix{};
  std::array<float, 9> world_matrix{};
};

struct RuntimeCharacterDebugState {
  std::size_t instance_id{0};
  std::int16_t character_id{0};
  std::int32_t area_id{0};
  bool active{false};
  bool area_present{false};
  bool loaded{false};
  bool renderable{false};
  std::array<std::int32_t, 3> serialized_position{};
  std::array<float, 3> runtime_position{};
  std::array<float, 3> render_position{};
  std::int16_t serialized_orientation_units{0};
  std::int32_t runtime_orientation_degrees{0};
  std::uint16_t definition_id{0};
  std::string definition_name;
  std::string model_resource;
  std::size_t model_group_count{0};
  std::array<float, 3> runtime_bounds_center{};
  float bounds_radius{0.0F};
  bool body_animation_active{false};
  bool body_animation_completed{false};
  std::string selected_object;
  std::uint32_t selected_mesh_id{0};
  std::uint32_t selected_script_id{0};
  bool selected_is_root{false};
  std::uint32_t animation_descriptor_index{0};
  std::string animation_name;
  std::uint32_t animation_id{0};
  std::uint32_t animation_max_frame{0};
  float animation_previous_progress{0.0F};
  float animation_current_progress{0.0F};
  std::uint32_t animation_execution_count{0};
  std::uint32_t animation_execution_limit{0};
  std::uint32_t path_index{0};
  std::string path_name;
  std::uint32_t subpath_index{0};
  std::string subpath_name;
  std::array<float, 3> sampled_path_position{};
  std::array<float, 3> authored_offset{};
  std::array<float, 3> final_anchor{};
  std::array<float, 3> root_motion_delta{};
  std::array<float, 3> accumulated_root_translation{};
  std::vector<RuntimeCharacterObjectPoseDebugState> object_poses;
};

/// Presentation diagnostics exposed by a normal 3D world scene.
///
/// This is debug-facing data only: the debug UI must not need to know which
/// concrete Scene subclass owns the renderer or camera.
struct WorldRenderDebugState {
  bool renderer_ready{false};

  std::size_t group_count{0};
  std::size_t material_count{0};
  std::size_t mirror_group_count{0};
  std::size_t uv_scroll_u_group_count{0};
  std::size_t uv_scroll_v_group_count{0};
  std::size_t environment_group_count{0};

  std::array<float, 3> bounds_center{};
  float bounds_radius{0.0F};

  std::optional<std::uint32_t> root_mesh_id;
  std::optional<std::size_t> root_mesh_index;
  std::vector<WorldMeshHierarchyDebugState> mesh_hierarchy;
  std::vector<RuntimeCharacterDebugState> runtime_characters;

  bool camera_has_pose{false};
  bool camera_scripted{false};
  bool camera_transitioning{false};
  std::optional<std::uint16_t> camera_id;
  std::array<std::int32_t, 3> camera_serialized_eye{};
  std::array<std::int32_t, 3> camera_serialized_target{};
  std::array<float, 3> camera_runtime_eye{};
  std::array<float, 3> camera_runtime_target{};
  std::array<float, 3> camera_render_eye{};
  std::array<float, 3> camera_render_target{};
  float camera_roll_degrees{0.0F};
  float camera_horizontal_fov_degrees{0.0F};
  float camera_vertical_fov_4_3_degrees{0.0F};
  float camera_near_inches{0.0F};
  float camera_far_inches{0.0F};
};

/// Optional debug capability implemented by scenes with 3D presentation
/// state. DebugUI depends on this interface rather than concrete scene types.
class SceneDebugView {
 public:
  virtual ~SceneDebugView() = default;

  [[nodiscard]] virtual std::optional<WorldRenderDebugState> world_render_debug_state() const {
    return std::nullopt;
  }

  [[nodiscard]] virtual bool light_overlay_supported() const {
    return false;
  }
  [[nodiscard]] virtual bool light_overlay_enabled() const {
    return false;
  }
  virtual void set_light_overlay_enabled(bool /*enabled*/) {}

  [[nodiscard]] virtual bool sprite_overlay_supported() const {
    return false;
  }
  [[nodiscard]] virtual bool sprite_overlay_enabled() const {
    return false;
  }
  virtual void set_sprite_overlay_enabled(bool /*enabled*/) {}
};

}  // namespace App::Debug
