#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Sprite/SpriteInstance.hpp"

namespace App {
class ScenarioRuntime;
}

namespace App::Sprite {
struct SpriteDrawCommand;
struct SpriteQueueStats;
}  // namespace App::Sprite

namespace App::Debug {

struct SpriteDrawCommandDebugState {
  std::size_t resource_index{0};
  std::int32_t material_index{0};
  std::uint32_t vertex_count{0};
  Sprite::SpriteRenderMode render_mode{Sprite::SpriteRenderMode::k_default};
};

struct SpriteSkipDebugState {
  Sprite::SpriteHandle handle;
  std::string_view reason;
};

/// Snapshot of the actual presentation renderer's last sprite queue.
struct SpriteRenderDebugState {
  const ScenarioRuntime* runtime{nullptr};
  std::size_t attached{0};
  std::size_t visible{0};
  std::size_t drawn{0};
  std::size_t culled{0};
  std::size_t invalid{0};
  std::size_t batches{0};
  std::size_t draw_calls{0};
  std::vector<SpriteDrawCommandDebugState> commands;
  std::vector<SpriteSkipDebugState> skipped;
};

/// Converts authoritative SpriteRenderer diagnostics into a stable debug
/// snapshot. Called only while a consumer needs sprite presentation data.
[[nodiscard]] SpriteRenderDebugState make_sprite_render_debug_state(const ScenarioRuntime* runtime,
    const Sprite::SpriteQueueStats& stats,
    const std::vector<Sprite::SpriteDrawCommand>& commands);

struct WorldMeshHierarchyDebugState {
  std::size_t descriptor_index{0};
  std::uint32_t mesh_id{0};
  std::uint32_t script_id{0};
  std::uint32_t flags{0};
  std::uint32_t mover_flags{0};
  std::string name;

  std::int32_t parent_id{-1};
  std::int32_t first_child_id{-1};
  std::int32_t next_sibling_id{-1};

  bool reachable{false};
  bool root{false};
  bool top_level{false};

