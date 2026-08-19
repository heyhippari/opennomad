#pragma once

// NOLINTNEXTLINE(misc-include-cleaner) — glm umbrella include, see Camera.cpp.
#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Buffers.hpp"
#include "Core/Audio/AudioTypes.hpp"
#include "Core/Camera.hpp"
#include "Core/CameraController.hpp"
#include "Core/Framebuffer.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Mesh.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/SCX.hpp"
#include "Core/Omikron/Texture3DT.hpp"

namespace App {
class ScenarioManager;
class ScenarioRuntime;
}
#include "Core/Scene.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Shader.hpp"
#include "Core/Sprite/SpritePool.hpp"
#include "Core/Sprite/SpriteRenderer.hpp"
#include "Core/Sprite/SpriteResource.hpp"
#include "Core/Texture.hpp"
#include "Core/TextureCube.hpp"
#include "Core/UniformBuffer.hpp"
#include "Core/VertexArray.hpp"

#include <filesystem>

namespace App {

/// Owns the SDL3_mixer audio subsystem (declared here to avoid a heavy
/// include; the scene holds only a non-owning pointer).
namespace Audio {
class AudioSystem;
}

/// Renders an Omikron 3D model, explorable with a free-flying camera
/// driven by the input system (WASD + Space/LeftShift movement, mouse look).
/// Also hosts the SCX script runtime and serves as its sprite world service.
class ModelScene final : public Scene {
 public:
  /// Loads a standalone model (MESHES/DECORS/Anekbah.3DO with its .3DT
  /// texture sidecar) relative to the executable and builds the
  /// render-ready scene.
  static std::expected<std::unique_ptr<ModelScene>, std::string> create();

  /// Loads the EFFECTS2_SMOKE2.3DO effect model embedded in
  /// SCPTDATA/aventure.SCX relative to the executable and builds the
  /// render-ready scene. The model is decoded through the same Model3DO /
  /// Texture3DT pipeline as standalone files.
  static std::expected<std::unique_ptr<ModelScene>, std::string> create_from_scx();

  /// Creates the scene using pre-loaded scenarios from ScenarioManager.
  /// The manager owns the parsed SCX data and provides access to gameplay and
  /// world scenarios. Scripts are kept inactive until explicitly activated.
  static std::expected<std::unique_ptr<ModelScene>, std::string> create_from_scenario_manager(
      ScenarioManager* manager);

  ~ModelScene() override = default;

  ModelScene(const ModelScene&) = delete;
  ModelScene(ModelScene&&) = delete;
  ModelScene& operator=(ModelScene other) = delete;
  ModelScene& operator=(ModelScene&& other) = delete;

  void update(float delta_time, const Input::InputManager& input) override;
  void render() override;
  void resize(int width, int height) override;

  // --- Runtime-style sprite instances ---------------------------------------

  /// The sprite instance pool of this scene.
  [[nodiscard]] Sprite::SpritePool& sprite_pool();
  [[nodiscard]] const Sprite::SpritePool& sprite_pool() const;
  /// Per-frame sprite queue statistics (valid after the last render()).
  [[nodiscard]] const Sprite::SpriteQueueStats& sprite_queue_stats() const;
  /// Batched draw commands of the last rendered frame (inspector only).
  [[nodiscard]] const std::vector<Sprite::SpriteDrawCommand>& sprite_commands() const;
  /// Number of sprite effect resources indexed from aventure.SCX.
  [[nodiscard]] std::size_t sprite_resource_count() const;
  /// The SCX sprite name of a resource (known even before decoding).
  [[nodiscard]] std::string_view sprite_resource_name(std::size_t resource_index) const;
  /// A registered resource, or nullptr when not decoded yet or out of range.
  [[nodiscard]] const Sprite::SpriteResource* sprite_resource(std::size_t resource_index) const;
  /// A decoded resource's GPU texture, or nullptr when not available.
  [[nodiscard]] const Texture2D* sprite_texture(
      std::size_t resource_index, std::size_t material_index) const;

