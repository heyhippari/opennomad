#pragma once

#include <array>
#include <expected>
#include <memory>
#include <string>

#include "Core/Input/InputManager.hpp"
#include "Core/Mesh.hpp"
#include "Core/Scene.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"

namespace App {

/// Fullscreen startup splash: shows IMAGES/OMIKRON.BMP centred and scaled to
/// fit (contain-fit) until the application swaps in the loaded scene. The
/// application owns the countdown; this scene only draws the image.
class SplashScene final : public Scene {
 public:
  /// Loads IMAGES/OMIKRON.BMP relative to the executable, uploads it as a
  /// texture and builds the fullscreen quad.
  static std::expected<std::unique_ptr<SplashScene>, std::string> create();

  ~SplashScene() override = default;

  SplashScene(const SplashScene&) = delete;
  SplashScene(SplashScene&&) = delete;
  SplashScene& operator=(SplashScene other) = delete;
  SplashScene& operator=(SplashScene&& other) = delete;

  void update(float delta_time, const Input::InputManager& input) override;
  void render() override;
  void resize(int width, int height) override;

  /// Contain-fit bounds of an image inside a viewport, in NDC: the image is
  /// scaled so it stays fully visible and centred while preserving its
  /// aspect ratio. Returns {left, right, bottom, top} in [-1, 1]; the
  /// fullscreen quad for non-positive dimensions.
  [[nodiscard]] static std::array<float, 4> compute_contain_bounds(
      int image_width, int image_height, int viewport_width, int viewport_height);

 private:
  explicit SplashScene(Texture2D texture, Shader shader);

  Texture2D m_texture;
  Shader m_shader;
  Mesh m_quad;
  int m_viewport_width{0};
  int m_viewport_height{0};
};

}  // namespace App
