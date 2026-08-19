#pragma once

#include <cstdint>
#include <memory>

#include "Core/Mesh.hpp"
#include "Core/Shader.hpp"
#include "Core/Texture.hpp"
#include "Core/Video/VideoPlayer.hpp"

namespace App::Video {

/// Fullscreen 2D video presenter. Presents decoded frames as a simple
/// textured-quad blit (ffplay-style), with no scene/update/render lifecycle.
class VideoScene final {
 public:
  /// Builds the fullscreen quad and its 2D blit shader. Cannot fail.
  static std::unique_ptr<VideoScene> create();

  ~VideoScene() = default;

  VideoScene(const VideoScene&) = delete;
  VideoScene(VideoScene&&) = delete;
  VideoScene& operator=(VideoScene other) = delete;
  VideoScene& operator=(VideoScene&& other) = delete;

  /// Uploads and draws one decoded frame as a fullscreen textured quad.
  void present_frame(const VideoFrame& frame);

 private:
  explicit VideoScene(Shader shader);

  Shader m_shader;
  Mesh m_quad;
  std::unique_ptr<Texture2D> m_texture;
  int m_texture_width{0};
  int m_texture_height{0};
};

}  // namespace App::Video
