#pragma once

#include <expected>
#include <memory>
#include <string>

#include "Core/Shader.hpp"

namespace App {

/// World programs shared by every attached WorldRenderer of one GL context.
///
/// The base programs are linked once per WorldScene rather than once per area
/// build. Enhanced variants compile on first use, so sessions that never
/// enable them pay nothing.
class WorldShaderSet {
 public:
  [[nodiscard]] static std::expected<std::unique_ptr<WorldShaderSet>, std::string> create();

  ~WorldShaderSet() = default;
  WorldShaderSet(const WorldShaderSet&) = delete;
  WorldShaderSet(WorldShaderSet&&) = delete;
  WorldShaderSet& operator=(const WorldShaderSet&) = delete;
  WorldShaderSet& operator=(WorldShaderSet&&) = delete;

  /// Fogged opaque/cutout program with Runtime's per-vertex lighting.
  [[nodiscard]] const Shader& modern() const {
    return *m_modern;
  }
  /// Blended program: far alpha fade, never fogged.
  [[nodiscard]] const Shader& legacy() const {
    return *m_legacy;
  }
  [[nodiscard]] const Shader& wireframe() const {
    return *m_wireframe;
  }
  /// Opaque/cutout program evaluating lit-actor lights per pixel. nullptr when
  /// it failed to build; the failure is logged once and not retried.
  [[nodiscard]] const Shader* per_pixel_modern();

 private:
  WorldShaderSet() = default;

  std::unique_ptr<Shader> m_modern;
  std::unique_ptr<Shader> m_legacy;
  std::unique_ptr<Shader> m_wireframe;
  std::unique_ptr<Shader> m_per_pixel_modern;
  bool m_per_pixel_modern_failed{false};
};

}  // namespace App
