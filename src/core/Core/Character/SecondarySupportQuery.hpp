#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Character {

struct SecondarySupportHit {
  std::size_t object_index{0};
  App::Runtime::Vec3 world_normal{};
  float distance{0.0F};
};

class SecondarySupportQuery {
 public:
  [[nodiscard]] static std::optional<SecondarySupportHit> find(const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
      const App::Runtime::Vec3& world_origin);
};

}  // namespace App::Character
