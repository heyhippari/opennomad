#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

inline constexpr std::uint32_t k_sfx_magic{0x56302E35U};

struct SfxRawRecord28 {
  std::array<std::byte, 0x28> bytes{};
};

struct SfxRawRecord2C {
  std::array<std::byte, 0x2C> bytes{};
};

struct SfxRawRecord10 {
  std::array<std::byte, 0x10> bytes{};
};

struct SfxDefinition {
  std::int32_t definition_id{0};
  std::int32_t sound_id{0};
  std::uint32_t sprite_id_raw{0};
  std::uint32_t flags{0};
  Runtime::Vec3 direction{};
  float vertical_acceleration{0.0F};
  float lifetime{0.0F};
  float sound_delay{0.0F};
  float emission_delay{0.0F};
  float raw_2c{0.0F};
  std::uint32_t start_color_rgb{0};
  std::uint32_t end_color_rgb{0};
  float initial_scale{0.0F};
  float cone_angle_degrees{0.0F};
  float angular_velocity_degrees{0.0F};
  std::int16_t spawn_count{0};
  std::string name;
  std::uint8_t sprite_render_mode{0};
  std::uint8_t raw_4f{0};

  [[nodiscard]] constexpr std::uint16_t sprite_id() const {
    return static_cast<std::uint16_t>(sprite_id_raw & 0xFFFFU);
  }
};

struct SfxNode {
  std::int32_t node_id{0};
  std::string label;
  std::int32_t trigger_type{0};
  std::int32_t trigger_id{0};
  std::int32_t track_id{0};
  std::uint32_t serialized_track_ptr{0};
  std::uint32_t serialized_point_ptr{0};
  Runtime::Vec3 serialized_runtime_position{};
  std::int32_t anchor_reference_type{0};
  std::int32_t anchor_reference_id{0};
  std::uint32_t serialized_anchor_ptr{0};
  std::int32_t fixed_definition_id{0};
  float startup_delay{0.0F};
  float serialized_elapsed{0.0F};
  std::int32_t repeat_limit{0};
  std::int32_t serialized_repeat_index{0};
  std::uint32_t flags{0};
};

struct SfxTrackPoint {
  std::int32_t point_id{0};
  std::int32_t definition_id{0};
  Runtime::Vec3 position{};
  float segment_duration{0.0F};
  std::int32_t reference_type{0};
  std::int32_t reference_id{0};
  std::uint32_t serialized_reference_ptr{0};
};

struct SfxTrack {
  std::int32_t track_id{0};
  std::string label;
  std::uint32_t point_count{0};
  float mutable_duration_seed{0.0F};
  std::vector<SfxTrackPoint> points;
};

/// Immutable decoded retail SFX companion data. Raw count DWORDs are retained;
/// each parsed count is the low byte, matching Runtime.
struct SfxData {
  std::uint32_t magic{0};
  std::uint32_t raw_count_a{0};
  std::uint32_t raw_count_b{0};
  std::uint32_t raw_definition_count{0};
  std::uint32_t raw_section_d_count{0};
  std::uint32_t raw_node_count{0};
  std::uint32_t raw_track_count{0};
  std::vector<SfxRawRecord28> records_a;
  std::vector<SfxRawRecord2C> records_b;
  std::vector<SfxDefinition> definitions;
  std::vector<SfxRawRecord10> section_d;
  std::vector<SfxNode> nodes;
  std::vector<SfxTrack> tracks;
};

class SFX {
 public:
  [[nodiscard]] static std::expected<SfxData, std::string> load(std::span<const std::byte> data);
};

}  // namespace App::Omikron
