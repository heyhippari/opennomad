#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

/// One 0x18-byte trajectory: a start point followed by `segment_count`
/// consecutive segment vectors. +0x0C is a Runtime-only occupancy list head.
struct TrajectoryRecord {
  Runtime::Vec3 start{};
  std::int16_t first_segment{0};  ///< +0x10, index into `segments`.
  std::int16_t first_route{0};    ///< +0x12, index into `routes`.
  std::int8_t route_count{0};     ///< +0x14; Runtime treats 0 as 1.
  std::uint8_t segment_count{0};  ///< +0x15.
};

/// One 0x14-byte segment. +0x00 is a Runtime-only occupancy list head.
struct TrajectorySegment {
  Runtime::Vec3 vector{};
  /// +0x10, index into `stop_points` or -1.
  std::int16_t stop_point{-1};
};

/// One 0x14-byte idle-activity point reached from a segment start.
struct TrajectoryStopPoint {
  /// +0x00, offset from the start of the segment that owns this point.
  Runtime::Vec3 offset{};
  /// +0x0C, facing while performing the activity, in degrees.
  float yaw_degrees{0.0F};
  /// +0x10, ANI animation ID of the looping activity animation. Zero
  /// disables the point; Runtime also clears it while the point is in use.
  std::int16_t animation_id{0};
  /// +0x12, number of extra activity loops.
  std::int8_t loop_count{0};
};

/// One 0x0C-byte route that connects the end of a trajectory to the start of
/// another through `step_count` junction steps. +0x00 is a Runtime-only
/// occupancy list head.
struct TrajectoryRoute {
  std::int16_t next_trajectory{0};  ///< +0x04.
  std::int16_t first_step{0};       ///< +0x06, index into `route_steps`.
  std::int16_t entry_crossing{-1};  ///< +0x08, index into `crossings` or -1.
  std::int8_t step_count{0};        ///< +0x0A.
};

/// One 0x10-byte junction step: a displacement plus the crossing that must be
/// acquired before the following step starts.
struct TrajectoryRouteStep {
  Runtime::Vec3 vector{};
  std::int16_t crossing{-1};  ///< +0x0C, index into `crossings` or -1.
};

/// One 4-byte crossing. Acquiring it locks every listed conflicting crossing.
/// Byte +0x03 is Runtime's live lock counter and is zero on disk.
struct TrajectoryCrossing {
  std::int16_t first_conflict{0};  ///< +0x00, index into `crossing_conflicts`.
  std::int8_t conflict_count{0};   ///< +0x02.
};

/// Decoded TRAJECTOIRES/<AREA +0x73>.OPT street network ("V1.0").
///
/// Runtime 0x00453450 relocates the header offsets at +0x18/+0x20/+0x28/
/// +0x30/+0x38/+0x40/+0x48. Each table's count is the dword immediately
/// before the following table's offset.
struct TrajectoryOpt {
  /// [pedestrian_begin, pedestrian_end) are passerby trajectories,
  /// [pedestrian_end, trajectory_end) are slider (flying vehicle) lanes.
  std::int32_t pedestrian_begin{0};  ///< +0x04.
  std::int32_t pedestrian_end{0};    ///< +0x08.
  /// +0x0C, passerby spawn spacing in metres at Crowd Density 4.
  std::int32_t pedestrian_spacing{0};
  std::int32_t slider_spacing{0};  ///< +0x10.
  std::int32_t trajectory_end{0};  ///< +0x14, trajectory count.

  std::vector<TrajectoryRecord> trajectories;
  std::vector<TrajectorySegment> segments;
  std::vector<TrajectoryStopPoint> stop_points;
  std::vector<TrajectoryRoute> routes;
  std::vector<TrajectoryRouteStep> route_steps;
  std::vector<TrajectoryCrossing> crossings;
  std::vector<std::int16_t> crossing_conflicts;

  /// Parses and bounds-checks every table and cross-table index.
  [[nodiscard]] static std::expected<TrajectoryOpt, std::string> load(
      std::span<const std::byte> data);
};

}  // namespace App::Omikron
