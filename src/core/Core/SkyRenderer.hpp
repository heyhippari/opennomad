#pragma once

#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Mesh.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/WorldFog.hpp"
#include "Core/WorldLighting.hpp"

namespace App {

class Camera;

namespace Sky {

/// Runtime sky load (0x0041D1A0) scales every object of the sky hierarchy by
/// 12.5 on all three axes.
inline constexpr float K_RUNTIME_SKY_SCALE{12.5F};
/// Runtime sky load subtracts 2250 inches from the root object's serialized Y.
inline constexpr float K_RUNTIME_SKY_ROOT_Y_OFFSET{2250.0F};
/// Root flags Runtime clears at sky load: alpha blend (bit 12) and additive (bit 13).
inline constexpr std::uint32_t K_RUNTIME_SKY_CLEARED_ROOT_FLAGS{0x00003000U};
/// Root flags Runtime sets at sky load (0x00437220(root, 0xC0, 4)): bit 16
/// doubled fog range and bit 17 no far cull.
inline constexpr std::uint32_t K_RUNTIME_SKY_SET_ROOT_FLAGS{0x00030000U};

/// Game-data path of a sky resource: `MESHES/MISC/<name><extension>`.
[[nodiscard]] std::string resource_path(std::string_view sky_name, std::string_view extension);

/// Builds sky geometry in Runtime space with Runtime's load-time mutations
/// applied: every object scaled by 12.5, the root's serialized Y lowered by
/// 2250, its blend bits cleared and bits 16/17 set. The root's X/Z are zeroed because Runtime
/// replaces them with the camera position every frame (see follow_transform()).
[[nodiscard]] std::expected<std::vector<Omikron::MaterialGroup>, std::string> build_geometry(
    const Omikron::Model3DOData& model);

/// Per-frame Runtime placement (0x00420764): the root follows the camera eye
/// on X/Z while keeping its load-time Y.
[[nodiscard]] Runtime::Transform follow_transform(const Runtime::Vec3& camera_eye);

}  // namespace Sky

/// GL presentation of Runtime's single global sky 3DO.
///
/// Runtime inserts the sky as an ordinary scene object whose root carries
/// flag bits 16/17: bit 17 exempts its polygons from the far-distance cull and
/// bit 16 selects the doubled-fog-range bucket, so its fog depth is halved.
/// OpenNomad draws it first,
/// without depth writes and with depth clamping instead of far-plane clipping,
/// so world geometry inside the clip distance always covers it. As an unlit
/// scene object its baked colours take the context ambient clamp.
class SkyRenderer {
 public:
  /// Loads `MESHES/MISC/<sky_name>.3DO` and its `.3DT` texture bank.
  [[nodiscard]] static std::expected<std::unique_ptr<SkyRenderer>, std::string> create(
      std::string_view sky_name);

  ~SkyRenderer() = default;
  SkyRenderer(const SkyRenderer&) = delete;
  SkyRenderer(SkyRenderer&&) = delete;
  SkyRenderer& operator=(const SkyRenderer&) = delete;
  SkyRenderer& operator=(SkyRenderer&&) = delete;

  /// Draws into the currently bound scene-linear target.
  void render(const Camera& camera,
      const Runtime::Vec3& camera_eye,
      float uv_phase_u,
      float uv_phase_v,
      const Fog::ShaderFog& fog,
      const Lighting::Ambient& ambient) const;

  [[nodiscard]] const std::string& name() const {
    return m_name;
  }

  /// Environment-map texture: Runtime copies the sky model's first texture
  /// page into a separate texture when the sky loads (0x0041D28A ->
  /// 0x004641E0). Null when the sky has no textures.
  [[nodiscard]] const GameColorTexture* environment_texture() const {
    return m_environment_texture ? &*m_environment_texture : nullptr;
  }

 private:
  SkyRenderer() = default;

  std::string m_name;
  std::unique_ptr<Shader> m_shader;
  std::deque<Mesh> m_meshes;
  std::vector<std::int32_t> m_group_material_ids;
  std::vector<std::uint32_t> m_group_flags;
  std::vector<GameColorTexture> m_textures;
  std::optional<GameColorTexture> m_environment_texture;
};

}  // namespace App
