#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Character {

struct SweptSphereQueryInput {
  App::Runtime::Vec3 start{};
  App::Runtime::Vec3 direction{};
  float max_distance{0.0F};
  float radius{0.0F};
};

struct SweptSphereHit {
  std::size_t object_index{0};
  App::Runtime::Vec3 world_point{};
  App::Runtime::Vec3 world_normal{};
  float travel_distance{0.0F};
};

class SweptSphereQuery {
 public:
  [[nodiscard]] static std::optional<SweptSphereHit> find(const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
      const SweptSphereQueryInput& query,
      std::uint32_t excluded_object_flags);
  [[nodiscard]] static std::optional<SweptSphereHit> find_in_object(
      const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
      std::size_t object_index,
      const SweptSphereQueryInput& query);
};

}  // namespace App::Character