  /// Decodes the resource on demand, creates an instance and attaches it to
  /// the scene's render list.
  [[nodiscard]] std::expected<Sprite::SpriteHandle, std::string> spawn_sprite(
      std::size_t resource_index, std::size_t object_index, std::array<float, 3> position);
  [[nodiscard]] std::expected<void, std::string> attach_sprite(Sprite::SpriteHandle handle);
  [[nodiscard]] std::expected<void, std::string> detach_sprite(Sprite::SpriteHandle handle);
  [[nodiscard]] std::expected<void, std::string> destroy_sprite(Sprite::SpriteHandle handle);
  [[nodiscard]] std::expected<void, std::string> set_sprite_frame(
      Sprite::SpriteHandle handle, std::uint16_t frame_index);
  void set_sprite_render_mode(Sprite::SpriteHandle handle, Sprite::SpriteRenderMode mode);
  void set_sprite_type(Sprite::SpriteHandle handle, std::uint16_t type);
  void set_sprite_position(Sprite::SpriteHandle handle, std::array<float, 3> position);
  void set_sprite_scale(Sprite::SpriteHandle handle, float scale_x, float scale_y);
  void set_sprite_scale_x(Sprite::SpriteHandle handle, float scale_x);
  void set_sprite_scale_y(Sprite::SpriteHandle handle, float scale_y);
  void set_sprite_rotation(Sprite::SpriteHandle handle, float rotation);
  void set_sprite_tint(Sprite::SpriteHandle handle, std::array<float, 3> tint);
  void set_sprite_texture_offset(Sprite::SpriteHandle handle, float offset_u, float offset_v);
  void set_sprite_unknown_24(Sprite::SpriteHandle handle, float value);
  void reset_sprite_to_defaults(Sprite::SpriteHandle handle);
  /// A point a few units in front of the camera, along its view direction.
  [[nodiscard]] std::array<float, 3> camera_focus_position() const;
  /// Moves the sprite to a point a few units in front of the camera.
  void place_sprite_at_camera_focus(Sprite::SpriteHandle handle);
  /// Renderer-wide grayscale toggle for the 3D scene (never the debug UI).
  void set_sprite_grayscale(bool enabled);
  [[nodiscard]] bool sprite_grayscale() const;

  // --- Script runtime ------------------------------------------------------

  /// The SCX script runtime of this scene (null before initialization).
  [[nodiscard]] Script::ScriptRuntime* script_runtime();
  [[nodiscard]] const Script::ScriptRuntime* script_runtime() const;
  [[nodiscard]] std::string_view script_scenario_name() const;
  /// Creates a runtime instance for one parsed source script.
  [[nodiscard]] std::expected<std::size_t, std::string> spawn_script_instance(
      std::size_t source_script_index);

  // --- Audio subsystem ------------------------------------------------------

  /// Injects the non-owning audio subsystem (Application owns it).
  void set_audio_system(Audio::AudioSystem* audio);
  [[nodiscard]] Audio::AudioSystem* audio_system();
  [[nodiscard]] const Audio::AudioSystem* audio_system() const;

  // --- Debug overlays -------------------------------------------------------

  /// Light debug overlay visibility (markers, spot lines, attenuation
  /// spheres). Toggled from the Overlays debug window.
  [[nodiscard]] bool light_overlay_enabled() const;
  void set_light_overlay_enabled(bool enabled);
  /// Sprite overlay visibility: one outline per drawn billboard,
  /// colour-coded by render mode. Toggled from the Overlays debug window.
  [[nodiscard]] bool sprite_overlay_enabled() const;
  void set_sprite_overlay_enabled(bool enabled);

 private:
  /// A k_mirror mesh and its world-space reflection plane.
  struct MirrorSurface {
    std::array<float, 4> plane{0.0F, 0.0F, 0.0F, 0.0F};
  };

  /// A fully decoded standalone model: core, textures and static geometry.
  struct DecodedModel {
    Omikron::Model3DOData model;
    std::vector<Omikron::Texture3DTImage> images;
    std::vector<Omikron::MaterialGroup> groups;
    std::string display_name;
  };

  /// Loads and decodes a standalone .3DO model plus its .3DT sidecar.
  static std::expected<DecodedModel, std::string> load_decoded_model(
      const std::filesystem::path& model_path);

