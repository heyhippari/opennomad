#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace App::Debug {

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

  bool camera_has_pose{false};
  bool camera_scripted{false};
  bool camera_transitioning{false};
  std::optional<std::uint16_t> camera_id;
  std::array<float, 3> camera_eye{};
  std::array<float, 3> camera_target{};
};

/// Optional debug capability implemented by scenes with 3D presentation
/// state. DebugUI depends on this interface rather than concrete scene types.
class SceneDebugView {
 public:
  virtual ~SceneDebugView() = default;

  [[nodiscard]] virtual std::optional<WorldRenderDebugState>
  world_render_debug_state() const {
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