  std::uint32_t vertex_count{0};
  std::uint32_t triangle_count{0};
  std::uint32_t rectangle_count{0};
  std::vector<std::string> materials;

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
  std::array<float, 3> local_offset{};
  std::array<float, 3> model_translation{};
  std::array<float, 3> presentation_translation{};
  std::array<float, 9> local_matrix{};
  std::array<float, 9> world_matrix{};
};

struct CinSfxChannelDebugState {
  bool enabled{false};
  bool active{false};
  bool in_window{false};
  std::int32_t definition_id{0};
  std::string definition_name;
  std::int32_t object_reference{0};
  std::optional<std::size_t> resolved_object_index;
  std::string resolved_object_name;
  std::uint32_t resolved_object_script_id{0};
  float start{0.0F};
  float end{0.0F};
  float elapsed{0.0F};
  std::array<float, 3> cached_position{};
  std::size_t emissions_this_execution{0};
  bool attachment_missing{false};
};

struct CinSfxPlaybackDebugState {
  std::size_t script_instance_id{0};
  std::size_t animation_index{0};
  std::uint32_t animation_id{0};
  std::string animation_name;
  std::size_t association_record_index{0};
  std::uint32_t association_id{0};
  float body_previous_progress{0.0F};
  float body_current_progress{0.0F};
  std::array<CinSfxChannelDebugState, 2> channels;
};

struct RuntimeCharacterDebugState {
  Character::BodyIdentity body_identity{0};
  std::size_t instance_id{0};
  std::int16_t character_id{0};
  std::int32_t area_id{0};
  bool active{false};
  bool area_present{false};
  bool loaded{false};
  bool renderable{false};
  std::uint64_t ordinary_actor_service_generation{0};
  std::array<float, 3> physical_candidate_translation{};
  std::array<float, 3> physical_accepted_translation{};
  bool physical_state_initialized{false};
  float physical_horizontal_x_per_tick{0.0F};
  float physical_vertical_velocity{0.0F};
  float physical_horizontal_z_per_tick{0.0F};
  float physical_gravity_delta_per_tick{0.0F};
  std::array<float, 3> horizontal_intended_displacement{};
  std::array<float, 3> horizontal_resolved_displacement{};
  float horizontal_body_radius{0.0F};
  float horizontal_body_top{0.0F};
  float horizontal_body_bottom{0.0F};
  float horizontal_collision_scale{1.0F};
  bool horizontal_body_valid{false};
  bool horizontal_forward_collision{false};
  bool horizontal_depenetrated{false};
  bool horizontal_depenetration_limit_reached{false};
  std::uint32_t horizontal_collision_passes{0};
  std::uint32_t horizontal_depenetration_iterations{0};
  std::optional<std::size_t> horizontal_object_index;
  std::optional<std::uint32_t> horizontal_source_world_scene_id;
  std::optional<std::size_t> horizontal_source_resident_slot;
  std::array<float, 3> horizontal_contact_point{};
  std::array<float, 3> horizontal_response_normal{};
  float horizontal_contact_distance{0.0F};
  bool ceiling_collision_attempted{false};
  bool ceiling_collision_hit{false};
  bool ceiling_collision_clamped{false};
  float ceiling_requested_delta_y{0.0F};
  float ceiling_resolved_delta_y{0.0F};
  float ceiling_body_top{0.0F};
  float ceiling_sphere_radius{0.0F};
  float ceiling_clearance_adjustment{0.0F};
  std::optional<std::size_t> ceiling_object_index;
  std::optional<std::uint32_t> ceiling_source_world_scene_id;
  std::optional<std::size_t> ceiling_source_resident_slot;
  std::array<float, 3> ceiling_contact_point{};
  std::array<float, 3> ceiling_contact_normal{};
  float ceiling_hit_distance{0.0F};
  float ceiling_limit{0.0F};
  bool automatic_heading_applied{false};
  bool spatial_heading_suppression_latch{false};
  bool spatial_heading_suppression_active{false};
  bool mdrot_suppression_active{false};
  Character::AutomaticHeadingSuppressionReason automatic_heading_suppression{
      Character::AutomaticHeadingSuppressionReason::k_none};
  float intended_heading_degrees{0.0F};
  float resolved_heading_degrees{0.0F};
  float heading_delta_degrees{0.0F};
  float yaw_before_degrees{0.0F};
  float yaw_after_degrees{0.0F};
  bool physical_support_valid{false};
  std::optional<Character::SupportClass> physical_support_class;
  std::optional<std::size_t> physical_support_object_index;
  std::optional<std::uint32_t> physical_support_source_world_scene_id;
  std::optional<std::size_t> physical_support_source_resident_slot;
  std::string physical_support_object_name;
  std::array<float, 3> physical_support_point{};
  std::array<float, 3> physical_support_normal{};
  float physical_support_clearance{0.0F};
  float physical_support_gap{0.0F};
  std::optional<std::size_t> physical_alternate_support_object_index;
  std::optional<std::uint32_t> physical_alternate_source_world_scene_id;
  std::optional<std::size_t> physical_alternate_source_resident_slot;
  float physical_alternate_support_clearance{0.0F};
  float physical_previous_primary_relative_y{0.0F};
  float physical_primary_relative_y{0.0F};
  float physical_alternate_relative_y{0.0F};
  float physical_support_delta_term{0.0F};
  float physical_primary_post_movement_gap{0.0F};
  float physical_alternate_gap{0.0F};
  bool physical_history_mode4_condition{false};
  bool physical_support_walkable{false};
  bool physical_grounded{false};
  bool physical_support_special_deferred{false};
  bool physical_small_step_snapped_this_tick{false};
  std::uint32_t physical_support_mover_flags{0};
  bool physical_support_mover_applied{false};
  bool support_mode4_attempted{false};
  bool support_mode4_triggered_by_history{false};
  bool support_mode4_triggered_by_steep_slope{false};
  std::array<float, 3> support_mode4_input{};
  std::array<float, 3> support_mode4_result{};
  bool support_mode4_forward_collision{false};
  bool support_mode4_depenetrated{false};
  std::uint32_t support_mode4_collision_passes{0};
  std::array<float, 3> support_mode4_response_normal{};
  bool steep_physical_terms_seeded{false};
  bool class2_support_eligible{false};
  bool class2_secondary_query_attempted{false};
  bool class2_secondary_hit{false};
  std::optional<std::size_t> class2_secondary_object_index;
  float class2_secondary_distance{0.0F};
  float class2_secondary_gap{0.0F};
  bool class2_triggered_by_gap{false};
  bool class2_triggered_by_slope{false};
  bool class2_triggered_by_special_flag{false};
  bool class2_attachment_applied{false};
  std::array<float, 2> class2_output_terms{};
  std::uint8_t physical_fall_stage{0};
  float physical_accumulated_fall_travel{0.0F};
  float physical_maximum_support_gap{0.0F};
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
  std::size_t selected_object_index{0};
  std::string selected_object;
  std::uint32_t selected_mesh_id{0};
  std::uint32_t selected_script_id{0};
  std::uint32_t selected_triangle_count{0};
  std::uint32_t selected_rectangle_count{0};
  bool selected_is_root{false};
  bool selected_is_actor_object{false};
  std::optional<std::size_t> hierarchy_root_index;
  std::string hierarchy_root_name;
  std::optional<std::size_t> actor_object_index;
  std::string actor_object_name;
  std::uint32_t actor_object_triangle_count{0};
  std::uint32_t actor_object_rectangle_count{0};
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
  std::array<float, 3> logical_actor_delta{};
  std::array<float, 3> accumulated_visual_translation{};
  std::array<float, 3> accumulated_logical_actor_translation{};
  std::vector<RuntimeCharacterObjectPoseDebugState> object_poses;
  std::optional<CinSfxPlaybackDebugState> cin_sfx;