  /// Compiles the sprite shader and creates the sprite renderer's GL
  /// resources. Requires a current GL context; called by the SCX and
  /// scenario-manager factory paths (never by the standalone model path).
  std::expected<void, std::string> initialize_sprite_renderer();

  /// CPU-side vertex of the light debug overlay (world position + RGBA).
  struct OverlayVertex {
    std::array<float, 3> position{0.0F, 0.0F, 0.0F};
    std::array<float, 4> color{0.0F, 0.0F, 0.0F, 1.0F};
  };

  ModelScene(const std::vector<Omikron::MaterialGroup>& groups,
             std::vector<Texture2D> textures,
             Shader shader,
             Shader mirror_shader,
             Shader env_shader,
             Shader skybox_shader,
             Shader overlay_shader,
             Framebuffer mirror_framebuffer,
             TextureCube sky_cubemap,
             UniformBuffer light_buffer,
             std::vector<OverlayVertex> overlay_vertices,
             std::size_t overlay_marker_count,
             std::size_t overlay_line_count,
             std::size_t overlay_sphere_count,
             std::array<float, 3> model_center);

  /// Builds the render-ready scene from decoded geometry, material
  /// descriptors and textures: GPU uploads, shaders, light block and the
  /// scene object itself. Shared by the standalone and SCX load paths.
  static std::expected<std::unique_ptr<ModelScene>, std::string> create_from_geometry(
      const std::vector<Omikron::MaterialGroup>& groups,
      const Omikron::Model3DOData& model,
      const std::vector<Omikron::Texture3DTImage>& images,
      std::string_view display_name);

  /// Draws one material group with the main shader.
  void draw_group(std::size_t index);
  /// Draws a k_mirror group compositing the reflection buffer.
  void draw_mirror_group(std::size_t index,
                         const glm::mat4& view,
                         const glm::mat4& projection,
                         const glm::mat4& model);
  /// Draws a k_environment_mapped group with the cube map.
  void draw_env_group(std::size_t index,
                      const glm::vec3& eye,
                      const glm::vec4& clip_plane,
                      const glm::mat4& view,
                      const glm::mat4& projection,
                      const glm::mat4& model);
  /// Draws a k_skybox group with the unlit skybox shader.
  void draw_skybox_group(std::size_t index);
  /// Draws all k_skybox groups: camera-following view, far-plane clamped,
  /// no depth writes, so the flagged geometry wraps the scene.
  void render_skybox(const glm::mat4& view, const glm::mat4& projection);
  /// Runs the opaque and blended passes for one camera pose.
  void render_scene(const glm::mat4& view,
                    const glm::mat4& projection,
                    const glm::mat4& model,
                    const glm::vec3& eye,
                    const glm::vec4& clip_plane,
                    bool draw_mirrors);
  /// Renders the scene through a mirror plane into the reflection buffer.
  void render_reflection(const MirrorSurface& mirror,
                         const glm::mat4& view,
                         const glm::mat4& projection,
                         const glm::mat4& model,
                         const glm::vec3& eye);
  /// Draws the light debug overlay (markers, spot lines, attenuation
  /// spheres) on top of the scene; respects depth, writes no depth.
  void render_light_overlay(const glm::mat4& view, const glm::mat4& projection);
  /// Draws the sprite overlay: one outlined quad per billboard drawn in the
  /// last frame, colour-coded by render mode. Depth-tested, no depth writes.
  void render_sprite_overlay(const glm::mat4& view, const glm::mat4& projection);

