#include "Core/ColorManagement.hpp"

#include <cmath>

namespace App::ColorManagement {

float srgb_to_linear(const float encoded) {
  if (encoded <= 0.04045F) {
    return encoded / 12.92F;
  }
  return std::pow((encoded + 0.055F) / 1.055F, 2.4F);
}

float linear_to_srgb(const float linear) {
  if (linear <= 0.0031308F) {
    return 12.92F * linear;
  }
  return (1.055F * std::pow(linear, 1.0F / 2.4F)) - 0.055F;
}

}  // namespace App::ColorManagement