  /// Which subsystem owns the visible base pose (model defaults / scripted
  /// body animation / CTL controller), as a display string.
  std::string pose_owner;

  /// Adventure CTL controller diagnostics; has_controller gates the rest.
  bool has_controller{false};
  std::string ctl_control_set;
  bool ctl_enabled{false};
  bool ctl_direct_control{false};
  std::optional<std::uint32_t> ctl_move_id;
  std::string ctl_move_name;
  std::optional<std::uint32_t> ctl_state_id;
  std::string ctl_animation_key;
  float ctl_previous_progress{0.0F};
  float ctl_current_progress{0.0F};
  float ctl_effective_end{0.0F};
  std::uint32_t ctl_input_mask{0};
  bool ctl_transition_pending{false};
  std::uint32_t ctl_pending_ticks{0};
  std::size_t ctl_callback_queue_size{0};
  std::uint32_t ctl_restart_count{0};
  std::size_t ctl_markers_fired{0};
};

/// Presentation diagnostics exposed by a normal 3D world scene.
///
/// This is debug-facing data only: the debug UI must not need to know which
/// concrete Scene subclass owns the renderer or camera.
struct WorldRenderDebugState {
  bool renderer_ready{false};
  bool color_pipeline_ready{false};
  bool current_scene_a{true};
  std::size_t legacy_stages{0};
  std::size_t legacy_source_draws{0};
  std::size_t legacy_composites{0};

  std::size_t group_count{0};
  std::size_t material_count{0};
  std::size_t mirror_group_count{0};
  std::size_t uv_scroll_u_group_count{0};
  std::size_t uv_scroll_v_group_count{0};
  float uv_phase_u{0.0F};
  float uv_phase_v{0.0F};
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

  bool letterbox_requested{false};
  bool letterbox_transitioning{false};
  float letterbox_amount{0.0F};
  int viewport_width{0};
  int viewport_height{0};
  float letterbox_target_bar_height{0.0F};
  float letterbox_current_bar_height{0.0F};
};

/// Optional debug capability implemented by scenes with 3D presentation
/// state. DebugUI depends on this interface rather than concrete scene types.
class SceneDebugView {
 public:
  virtual ~SceneDebugView() = default;

  [[nodiscard]] virtual std::optional<WorldRenderDebugState> world_render_debug_state() const {
    return std::nullopt;
  }

  [[nodiscard]] virtual std::optional<SpriteRenderDebugState> sprite_render_debug_state() const {
    return std::nullopt;
  }

  /// Runtime-native point suitable for deliberately placing a debug sprite.
  [[nodiscard]] virtual std::optional<std::array<float, 3>> sprite_debug_focus_position() const {
    return std::nullopt;
  }

  [[nodiscard]] virtual bool sprite_grayscale_supported() const {
    return false;
  }
  [[nodiscard]] virtual bool sprite_grayscale_enabled() const {
    return false;
  }
  virtual void set_sprite_grayscale_enabled(bool /*enabled*/) {}

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

  [[nodiscard]] virtual bool geometry_wireframe_supported() const {
    return false;
  }
  [[nodiscard]] virtual bool geometry_wireframe_enabled() const {
    return false;
  }
  virtual void set_geometry_wireframe_enabled(bool /*enabled*/) {}
};

}  // namespace App::Debug
