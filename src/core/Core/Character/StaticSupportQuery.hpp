#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Character {

struct StaticSupportQueryInput {
  App::Runtime::Vec3 world_probe{};
  float radius{0.0F};
};

struct StaticSupportHit {
  std::size_t object_index{0};
  App::Runtime::Vec3 world_point{};
  App::Runtime::Vec3 world_normal{};
  float clearance{0.0F};
};

class StaticSupportQuery {
 public:
  [[nodiscard]] static std::optional<StaticSupportHit> find(const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
      const StaticSupportQueryInput& query);
};

}  // namespace App::Character