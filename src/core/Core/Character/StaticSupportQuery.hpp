#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Character {

enum class SupportClass : std::uint8_t {
  k_static_polygon = 1,
  k_transformed_general = 2,
};

struct StaticSupportQueryInput {
  App::Runtime::Vec3 world_probe{};
  float radius{0.0F};
};

struct StaticSupportHit {
  std::size_t object_index{0};
  App::Runtime::Vec3 world_point{};
  App::Runtime::Vec3 world_normal{};
  float clearance{0.0F};
  SupportClass support_class{SupportClass::k_static_polygon};
};

struct SupportQueryResult {
  StaticSupportHit primary{};
  std::optional<StaticSupportHit> alternate;
};

class StaticSupportQuery {
 public:
  [[nodiscard]] static std::optional<SupportQueryResult> find_candidates(
      const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
      const StaticSupportQueryInput& query);
  [[nodiscard]] static std::optional<StaticSupportHit> find(const Omikron::Model3DOData& model,
      std::span<const Omikron::Model3DOData::RuntimeObjectState> runtime_objects,
      const StaticSupportQueryInput& query);
};

}  // namespace App::Character