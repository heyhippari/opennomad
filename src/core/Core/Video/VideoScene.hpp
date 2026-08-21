#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "Core/Mesh.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/Video/VideoPlayer.hpp"

namespace App::Video {

/// Centred 2D video presenter. Presents decoded frames as a contain-fitted
/// textured-quad blit, with no scene/update/render lifecycle.
class VideoScene final {
 public:
  /// Builds the video quad and its 2D blit shader. Cannot fail.
  static std::unique_ptr<VideoScene> create();

  ~VideoScene() = default;

  VideoScene(const VideoScene&) = delete;
  VideoScene(VideoScene&&) = delete;
  VideoScene& operator=(VideoScene other) = delete;
  VideoScene& operator=(VideoScene&& other) = delete;

  /// Uploads and draws one decoded frame centred in the viewport while
  /// preserving the frame's aspect ratio.
  void present_frame(const VideoFrame& frame, int viewport_width, int viewport_height);

  /// Contain-fit scale for a frame inside a viewport, in NDC. One component
  /// remains 1 while the other shrinks to preserve the frame's aspect ratio.
  /// Returns the fullscreen scale for non-positive dimensions.
  [[nodiscard]] static std::array<float, 2> compute_contain_scale(
      int frame_width, int frame_height, int viewport_width, int viewport_height);

 private:
  explicit VideoScene(Shader shader);

  Shader m_shader;
  Mesh m_quad;
  std::unique_ptr<Texture2D> m_texture;
  int m_texture_width{0};
  int m_texture_height{0};
};

}  // namespace App::Video
