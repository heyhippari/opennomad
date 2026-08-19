#pragma once

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Core/Interface/I2DModel.hpp"

namespace App::Interface {

/// The recovered 640x480 interface canvas, used as the reference layout space
/// for every logical calculation. It is NOT the physical framebuffer size.
inline constexpr float k_reference_width{640.0F};
inline constexpr float k_reference_height{480.0F};
/// Horizontal centre of the reference canvas.
inline constexpr float k_reference_center_x{320.0F};

/// Modern presentation geometry: maps the recovered 640x480 reference space
/// onto a physical framebuffer of arbitrary size. The recovered coordinates
/// remain authoritative for layout; this value type only expresses how they
/// are presented.
struct I2DPresentationTransform {
  int pixel_width{0};
  int pixel_height{0};

  /// Physical pixels per reference unit (pixel_height / 480). The legacy
  /// interface always fills the physical screen height.
  float pixels_per_reference_unit{1.0F};

  /// Logical (reference-space) horizontal range visible on screen. On
  /// widescreen this extends beyond 0..640, centred on 320.
  float logical_left{0.0F};
  float logical_right{k_reference_width};
  float logical_top{0.0F};
  float logical_bottom{k_reference_height};

  /// Orthographic projection mapping logical reference coordinates to the
  /// full viewport (no 4:3 letterboxing).
  glm::mat4 projection{1.0F};
};

/// Builds the presentation transform for a physical drawable framebuffer.
///
/// The vertical scale always fits the 480-unit reference height to the
/// screen; the visible logical width follows the aspect ratio so extra
/// horizontal space becomes available (no black side bars).
[[nodiscard]] inline I2DPresentationTransform make_presentation_transform(
    const int pixel_width, const int pixel_height) {
  I2DPresentationTransform transform;
  transform.pixel_width = pixel_width;
  transform.pixel_height = pixel_height;

  const float scale{
      pixel_height > 0 ? static_cast<float>(pixel_height) / k_reference_height : 1.0F};
  transform.pixels_per_reference_unit = scale;

  const float visible_logical_width{
      pixel_width > 0 ? static_cast<float>(pixel_width) / scale : k_reference_width};
  transform.logical_left = k_reference_center_x - visible_logical_width / 2.0F;
  transform.logical_right = k_reference_center_x + visible_logical_width / 2.0F;
  transform.logical_top = 0.0F;
  transform.logical_bottom = k_reference_height;

  // Top-left origin: y grows down, so the ortho top/bottom are swapped
  // relative to the recovered canvas convention.
  transform.projection = glm::ortho(
      transform.logical_left, transform.logical_right, k_reference_height, 0.0F, -1.0F, 1.0F);
  return transform;
}

/// Reference-space placement of a top-centred element (the main-menu logo)
/// under the modern presentation rules. The recovered destination rectangle
/// is not mutated; the top margin and narrow-screen width clamp are
/// presentation-only adjustments.
struct I2DTopCenterPlacement {
  float x0{0.0F};
  float y0{0.0F};
  float x1{0.0F};
  float y1{0.0F};
};

/// Computes the reference-space quad for a top-centred element.
///
/// `destination` is the recovered Runtime rectangle (e.g. 0,0,640,150).
/// The result keeps the element horizontally centred on the reference canvas
/// and applies `top_margin_reference` reference units of top margin. When the
/// physical viewport is narrower than 4:3, `clamp_width_to_viewport` shrinks
/// the element (preserving aspect) so it stays fully visible; on ordinary
/// landscape displays this is a no-op.
[[nodiscard]] inline I2DTopCenterPlacement compute_top_center_placement(const I2DRect& destination,
    const float top_margin_reference,
    const bool clamp_width_to_viewport,
    const int pixel_width,
    const int pixel_height,
    const float element_scale = 1.0F) {
  const float reference_width{static_cast<float>(destination.width)};
  const float reference_height{static_cast<float>(destination.height)};
  
  const float presentation_scale{std::max(0.0F, element_scale)};

  float width{reference_width * presentation_scale};
  float height{reference_height * presentation_scale};

  if (clamp_width_to_viewport) {
    const float height_scale{
        pixel_height > 0 ? static_cast<float>(pixel_height) / k_reference_height : 1.0F};
    const float visible_logical_width{pixel_width > 0 && height_scale > 0.0F
                                          ? static_cast<float>(pixel_width) / height_scale
                                          : k_reference_width};

    const float factor{width > 0.0F
                           ? std::clamp(std::min(1.0F, visible_logical_width / width), 0.0F, 1.0F)
                           : 1.0F};
    width *= factor;
    height *= factor;
  }

  const float x0{k_reference_center_x - width / 2.0F};
  const float x1{k_reference_center_x + width / 2.0F};
  const float y0{static_cast<float>(destination.y) + top_margin_reference};
  const float y1{y0 + height};

  return I2DTopCenterPlacement{.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1};
}

}  // namespace App::Interface
