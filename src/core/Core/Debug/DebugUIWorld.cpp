#include <fmt/format.h>
#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Core/Debug/DebugEvidence.hpp"
#include "Core/Debug/DebugUI.hpp"
#include "Core/Debug/DebugUIInternal.hpp"
#include "Core/Debug/Metrics.hpp"
#include "Core/Debug/SceneDebugView.hpp"
#include "Core/Interface/I2DModel.hpp"
#include "Core/Interface/InterfaceDispatcher.hpp"
#include "Core/Interface/InterfaceManager.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Scenario/ScenarioEngine.hpp"
#include "Core/Scenario/ScenarioRuntime.hpp"
#include "Core/Sprite/SpriteFrame.hpp"
#include "Core/Sprite/SpriteInstance.hpp"
#include "Core/Sprite/SpritePool.hpp"
#include "Core/Sprite/SpriteRenderMode.hpp"
#include "Core/Sprite/SpriteResource.hpp"
#include "Core/Texture.hpp"

namespace App::Debug {

void DebugUI::show_world_inspector() {
  ImGui::Begin("World Inspector", &m_show_world_inspector);

  auto* view{dynamic_cast<Debug::SceneDebugView*>(m_context.scene)};
  if (view == nullptr) {
    ImGui::TextUnformatted("The active scene exposes no 3D debug information.");
    ImGui::End();
    return;
  }

  if (const auto world{view->world_render_debug_state()}; world.has_value()) {
    ImGui::SeparatorText("World / Renderer");
    ImGui::Text("Renderer: %s", world->renderer_ready ? "ready" : "not ready");
    ImGui::Text("Groups: %zu", world->group_count);
    ImGui::Text("Materials: %zu", world->material_count);
    ImGui::Text("Bounds center: %.3f, %.3f, %.3f",
        static_cast<double>(world->bounds_center.at(0)),
        static_cast<double>(world->bounds_center.at(1)),
        static_cast<double>(world->bounds_center.at(2)));
    ImGui::Text("Bounds radius: %.3f", static_cast<double>(world->bounds_radius));

    ImGui::Text("Root mesh: %s",
        world->root_mesh_id.has_value()
            ? fmt::format(
                  "{} (index {})", world->root_mesh_id.value(), world->root_mesh_index.value_or(0U))
                  .c_str()
            : "unavailable");

    ImGui::SeparatorText("Geometry / hierarchy");
    constexpr ImGuiTableFlags k_hierarchy_flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                ImGuiTableFlags_ScrollY |
                                                ImGuiTableFlags_SizingFixedFit};
    if (ImGui::CollapsingHeader("3DO hierarchy", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImGui::BeginTable("##3DOHierarchy", 7, k_hierarchy_flags, ImVec2{0.0F, 220.0F})) {
      ImGui::TableSetupColumn("ID");
      ImGui::TableSetupColumn("Name");
      ImGui::TableSetupColumn("Parent");
      ImGui::TableSetupColumn("Child");
      ImGui::TableSetupColumn("Sibling");
      ImGui::TableSetupColumn("Live");
      ImGui::TableSetupColumn("Runtime world T");
      ImGui::TableHeadersRow();
      for (const Debug::WorldMeshHierarchyDebugState& mesh : world->mesh_hierarchy) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text(mesh.root ? "%u *" : "%u", mesh.mesh_id);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(mesh.name.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%d", mesh.parent_id);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%d", mesh.first_child_id);
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%d", mesh.next_sibling_id);
        ImGui::TableSetColumnIndex(5);
        ImGui::TextUnformatted(mesh.reachable ? "yes" : "no");
        ImGui::TableSetColumnIndex(6);
        ImGui::Text("%.3f, %.3f, %.3f",
            static_cast<double>(mesh.runtime_world_translation.at(0)),
            static_cast<double>(mesh.runtime_world_translation.at(1)),
            static_cast<double>(mesh.runtime_world_translation.at(2)));
        if (ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::Text("Serialized position: %.3f, %.3f, %.3f",
              static_cast<double>(mesh.position.at(0)),
              static_cast<double>(mesh.position.at(1)),
              static_cast<double>(mesh.position.at(2)));
          ImGui::Text("Bone position: %.3f, %.3f, %.3f",
              static_cast<double>(mesh.bone_position.at(0)),
              static_cast<double>(mesh.bone_position.at(1)),
              static_cast<double>(mesh.bone_position.at(2)));
          ImGui::Text("Runtime local offset: %.3f, %.3f, %.3f",
              static_cast<double>(mesh.runtime_local_offset.at(0)),
              static_cast<double>(mesh.runtime_local_offset.at(1)),
              static_cast<double>(mesh.runtime_local_offset.at(2)));
          ImGui::TextUnformatted("Runtime local matrix (rows):");
          for (std::size_t row{0}; row < 3U; ++row) {
            ImGui::Text("  %.3f %.3f %.3f",
                static_cast<double>(mesh.runtime_local_matrix.at(row * 3U)),
                static_cast<double>(mesh.runtime_local_matrix.at((row * 3U) + 1U)),
                static_cast<double>(mesh.runtime_local_matrix.at((row * 3U) + 2U)));
          }
          ImGui::TextUnformatted("Runtime world matrix (rows):");
          for (std::size_t row{0}; row < 3U; ++row) {
            ImGui::Text("  %.3f %.3f %.3f",
                static_cast<double>(mesh.runtime_world_matrix.at(row * 3U)),
                static_cast<double>(mesh.runtime_world_matrix.at((row * 3U) + 1U)),
                static_cast<double>(mesh.runtime_world_matrix.at((row * 3U) + 2U)));
          }
          ImGui::EndTooltip();
        }
      }
      ImGui::EndTable();
    }

    if (ImGui::CollapsingHeader("Mesh transform details")) {
      ImGui::TextDisabled("Source -> Runtime local -> Runtime world");
      for (const Debug::WorldMeshHierarchyDebugState& mesh : world->mesh_hierarchy) {
        ImGui::PushID(static_cast<int>(mesh.mesh_id));
        if (ImGui::TreeNode(mesh.name.c_str())) {
          constexpr ImGuiTableFlags k_transform_flags{
              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg};
          if (ImGui::BeginTable("##MeshTransforms", 3, k_transform_flags)) {
            ImGui::TableSetupColumn("Stage");
            ImGui::TableSetupColumn("Position / offset");
            ImGui::TableSetupColumn("Basis");
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Source");
            ImGui::TableNextColumn();
            ImGui::Text("%.3f, %.3f, %.3f",
                static_cast<double>(mesh.position.at(0)),
                static_cast<double>(mesh.position.at(1)),
                static_cast<double>(mesh.position.at(2)));
            ImGui::TableNextColumn();
            ImGui::Text("bone %.3f, %.3f, %.3f",
                static_cast<double>(mesh.bone_position.at(0)),
                static_cast<double>(mesh.bone_position.at(1)),
                static_cast<double>(mesh.bone_position.at(2)));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Runtime local");
            ImGui::TableNextColumn();
            ImGui::Text("%.3f, %.3f, %.3f",
                static_cast<double>(mesh.runtime_local_offset.at(0)),
                static_cast<double>(mesh.runtime_local_offset.at(1)),
                static_cast<double>(mesh.runtime_local_offset.at(2)));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("instance-local pose");
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Runtime world");
            ImGui::TableNextColumn();
            ImGui::Text("%.3f, %.3f, %.3f",
                static_cast<double>(mesh.runtime_world_translation.at(0)),
                static_cast<double>(mesh.runtime_world_translation.at(1)),
                static_cast<double>(mesh.runtime_world_translation.at(2)));
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("hierarchy-resolved pose");
            ImGui::EndTable();
          }
          if (ImGui::TreeNode("Matrices")) {
            ImGui::TextUnformatted("Runtime local matrix (rows):");
            for (std::size_t row{0}; row < 3U; ++row) {
              ImGui::Text("  %.3f %.3f %.3f",
                  static_cast<double>(mesh.runtime_local_matrix.at(row * 3U)),
                  static_cast<double>(mesh.runtime_local_matrix.at((row * 3U) + 1U)),
                  static_cast<double>(mesh.runtime_local_matrix.at((row * 3U) + 2U)));
            }
            ImGui::TextUnformatted("Runtime world matrix (rows):");
            for (std::size_t row{0}; row < 3U; ++row) {
              ImGui::Text("  %.3f %.3f %.3f",
                  static_cast<double>(mesh.runtime_world_matrix.at(row * 3U)),
                  static_cast<double>(mesh.runtime_world_matrix.at((row * 3U) + 1U)),
                  static_cast<double>(mesh.runtime_world_matrix.at((row * 3U) + 2U)));
            }
            ImGui::TreePop();
          }
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
    }

    ImGui::TextUnformatted("Special material / mesh flags");
    ImGui::Text("Mirror: %zu", world->mirror_group_count);
    ImGui::Text("UV scroll U: %zu", world->uv_scroll_u_group_count);
    ImGui::Text("UV scroll V: %zu", world->uv_scroll_v_group_count);
    ImGui::Text("Environment mapped: %zu", world->environment_group_count);
    const std::size_t unsupported_special_groups{
        world->mirror_group_count + world->environment_group_count};
    if (unsupported_special_groups != 0U) {
      ImGui::TextColored(K_WARNING_COLOR,
          "%zu group(s) currently use WorldRenderer's fallback base pass.",
          unsupported_special_groups);
    }
    const std::size_t uv_scroll_groups{
        world->uv_scroll_u_group_count + world->uv_scroll_v_group_count};
    if (uv_scroll_groups != 0U) {
      ImGui::TextColored(K_WARNING_COLOR,
          "%zu UV-scroll flag occurrence(s); Runtime UV phase is not yet applied.",
          uv_scroll_groups);
    }

    ImGui::SeparatorText("Characters");
    ImGui::Text("Count: %zu", world->runtime_characters.size());
    constexpr ImGuiTableFlags k_character_flags{
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit};
    if (ImGui::BeginTable("##Characters", 8, k_character_flags)) {
      ImGui::TableSetupColumn("Instance");
      ImGui::TableSetupColumn("ID");
      ImGui::TableSetupColumn("AREA");
      ImGui::TableSetupColumn("Active");
      ImGui::TableSetupColumn("Loaded");
      ImGui::TableSetupColumn("Renderable");
      ImGui::TableSetupColumn("Definition");
      ImGui::TableSetupColumn("Model");
      ImGui::TableHeadersRow();
      for (const Debug::RuntimeCharacterDebugState& character : world->runtime_characters) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%zu", character.instance_id);
        ImGui::TableNextColumn();
        ImGui::Text("%d", character.character_id);
        ImGui::TableNextColumn();
        ImGui::Text("%d", character.area_id);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(character.active ? "yes" : "no");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(character.loaded ? "yes" : "no");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(character.renderable ? "yes" : "no");
        ImGui::TableNextColumn();
        ImGui::Text("%u %s", character.definition_id, character.definition_name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(character.model_resource.c_str());
      }
      ImGui::EndTable();
    }
    ImGui::TextDisabled("Expand a character for Source -> Runtime -> Presentation detail.");
    for (const Debug::RuntimeCharacterDebugState& character : world->runtime_characters) {
      const std::string label{fmt::format(
          "Character {}##RuntimeCharacter{}", character.character_id, character.instance_id)};
      if (!ImGui::CollapsingHeader(label.c_str())) {
        continue;
      }
      ImGui::Indent();
      ImGui::Text("Instance: %zu", character.instance_id);
      ImGui::Text("AREA: %d", character.area_id);
      ImGui::Text("Active: %s", character.active ? "yes" : "no");
      ImGui::Text("AREA present: %s", character.area_present ? "yes" : "no");
      constexpr ImGuiTableFlags k_pose_flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg};
      if (ImGui::BeginTable("##CharacterPose", 3, k_pose_flags)) {
        ImGui::TableSetupColumn("Stage");
        ImGui::TableSetupColumn("Position");
        ImGui::TableSetupColumn("Orientation");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Source");
        ImGui::TableNextColumn();
        ImGui::Text("%d, %d, %d",
            character.serialized_position.at(0),
            character.serialized_position.at(1),
            character.serialized_position.at(2));
        ImGui::TableNextColumn();
        ImGui::Text("%d units", character.serialized_orientation_units);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Runtime");
        ImGui::TableNextColumn();
        ImGui::Text("%.3f, %.3f, %.3f",
            static_cast<double>(character.runtime_position.at(0)),
            static_cast<double>(character.runtime_position.at(1)),
            static_cast<double>(character.runtime_position.at(2)));
        ImGui::TableNextColumn();
        ImGui::Text("%d deg", character.runtime_orientation_degrees);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Presentation");
        ImGui::TableNextColumn();
        ImGui::Text("%.3f, %.3f, %.3f",
            static_cast<double>(character.render_position.at(0)),
            static_cast<double>(character.render_position.at(1)),
            static_cast<double>(character.render_position.at(2)));
        ImGui::TableNextColumn();
        ImGui::TextDisabled("not exposed");
        ImGui::EndTable();
      }
      ImGui::Text("Definition: %u %s",
          static_cast<unsigned int>(character.definition_id),
          character.definition_name.c_str());
      ImGui::Text("Model resource: %s", character.model_resource.c_str());
      ImGui::Text("Loaded: %s", character.loaded ? "yes" : "no");
      ImGui::Text("Renderable: %s", character.renderable ? "yes" : "no");
      ImGui::Text("Model groups: %zu", character.model_group_count);
      ImGui::Text("Runtime bounds: center %.3f, %.3f, %.3f radius %.3f",
          static_cast<double>(character.runtime_bounds_center.at(0)),
          static_cast<double>(character.runtime_bounds_center.at(1)),
          static_cast<double>(character.runtime_bounds_center.at(2)),
          static_cast<double>(character.bounds_radius));
      if (!character.selected_object.empty()) {
        ImGui::SeparatorText("Animation");
        ImGui::TextUnformatted("Source / selection");
        ImGui::Text("Selected: %s (mesh %u, script %u, %s)",
            character.selected_object.c_str(),
            character.selected_mesh_id,
            character.selected_script_id,
            character.selected_is_root ? "root" : "non-root");
        ImGui::Text("Animation: [%u] %s (id %u, max %u)",
            character.animation_descriptor_index,
            character.animation_name.c_str(),
            character.animation_id,
            character.animation_max_frame);
        ImGui::TextUnformatted("Playback");
        ImGui::Text("Progress: %.3f -> %.3f, execution %u/%u (%s)",
            static_cast<double>(character.animation_previous_progress),
            static_cast<double>(character.animation_current_progress),
            character.animation_execution_count,
            character.animation_execution_limit,
            character.body_animation_completed ? "completed" : "active");
        ImGui::TextUnformatted("Path");
        ImGui::Text("Selection: [%u] %s / [%u] %s",
            character.path_index,
            character.path_name.c_str(),
            character.subpath_index,
            character.subpath_name.c_str());
        ImGui::Text("Sampled XYZ: %.3f, %.3f, %.3f",
            static_cast<double>(character.sampled_path_position.at(0)),
            static_cast<double>(character.sampled_path_position.at(1)),
            static_cast<double>(character.sampled_path_position.at(2)));
        ImGui::Text("Authored offset: %.3f, %.3f, %.3f",
            static_cast<double>(character.authored_offset.at(0)),
            static_cast<double>(character.authored_offset.at(1)),
            static_cast<double>(character.authored_offset.at(2)));
        ImGui::Text("Final anchor: %.3f, %.3f, %.3f",
            static_cast<double>(character.final_anchor.at(0)),
            static_cast<double>(character.final_anchor.at(1)),
            static_cast<double>(character.final_anchor.at(2)));
        ImGui::TextUnformatted("Root motion");
        ImGui::Text("Delta: %.3f, %.3f, %.3f (accum %.3f, %.3f, %.3f)",
            static_cast<double>(character.root_motion_delta.at(0)),
            static_cast<double>(character.root_motion_delta.at(1)),
            static_cast<double>(character.root_motion_delta.at(2)),
            static_cast<double>(character.accumulated_root_translation.at(0)),
            static_cast<double>(character.accumulated_root_translation.at(1)),
            static_cast<double>(character.accumulated_root_translation.at(2)));
        if (ImGui::TreeNode("Object pose")) {
          for (std::size_t pose_index{0}; pose_index < character.object_poses.size();
              ++pose_index) {
            const Debug::RuntimeCharacterObjectPoseDebugState& pose{
                character.object_poses.at(pose_index)};
            ImGui::PushID(static_cast<int>(pose_index));
            if (ImGui::TreeNode(pose.object_name.c_str())) {
              ImGui::Text("script_id: %u", pose.script_id);
              ImGui::Text("channel: %s%u %s",
                  pose.channel_bound ? "" : "unbound / ",
                  pose.channel_id,
                  pose.channel_name.c_str());
              ImGui::Text("quaternion wxyz: %.4f, %.4f, %.4f, %.4f",
                  static_cast<double>(pose.quaternion.at(0)),
                  static_cast<double>(pose.quaternion.at(1)),
                  static_cast<double>(pose.quaternion.at(2)),
                  static_cast<double>(pose.quaternion.at(3)));
              ImGui::Text("local matrix: [%.3f %.3f %.3f] [%.3f %.3f %.3f] [%.3f %.3f %.3f]",
                  static_cast<double>(pose.local_matrix.at(0)),
                  static_cast<double>(pose.local_matrix.at(1)),
                  static_cast<double>(pose.local_matrix.at(2)),
                  static_cast<double>(pose.local_matrix.at(3)),
                  static_cast<double>(pose.local_matrix.at(4)),
                  static_cast<double>(pose.local_matrix.at(5)),
                  static_cast<double>(pose.local_matrix.at(6)),
                  static_cast<double>(pose.local_matrix.at(7)),
                  static_cast<double>(pose.local_matrix.at(8)));
              ImGui::Text("world matrix: [%.3f %.3f %.3f] [%.3f %.3f %.3f] [%.3f %.3f %.3f]",
                  static_cast<double>(pose.world_matrix.at(0)),
                  static_cast<double>(pose.world_matrix.at(1)),
                  static_cast<double>(pose.world_matrix.at(2)),
                  static_cast<double>(pose.world_matrix.at(3)),
                  static_cast<double>(pose.world_matrix.at(4)),
                  static_cast<double>(pose.world_matrix.at(5)),
                  static_cast<double>(pose.world_matrix.at(6)),
                  static_cast<double>(pose.world_matrix.at(7)),
                  static_cast<double>(pose.world_matrix.at(8)));
              ImGui::TreePop();
            }
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
      }
      ImGui::Unindent();
    }

    ImGui::SeparatorText("Camera");
    ImGui::Text("Pose: %s", world->camera_has_pose ? "yes" : "no");
    ImGui::Text("Source: %s", world->camera_scripted ? "scripted" : "fallback");
    ImGui::Text("Transitioning: %s", world->camera_transitioning ? "yes" : "no");
    if (world->camera_id.has_value()) {
      ImGui::Text("AREA camera: %u", static_cast<unsigned int>(world->camera_id.value()));
    } else {
      ImGui::TextUnformatted("AREA camera: none");
    }
    if (world->camera_has_pose) {
      constexpr ImGuiTableFlags k_camera_flags{ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg};
      if (ImGui::BeginTable("##CameraPose", 3, k_camera_flags)) {
        ImGui::TableSetupColumn("Stage");
        ImGui::TableSetupColumn("Eye");
        ImGui::TableSetupColumn("Target");
        ImGui::TableHeadersRow();
        if (world->camera_scripted) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted("Source");
          ImGui::TableNextColumn();
          ImGui::Text("%d, %d, %d",
              world->camera_serialized_eye.at(0),
              world->camera_serialized_eye.at(1),
              world->camera_serialized_eye.at(2));
          ImGui::TableNextColumn();
          ImGui::Text("%d, %d, %d",
              world->camera_serialized_target.at(0),
              world->camera_serialized_target.at(1),
              world->camera_serialized_target.at(2));
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Runtime (in)");
        ImGui::TableNextColumn();
        ImGui::Text("%.3f, %.3f, %.3f",
            static_cast<double>(world->camera_runtime_eye.at(0)),
            static_cast<double>(world->camera_runtime_eye.at(1)),
            static_cast<double>(world->camera_runtime_eye.at(2)));
        ImGui::TableNextColumn();
        ImGui::Text("%.3f, %.3f, %.3f",
            static_cast<double>(world->camera_runtime_target.at(0)),
            static_cast<double>(world->camera_runtime_target.at(1)),
            static_cast<double>(world->camera_runtime_target.at(2)));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Presentation");
        ImGui::TableNextColumn();
        ImGui::Text("%.3f, %.3f, %.3f",
            static_cast<double>(world->camera_render_eye.at(0)),
            static_cast<double>(world->camera_render_eye.at(1)),
            static_cast<double>(world->camera_render_eye.at(2)));
        ImGui::TableNextColumn();
        ImGui::Text("%.3f, %.3f, %.3f",
            static_cast<double>(world->camera_render_target.at(0)),
            static_cast<double>(world->camera_render_target.at(1)),
            static_cast<double>(world->camera_render_target.at(2)));
        ImGui::EndTable();
      }
      ImGui::Text("Roll: %.1f deg", static_cast<double>(world->camera_roll_degrees));
      ImGui::Text(
          "Horizontal FOV: %.1f deg", static_cast<double>(world->camera_horizontal_fov_degrees));
      ImGui::Text("Derived 4:3 vertical FOV: %.3f deg",
          static_cast<double>(world->camera_vertical_fov_4_3_degrees));
      ImGui::Text("Clip: %.3f .. %.3f in (far %.3f m)",
          static_cast<double>(world->camera_near_inches),
          static_cast<double>(world->camera_far_inches),
          static_cast<double>(world->camera_far_inches * 0.0254F));
    }
    ImGui::SeparatorText("Presentation / cinematic");
    ImGui::Text("Letterbox requested: %s", world->letterbox_requested ? "yes" : "no");
    ImGui::Text("Current amount: %.3f", static_cast<double>(world->letterbox_amount));
    ImGui::Text("Transitioning: %s", world->letterbox_transitioning ? "yes" : "no");
    ImGui::Text("Runtime behavior: 2/15 height per bar (~1.818:1 at 640x480)");
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", evidence_label(EvidenceConfidence::k_confirmed_runtime));
    ImGui::Text("OpenNomad presentation: 1.85:1 target");
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", evidence_label(EvidenceConfidence::k_open_nomad_only));
    ImGui::Text("Viewport: %d x %d", world->viewport_width, world->viewport_height);
    ImGui::Text(
        "Full target bar: %.2f px", static_cast<double>(world->letterbox_target_bar_height));
    ImGui::Text("Current bar: %.2f px", static_cast<double>(world->letterbox_current_bar_height));
    ImGui::TextUnformatted("Transition duration: 60 Runtime units / 2.0 s");
  }

  ImGui::End();
}

void DebugUI::show_visualizers() {
  ImGui::Begin("Visualizers", &m_show_visualizers);

  auto* view{dynamic_cast<Debug::SceneDebugView*>(m_context.scene)};
  if (view == nullptr) {
    ImGui::TextUnformatted("The active scene exposes no visualization capabilities.");
    ImGui::End();
    return;
  }

  ImGui::TextDisabled("Debugger-only presentation aids; runtime simulation data is unchanged.");

  if (view->light_overlay_supported()) {
    bool enabled{view->light_overlay_enabled()};
    if (ImGui::Checkbox("Light overlay", &enabled)) {
      view->set_light_overlay_enabled(enabled);
    }
  } else {
    ImGui::TextDisabled("Light overlay: unavailable for the active scene");
  }

  if (view->sprite_overlay_supported()) {
    bool enabled{view->sprite_overlay_enabled()};
    if (ImGui::Checkbox("Sprite overlay", &enabled)) {
      view->set_sprite_overlay_enabled(enabled);
    }
  } else {
    ImGui::TextDisabled("Sprite overlay: unavailable for the active scene");
  }

  if (view->geometry_wireframe_supported()) {
    bool enabled{view->geometry_wireframe_enabled()};
    if (ImGui::Checkbox("Geometry wireframe", &enabled)) {
      view->set_geometry_wireframe_enabled(enabled);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "Draws a solid-colour, depth-tested wireframe over every triangle mesh, independent "
          "of its texture or material transparency.");
    }
  } else {
    ImGui::TextDisabled("Geometry wireframe: unavailable for the active scene");
  }

  if (view->sprite_grayscale_supported()) {
    bool enabled{view->sprite_grayscale_enabled()};
    if (ImGui::Checkbox("Sprite grayscale", &enabled)) {
      view->set_sprite_grayscale_enabled(enabled);
    }
  } else {
    ImGui::TextDisabled("Sprite grayscale: unavailable for the active scene");
  }

  ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Sprite Inspector
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_sprite_inspector(const float delta_time) {
  ImGui::Begin("Sprite Inspector", &m_show_sprite_inspector);

  show_runtime_target_summary();
  ScenarioRuntime* runtime{m_runtime_context.resolved().runtime};
  if (runtime == nullptr) {
    ImGui::TextUnformatted("Selected target has no loaded sprite runtime.");
    ImGui::End();
    return;
  }
  auto* scene_view{dynamic_cast<SceneDebugView*>(m_context.scene)};

  if (ImGui::BeginTabBar("SpriteTabs")) {
    if (ImGui::BeginTabItem("Resources")) {
      show_sprite_resources_tab(*runtime, scene_view);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Instances")) {
      show_sprite_instances_tab(*runtime, scene_view, delta_time);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Frames")) {
      show_sprite_frames_tab(*runtime);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Render Queue")) {
      const std::optional<SpriteRenderDebugState> state{
          scene_view == nullptr ? std::nullopt : scene_view->sprite_render_debug_state()};
      if (!state.has_value() || state->runtime != runtime) {
        ImGui::TextUnformatted("The selected runtime is not currently presented by this scene.");
      } else {
        show_sprite_queue_tab(state.value());
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

void DebugUI::show_sprite_resources_tab(
    ScenarioRuntime& runtime, SceneDebugView* const scene_view) {
  ImGui::Text("Select an embedded effect resource to inspect or spawn.");
  ImGui::Text("Decoding is lazy: the resource loads on the first spawn.");
  ImGui::Separator();

  for (std::size_t index{0}; index < runtime.sprite_resource_count(); ++index) {
    const Sprite::SpriteResource* resource{runtime.sprite_resource(index)};
    const std::string label{fmt::format("{}: {}{}##resource{}",
        index,
        runtime.sprite_resource_name(index),
        resource == nullptr ? " (not decoded)" : "",
        index)};
    if (ImGui::Selectable(label.c_str(), m_sprite_selected_resource == index)) {
      m_sprite_selected_resource = index;
    }
  }

  ImGui::SeparatorText("Debug Overrides");
  ImGui::TextDisabled("Spawning adds a mutable runtime instance.");
  if (ImGui::Button("Spawn from selected resource")) {
    std::size_t object_index{0};
    if (const Sprite::SpriteResource* resource{runtime.sprite_resource(m_sprite_selected_resource)};
        resource != nullptr) {
      object_index = resource->default_object_index();
    }
    const std::optional<std::array<float, 3>> focus{
        scene_view == nullptr ? std::nullopt : scene_view->sprite_debug_focus_position()};
    const std::array<float, 3> position{focus.value_or(runtime.world_anchor())};
    if (auto handle{runtime.spawn_sprite(m_sprite_selected_resource, object_index, position)};
        handle.has_value()) {
      m_sprite_selected_handle = handle.value();
      if (auto frame{runtime.set_sprite_frame(handle.value(), 0)}; !frame) {
        App::Log::warn(LogCategory::Debug, "Sprite frame selection failed: {}", frame.error());
      }
      App::Log::debug(LogCategory::Debug,
          "Spawned sprite {}:{} from resource '{}'",
          handle->index,
          handle->generation,
          runtime.sprite_resource_name(m_sprite_selected_resource));
    } else {
      App::Log::error(LogCategory::Debug, "Sprite spawn failed: {}", handle.error());
    }
  }
}

void DebugUI::show_sprite_instances_tab(
    ScenarioRuntime& runtime, SceneDebugView* const scene_view, const float delta_time) {
  Sprite::SpritePool& pool{runtime.sprite_pool()};

  ImGui::Text("Pool: %lu live / %lu capacity / %lu attached",
      static_cast<unsigned long>(pool.live_count()),
      static_cast<unsigned long>(pool.capacity()),
      static_cast<unsigned long>(pool.attached_count()));

  if (ImGui::BeginChild("##SpriteInstances", ImVec2(0.0F, 120.0F), ImGuiChildFlags_Borders)) {
    for (auto head{pool.render_list_head()}; head.has_value();
        head = pool.render_list_next(*head)) {
      const Sprite::SpriteInstance* instance{pool.find(*head)};
      if (instance == nullptr) {
        continue;
      }
      const std::string label{fmt::format("{}:{} '{}' frame {} mode {}##inst{}",
          instance->handle.index,
          instance->handle.generation,
          runtime.sprite_resource_name(instance->resource_index),
          instance->frame_index,
          render_mode_name(instance->render_mode),
          instance->handle.index)};
      if (ImGui::Selectable(label.c_str(), m_sprite_selected_handle == *head)) {
        m_sprite_selected_handle = *head;
      }
    }
  }
  ImGui::EndChild();
  ImGui::Separator();

  const Sprite::SpriteInstance* instance{pool.find(m_sprite_selected_handle)};
  if (instance == nullptr) {
    ImGui::TextUnformatted("Select an instance to edit it (spawn one from the Resources tab).");
    return;
  }
  const Sprite::SpriteHandle handle{instance->handle};

  const std::string header{fmt::format("Handle {}:{} — resource '{}', object {}",
      handle.index,
      handle.generation,
      runtime.sprite_resource_name(instance->resource_index),
      instance->object_index)};
  ImGui::TextUnformatted(header.c_str());

  // --- Visibility diagnostics ---
  if (!pool.attached(handle)) {
    ImGui::TextColored(K_WARNING_COLOR, "Detached from the render list.");
  }
  if (instance->frame_index == Sprite::SpriteInstance::k_invalid_frame) {
    ImGui::TextColored(K_WARNING_COLOR, "No valid frame selected (0xFFFF).");
  }
  const std::optional<SpriteRenderDebugState> render_state{
      scene_view == nullptr ? std::nullopt : scene_view->sprite_render_debug_state()};
  if (render_state.has_value() && render_state->runtime == &runtime) {
    for (const SpriteSkipDebugState& skipped : render_state->skipped) {
      if (skipped.handle == handle) {
        ImGui::TextColored(
            K_WARNING_COLOR, "Not drawn: %s", fmt::format("{}", skipped.reason).c_str());
      }
    }
  }

  ImGui::SeparatorText("Debug Overrides");
  ImGui::TextDisabled("The controls below modify the selected runtime or its presentation.");

  // --- Lifecycle ---
  if (!pool.attached(handle) && ImGui::Button("Attach")) {
    if (auto result{runtime.attach_sprite(handle)}; !result) {
      App::Log::error(LogCategory::Debug, "Attach failed: {}", result.error());
    }
  }
  ImGui::SameLine();
  if (pool.attached(handle) && ImGui::Button("Detach")) {
    if (auto result{runtime.detach_sprite(handle)}; !result) {
      App::Log::error(LogCategory::Debug, "Detach failed: {}", result.error());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Destroy")) {
    if (auto result{runtime.destroy_sprite(handle)}; !result) {
      App::Log::error(LogCategory::Debug, "Destroy failed: {}", result.error());
    }
  }

  // --- Frame selection ---
  const Sprite::SpriteResource* resource{runtime.sprite_resource(instance->resource_index)};
  const std::size_t frame_count{
      resource == nullptr ? std::size_t{0} : resource->frame_count(instance->object_index)};
  ImGui::Text("Frame: %u / %zu", instance->frame_index, frame_count);
  const auto set_frame = [&](const std::uint16_t frame) {
    if (auto result{runtime.set_sprite_frame(handle, frame)}; !result) {
      App::Log::error(LogCategory::Debug, "{}", result.error());
    }
  };
  const auto advance_frame = [&](const int step) {
    if (frame_count == 0) {
      return;
    }
    const int current{instance->frame_index == Sprite::SpriteInstance::k_invalid_frame
                          ? 0
                          : static_cast<int>(instance->frame_index)};
    const int wrapped{
        (current + step + static_cast<int>(frame_count)) % static_cast<int>(frame_count)};
    set_frame(static_cast<std::uint16_t>(wrapped));
  };
  if (ImGui::Button("Prev") && frame_count > 0) {
    advance_frame(-1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Next") && frame_count > 0) {
    advance_frame(1);
  }
  ImGui::SameLine();
  if (ImGui::Button("Invalidate") && frame_count > 0) {
    set_frame(static_cast<std::uint16_t>(frame_count));  // Fails → 0xFFFF state.
  }
  ImGui::Checkbox("Play frames", &m_sprite_play_frames);
  ImGui::SameLine();
  ImGui::SliderFloat("rate (fps)", &m_sprite_play_rate, 0.5F, 60.0F);
  if (m_sprite_play_frames && frame_count > 0) {
    m_sprite_play_accumulator += delta_time;
    const float interval{1.0F / m_sprite_play_rate};
    if (m_sprite_play_accumulator >= interval) {
      m_sprite_play_accumulator -= interval;
      advance_frame(1);
    }
  }

  // --- Transforms and appearance ---
  std::array<float, 3> position{instance->position};
  if (ImGui::DragFloat3("Position", position.data(), 0.1F)) {
    runtime.set_sprite_position(handle, position);
  }
  float scale_x{instance->scale_x};
  float scale_y{instance->scale_y};
  if (ImGui::DragFloat("Scale X", &scale_x, 0.05F)) {
    runtime.set_sprite_scale(handle, scale_x, scale_y);
  }
  if (ImGui::DragFloat("Scale Y", &scale_y, 0.05F)) {
    runtime.set_sprite_scale(handle, scale_x, scale_y);
  }
  constexpr float k_rad_to_deg{180.0F / std::numbers::pi_v<float>};
  float rotation_degrees{instance->rotation * k_rad_to_deg};
  if (ImGui::DragFloat("Rotation (degrees)", &rotation_degrees, 1.0F)) {
    runtime.set_sprite_rotation(handle, rotation_degrees / k_rad_to_deg);
  }
  std::array<float, 3> tint{instance->tint};
  if (ImGui::ColorEdit3("Tint", tint.data())) {
    runtime.set_sprite_tint(handle, tint);
  }
  float offset_u{instance->texture_offset_u};
  float offset_v{instance->texture_offset_v};
  if (ImGui::DragFloat("UV offset U", &offset_u, 0.01F)) {
    runtime.set_sprite_texture_offset(handle, offset_u, offset_v);
  }
  if (ImGui::DragFloat("UV offset V", &offset_v, 0.01F)) {
    runtime.set_sprite_texture_offset(handle, offset_u, offset_v);
  }
  float unknown_24{instance->unknown_24};
  if (ImGui::DragFloat("Unknown +0x24 (provisional)", &unknown_24, 0.05F)) {
    runtime.set_sprite_unknown_24(handle, unknown_24);
  }
  int mode{static_cast<int>(instance->render_mode)};
  if (ImGui::Combo("Render mode",
          &mode,
          "Default\0Cutout\0Alpha\0Alpha+Cutout\0Additive\0Additive+Cutout\0Darken\0Darken+"
          "Cutout\0AlternateCutout\0\0")) {
    runtime.set_sprite_render_mode(handle, static_cast<Sprite::SpriteRenderMode>(mode));
  }
  ImGui::Text("Effective: blend %s, depth write %s, cutout %s, fogged %s",
      Sprite::render_state(instance->render_mode).blend_enabled ? "on" : "off",
      Sprite::render_state(instance->render_mode).depth_write ? "on" : "off",
      Sprite::render_state(instance->render_mode).cutout ? "on" : "off",
      Sprite::render_state(instance->render_mode).fogged ? "on" : "off");

  if (ImGui::Button("Reset to Runtime defaults")) {
    runtime.reset_sprite_to_defaults(handle);
  }
  if (scene_view != nullptr && render_state.has_value() && render_state->runtime == &runtime) {
    ImGui::SameLine();
    if (ImGui::Button("Move to camera focus")) {
      if (const auto focus{scene_view->sprite_debug_focus_position()}; focus.has_value()) {
        runtime.set_sprite_position(handle, focus.value());
      }
    }
  }
}

void DebugUI::show_sprite_frames_tab(ScenarioRuntime& runtime) {
  const Sprite::SpriteInstance* instance{runtime.sprite_pool().find(m_sprite_selected_handle)};
  if (instance == nullptr) {
    ImGui::TextUnformatted("Select an instance first (Instances tab).");
    return;
  }
  const Sprite::SpriteResource* resource{runtime.sprite_resource(instance->resource_index)};
  if (resource == nullptr) {
    ImGui::TextUnformatted("The resource is not decoded yet; spawn the instance first.");
    return;
  }

  const auto resolved{resource->resolve_frame(instance->object_index,
      instance->frame_index,
      instance->texture_offset_u,
      instance->texture_offset_v)};
  if (!resolved.has_value()) {
    ImGui::TextColored(K_WARNING_COLOR, "Resolution error: %s", resolved.error().message.c_str());
    return;
  }
  const Sprite::SpriteFrame& frame{*resolved};

  ImGui::Text("Object %zu — frame %u of %zu",
      instance->object_index,
      instance->frame_index,
      resource->frame_count(instance->object_index));
  ImGui::Separator();
  ImGui::Text("Points: (%.3f, %.3f) -> (%.3f, %.3f)",
      static_cast<double>(frame.point0.at(0)),
      static_cast<double>(frame.point0.at(1)),
      static_cast<double>(frame.point1.at(0)),
      static_cast<double>(frame.point1.at(1)));
  ImGui::Text("Dimensions: %.3f x %.3f world units",
      static_cast<double>(frame.width),
      static_cast<double>(frame.height));
  ImGui::Text("Texture index: %d", frame.texture_index);
  ImGui::Text("UV0: %.4f, %.4f  |  UV1: %.4f, %.4f  (byte / 256 + offsets)",
      static_cast<double>(frame.uv0.at(0)),
      static_cast<double>(frame.uv0.at(1)),
      static_cast<double>(frame.uv1.at(0)),
      static_cast<double>(frame.uv1.at(1)));

  const std::vector<Omikron::Rectangle>& rectangles{
      resource->model.polygons.at(instance->object_index).rectangles};
  if (static_cast<std::size_t>(instance->frame_index) < rectangles.size()) {
    const Omikron::Rectangle& rectangle{rectangles.at(instance->frame_index)};
    ImGui::Separator();
    ImGui::Text("Raw descriptor: points %u, %u (slots +0x00/+0x04)",
        rectangle.vertices.at(0),
        rectangle.vertices.at(2));
    ImGui::Text("Raw UV bytes: (%u, %u) (%u, %u)  [bytes 2-3 and 6-7 ignored]",
        rectangle.uv.at(0),
        rectangle.uv.at(1),
        rectangle.uv.at(4),
        rectangle.uv.at(5));
  }

  const Texture2D* texture{runtime.sprite_texture(
      instance->resource_index, static_cast<std::size_t>(frame.texture_index))};
  if (texture != nullptr) {
    ImGui::Separator();
    ImGui::Text("Texture: %d x %d", texture->width(), texture->height());
    // ImGui takes the GL texture id as an opaque void*; flip V for GL's origin.
    // NOLINTNEXTLINE(performance-no-int-to-ptr, cppcoreguidelines-pro-type-reinterpret-cast)
    ImGui::Image(reinterpret_cast<void*>(static_cast<std::intptr_t>(texture->id())),
        ImVec2(128.0F, 128.0F),
        ImVec2(0.0F, 1.0F),
        ImVec2(1.0F, 0.0F));
  }
}

void DebugUI::show_sprite_queue_tab(const SpriteRenderDebugState& state) {
  ImGui::Text("Attached %lu | visible %lu | drawn %lu | culled %lu | invalid %lu",
      static_cast<unsigned long>(state.attached),
      static_cast<unsigned long>(state.visible),
      static_cast<unsigned long>(state.drawn),
      static_cast<unsigned long>(state.culled),
      static_cast<unsigned long>(state.invalid));
  ImGui::Text("Batches %lu | draw calls %lu",
      static_cast<unsigned long>(state.batches),
      static_cast<unsigned long>(state.draw_calls));
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Draw commands", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("%-4s %-4s %-6s %-16s", "Res", "Mat", "Verts", "Mode");
    for (const SpriteDrawCommandDebugState& command : state.commands) {
      ImGui::Text("%-4lu %-4d %-6u %-16s",
          static_cast<unsigned long>(command.resource_index),
          command.material_index,
          command.vertex_count,
          render_mode_name(command.render_mode));
    }
  }

  if (ImGui::CollapsingHeader("Skipped sprites")) {
    for (const SpriteSkipDebugState& skipped : state.skipped) {
      ImGui::Text("%u:%u — %s",
          skipped.handle.index,
          skipped.handle.generation,
          fmt::format("{}", skipped.reason).c_str());
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Interface inspector
// ─────────────────────────────────────────────────────────────────────────────

void DebugUI::show_interface() {
  ImGui::Begin("Interface Inspector", &m_show_interface);

  const ScenarioEngine* engine{m_context.scenario_engine};
  if (engine == nullptr) {
    ImGui::TextUnformatted("Scenario engine not available.");
    ImGui::End();
    return;
  }
  const InterfaceDispatcher& dispatcher{engine->dispatcher()};
  const InterfaceOpenRequest& request{dispatcher.last_request()};
  ImGui::SeparatorText("Request");
  ImGui::Text("Interface_Open: ID %u  operands %d / %d",
      static_cast<unsigned int>(request.interface_id),
      request.operand_b,
      request.operand_c);
  ImGui::TextDisabled("Caller/origin: not recorded by the current dispatcher");
  ImGui::Text("Main menu: %s  preliminary interface 29: %s",
      engine->main_menu_active() ? "active" : "inactive",
      engine->preliminary_29_active() ? "active" : "inactive");

  const Interface::InterfaceManager* manager{m_context.interface_manager};
  if (manager == nullptr) {
    ImGui::TextUnformatted("Interface manager not available.");
    ImGui::End();
    return;
  }
  const Interface::InterfaceInstance* instance{manager->focused_instance()};
  if (instance == nullptr || instance->descriptor == nullptr) {
    ImGui::TextUnformatted("No active interface.");
    ImGui::End();
    return;
  }

  ImGui::SeparatorText("Instance");
  ImGui::Text("Focused instance: %u:%u  descriptor %d \"%s\"",
      instance->handle.interface_id,
      instance->handle.generation,
      instance->descriptor->id,
      fmt::format("{}", instance->descriptor->name).c_str());
  ImGui::Text("bitmap: %s", fmt::format("{}", instance->descriptor->bitmap_name).c_str());
  ImGui::Text("string table: %s (%zu entries)",
      fmt::format("{}", instance->descriptor->string_table_name).c_str(),
      instance->strings.size());
  ImGui::Text("Resident instances: %zu", manager->instance_count());

  const Interface::I2DState* state{instance->current_state};
  const char* state_name{"none"};
  if (state != nullptr) {
    state_name = state == instance->root_state ? "root" : "child";
  }
  ImGui::Text("Active logical state: %s  selected element: %zu",
      state_name,
      state != nullptr ? state->selected_element : std::size_t{0});

  if (state != nullptr) {
    ImGui::SeparatorText("I2D source");
    ImGui::TextDisabled("Authored 640x480 logical descriptors and recovered raw flags.");
    std::size_t selectable_ordinal{0};
    for (const Interface::I2DGroup& group : state->groups) {
      ImGui::Text("group flags 0x%08X", group.runtime_flags);
      for (const Interface::I2DElement& element : group.elements) {
        if (const auto* bitmap{std::get_if<Interface::I2DBitmapElement>(&element.data)}) {
          ImGui::Text("Bitmap  source(%d,%d,%d,%d)  authored destination(%d,%d,%d,%d)",
              bitmap->source.x,
              bitmap->source.y,
              bitmap->source.width,
              bitmap->source.height,
              bitmap->destination.x,
              bitmap->destination.y,
              bitmap->destination.width,
              bitmap->destination.height);
          ImGui::TextDisabled("  flags 0x%08X  blit mode 0x%02X  selectable no",
              bitmap->runtime_flags,
              bitmap->runtime_blit_mode);
        } else if (const auto* text{std::get_if<Interface::I2DTextElement>(&element.data)}) {
          const bool selected{state != nullptr && text->selectable() &&
                              selectable_ordinal == state->selected_element};
          if (text->selectable()) {
            ++selectable_ordinal;
          }
          ImGui::Text("Text[%u] \"%s\"  font '%c'  authored bounds(%d,%d,%d,%d)%s",
              text->string_index,
              fmt::format("{}", instance->strings.at(text->string_index)).c_str(),
              text->font_key,
              text->bounds.x,
              text->bounds.y,
              text->bounds.width,
              text->bounds.height,
              selected ? "  <selected>" : "");
          ImGui::TextDisabled("  flags 0x%08X  selectable %s",
              text->runtime_flags,
              text->selectable() ? "yes" : "no");
        }
      }
    }
  }

  const I2DCounters& counters{Metrics::get().i2d_counters()};
  ImGui::SeparatorText("Runtime state - logical 30 Hz");
  ImGui::Text("Background endpoint tick: %llu  ticks crossed this update: %llu",
      static_cast<unsigned long long>(counters.background_tick),
      static_cast<unsigned long long>(counters.background_ticks));
  ImGui::TextDisabled("Authored interface logic advances on 30 Hz endpoints.");

  ImGui::SeparatorText("Presentation - display frame");
  ImGui::Text("Draw calls %zu | quads %zu | glyphs %zu",
      counters.draw_calls,
      counters.quads,
      counters.glyphs);
  ImGui::Text("Interpolation alpha %.3f (%s)",
      static_cast<double>(counters.background_alpha),
      counters.background_interpolated ? "interpolated" : "stepped");
  ImGui::Text("Background: endpoint passes %zu | bytes uploaded %zu | GPU draws %zu",
      counters.background_warp_passes,
      counters.background_bytes_uploaded,
      counters.background_draw_calls);
  ImGui::Text("OpenNomad presentation phase: %d", static_cast<int>(instance->presentation.phase));
  ImGui::SameLine();
  ImGui::TextDisabled("[%s]", evidence_label(EvidenceConfidence::k_open_nomad_only));

  ImGui::SeparatorText("Debug Overrides");
  ImGui::TextDisabled("This changes presentation interpolation, not the logical 30 Hz state.");
  bool interpolated{manager->background_interpolated()};
  if (ImGui::Checkbox("Interpolate background", &interpolated)) {
    m_context.interface_manager->set_background_interpolated(interpolated);
  }

  ImGui::End();
}

}  // namespace App::Debug