  static constexpr std::array<float, 3> k_light_direction{0.35F, 0.75F, 0.55F};
  static constexpr float k_ambient_strength{0.35F};
  /// Multiplier for light colour x intensity (visual tuning knob).
  static constexpr float k_light_intensity_scale{2.0F};
  /// Cosines of the spot half-angles: 20° hotspot, 60° falloff (40°/120°
  /// full cones, matching the reference importer).
  static constexpr float k_spot_hotspot_cos{0.93969262F};
  static constexpr float k_spot_falloff_cos{0.5F};
  /// Clip plane that keeps every fragment (the mirror pass overrides it).
  static constexpr std::array<float, 4> k_no_clip_plane{0.0F, 0.0F, 0.0F, 1.0F};
  /// Resolution of the offscreen reflection buffer.
  static constexpr int k_mirror_resolution{1024};
  /// Procedural sky cube-map resolution (per face).
  static constexpr int k_sky_cubemap_size{64};
  /// Backdrop of the reflection pass; matches the renderer clear colour.
  static constexpr std::array<float, 4> k_mirror_clear_color{0.5F, 0.5F, 0.5F, 1.0F};
  static constexpr float k_camera_distance{3.0F};
  static constexpr float k_camera_height{0.5F};

  Shader m_shader;
  Shader m_mirror_shader;
  Shader m_env_shader;
  Shader m_skybox_shader;
  Shader m_overlay_shader;
  Framebuffer m_mirror_framebuffer;
  TextureCube m_sky_cubemap;
  /// std140 light block, bound to binding point 0 for all scene shaders.
  UniformBuffer m_light_buffer;
  /// Combined light-overlay geometry: markers, spot lines, sphere wireframes.
  VertexBuffer m_overlay_buffer;
  VertexArray m_overlay_array;
  std::size_t m_overlay_marker_count{0};
  std::size_t m_overlay_line_count{0};
  std::size_t m_overlay_sphere_count{0};
  /// Dynamic sprite-overlay geometry: per-frame billboard outlines.
  VertexBuffer m_sprite_overlay_buffer;
  VertexArray m_sprite_overlay_array;
  /// Scene lights on (true) or flat ambient tint only (false). Toggled by L.
  bool m_lights_enabled{true};
  /// Light debug overlay visible. Toggled from the Overlays debug window.
  bool m_light_overlay_enabled{false};
  /// Sprite billboard outline overlay visible (Overlays debug window).
  bool m_sprite_overlay_enabled{false};
  // One mesh per material group (Mesh is neither movable nor copyable, so a
  // deque keeps the elements stable).
  std::deque<Mesh> m_meshes;
  /// Material id of each mesh, parallel to m_meshes.
  std::vector<std::int32_t> m_group_material_ids;
  /// Mesh flags of each mesh, parallel to m_meshes.
  std::vector<std::uint32_t> m_group_flags;
  /// Blend mode of each mesh, parallel to m_meshes.
  std::vector<Omikron::BlendMode> m_group_modes;
  /// World-space bounding-box centre of each mesh, parallel to m_meshes.
  std::vector<std::array<float, 3>> m_group_centers;
  /// Uploaded textures, aligned with the model's material table.
  std::vector<Texture2D> m_textures;
  Camera m_camera{60.0F, 1.0F, 0.1F, 1000.0F};
  /// Drives m_camera from the input system's action values.
  CameraController m_camera_controller{m_camera};
  /// One entry per k_mirror mesh, discovered at load time.
  std::vector<MirrorSurface> m_mirrors;
  /// Indices of k_skybox meshes into m_meshes (and its parallel vectors),
  /// discovered at load time.
  std::vector<std::size_t> m_skybox_group_indices;
  int m_viewport_width{1};
  int m_viewport_height{1};

  /// Bounding-box centre of the model in world space; the camera frames it
  /// at startup.
  std::array<float, 3> m_model_center{};

  float m_aspect_ratio{-1.0F};

  // --- Sprite system ---
  Sprite::SpriteRenderer m_sprite_renderer;
  /// Renderer-wide grayscale toggle (3D scene only).
  bool m_sprite_grayscale_enabled{false};

  // --- Shared sprite/script/audio runtime ---
  /// Non-owning pointer to the scene-independent gameplay runtime. Owned by
  /// ScenarioManager for the scenario-manager path, or by m_owned_runtime
  /// for the standalone SCX path. Null for the standalone model path.
  ScenarioRuntime* m_runtime{nullptr};
  /// Owning handle for the standalone SCX path (create_from_scx).
  std::unique_ptr<ScenarioRuntime> m_owned_runtime;
};

}  // namespace App
