#include "Core/Omikron/TrajectoryOpt.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Omikron/BinaryReader.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Omikron {

namespace {

constexpr std::array<std::byte, 4> K_MAGIC{
    std::byte{'V'}, std::byte{'1'}, std::byte{'.'}, std::byte{'0'}};
constexpr std::size_t K_HEADER_SIZE{0x4C};
constexpr std::uint32_t K_MAX_RECORD_COUNT{1U << 20U};

constexpr std::size_t K_TRAJECTORY_STRIDE{0x18};
constexpr std::size_t K_SEGMENT_STRIDE{0x14};
constexpr std::size_t K_STOP_POINT_STRIDE{0x14};
constexpr std::size_t K_ROUTE_STRIDE{0x0C};
constexpr std::size_t K_ROUTE_STEP_STRIDE{0x10};
constexpr std::size_t K_CROSSING_STRIDE{0x04};
constexpr std::size_t K_CONFLICT_STRIDE{0x02};

struct TableSpan {
  std::uint32_t offset{0};
  std::uint32_t count{0};
};

std::int16_t read_i16(BinaryReader& reader) {
  return static_cast<std::int16_t>(reader.read_u16());
}

std::int8_t read_i8(BinaryReader& reader) {
  return static_cast<std::int8_t>(reader.read_u8());
}

Runtime::Vec3 read_vec3(BinaryReader& reader) {
  return Runtime::Vec3{.x = reader.read_f32(), .y = reader.read_f32(), .z = reader.read_f32()};
}

std::expected<BinaryReader, std::string> table_reader(const std::span<const std::byte> data,
    const TableSpan table,
    const std::size_t stride,
    const std::string_view name) {
  if (table.count > K_MAX_RECORD_COUNT) {
    return std::expected<BinaryReader, std::string>{
        std::unexpect, fmt::format("OPT {} count {} is implausible", name, table.count)};
  }
  const std::size_t size{static_cast<std::size_t>(table.count) * stride};
  if (table.offset < K_HEADER_SIZE || table.offset > data.size() ||
      size > data.size() - table.offset) {
    return std::expected<BinaryReader, std::string>{std::unexpect,
        fmt::format("OPT {} table [{:#x}, +{:#x}) exceeds {} bytes",
            name,
            table.offset,
            size,
            data.size())};
  }
  return BinaryReader{data.subspan(table.offset, size)};
}

template <typename Index>
bool index_in_range(const Index index, const std::size_t count) {
  return index >= 0 && std::cmp_less(index, count);
}

std::expected<void, std::string> validate(const TrajectoryOpt& opt) {
  const auto fail = [](std::string message) {
    return std::expected<void, std::string>{std::unexpect, std::move(message)};
  };
  if (opt.pedestrian_begin < 0 || opt.pedestrian_begin > opt.pedestrian_end ||
      opt.pedestrian_end > opt.trajectory_end) {
    return fail(fmt::format("OPT trajectory ranges [{}, {}, {}) are inconsistent",
        opt.pedestrian_begin,
        opt.pedestrian_end,
        opt.trajectory_end));
  }
  for (std::size_t index{0}; index < opt.trajectories.size(); ++index) {
    const TrajectoryRecord& trajectory{opt.trajectories.at(index)};
    if (trajectory.segment_count == 0U || trajectory.first_segment < 0 ||
        static_cast<std::size_t>(trajectory.first_segment) + trajectory.segment_count >
            opt.segments.size()) {
      return fail(fmt::format("OPT trajectory {} segment range is invalid", index));
    }
    const std::size_t route_count{
        static_cast<std::size_t>(std::max<std::int8_t>(trajectory.route_count, 1))};
    if (trajectory.first_route < 0 ||
        static_cast<std::size_t>(trajectory.first_route) + route_count > opt.routes.size()) {
      return fail(fmt::format("OPT trajectory {} route range is invalid", index));
    }
  }
  for (std::size_t index{0}; index < opt.segments.size(); ++index) {
    const std::int16_t stop_point{opt.segments.at(index).stop_point};
    if (stop_point != -1 && !index_in_range(stop_point, opt.stop_points.size())) {
      return fail(fmt::format("OPT segment {} stop point {} is out of range", index, stop_point));
    }
  }
  for (std::size_t index{0}; index < opt.routes.size(); ++index) {
    const TrajectoryRoute& route{opt.routes.at(index)};
    if (!index_in_range(route.next_trajectory, opt.trajectories.size()) || route.step_count < 0 ||
        route.first_step < 0 ||
        static_cast<std::size_t>(route.first_step) + static_cast<std::size_t>(route.step_count) >
            opt.route_steps.size() ||
        (route.entry_crossing != -1 &&
            !index_in_range(route.entry_crossing, opt.crossings.size()))) {
      return fail(fmt::format("OPT route {} is invalid", index));
    }
  }
  for (std::size_t index{0}; index < opt.route_steps.size(); ++index) {
    const std::int16_t crossing{opt.route_steps.at(index).crossing};
    if (crossing != -1 && !index_in_range(crossing, opt.crossings.size())) {
      return fail(fmt::format("OPT route step {} crossing {} is out of range", index, crossing));
    }
  }
  for (std::size_t index{0}; index < opt.crossings.size(); ++index) {
    const TrajectoryCrossing& crossing{opt.crossings.at(index)};
    if (crossing.conflict_count < 0 || crossing.first_conflict < 0 ||
        static_cast<std::size_t>(crossing.first_conflict) +
                static_cast<std::size_t>(crossing.conflict_count) >
            opt.crossing_conflicts.size()) {
      return fail(fmt::format("OPT crossing {} conflict range is invalid", index));
    }
  }
  for (std::size_t index{0}; index < opt.crossing_conflicts.size(); ++index) {
    if (!index_in_range(opt.crossing_conflicts.at(index), opt.crossings.size())) {
      return fail(fmt::format("OPT crossing conflict {} is out of range", index));
    }
  }
  return {};
}

}  // namespace

std::expected<TrajectoryOpt, std::string> TrajectoryOpt::load(
    const std::span<const std::byte> data) {
  using Result = std::expected<TrajectoryOpt, std::string>;
  if (data.size() < K_HEADER_SIZE || !std::ranges::equal(data.first(K_MAGIC.size()), K_MAGIC)) {
    return Result{std::unexpect, "OPT header is missing its V1.0 signature"};
  }

  BinaryReader header{data.subspan(K_MAGIC.size(), K_HEADER_SIZE - K_MAGIC.size())};
  TrajectoryOpt opt;
  opt.pedestrian_begin = header.read_i32();
  opt.pedestrian_end = header.read_i32();
  opt.pedestrian_spacing = header.read_i32();
  opt.slider_spacing = header.read_i32();
  opt.trajectory_end = header.read_i32();
  // Offsets at +0x18, +0x20, ... +0x48; counts at +0x14 (trajectories) and
  // the dword after each offset for the following table.
  std::array<std::uint32_t, 13> words{};
  for (std::uint32_t& word : words) {
    word = header.read_u32();
  }
  if (header.has_error() || opt.trajectory_end < 0) {
    return Result{std::unexpect, "OPT header is truncated or invalid"};
  }
  const TableSpan trajectory_table{
      .offset = words.at(0), .count = static_cast<std::uint32_t>(opt.trajectory_end)};
  const TableSpan segment_table{.offset = words.at(2), .count = words.at(1)};
  const TableSpan stop_point_table{.offset = words.at(4), .count = words.at(3)};
  const TableSpan route_table{.offset = words.at(6), .count = words.at(5)};
  const TableSpan route_step_table{.offset = words.at(8), .count = words.at(7)};
  const TableSpan crossing_table{.offset = words.at(10), .count = words.at(9)};
  const TableSpan conflict_table{.offset = words.at(12), .count = words.at(11)};

  auto trajectories{table_reader(data, trajectory_table, K_TRAJECTORY_STRIDE, "trajectory")};
  auto segments{table_reader(data, segment_table, K_SEGMENT_STRIDE, "segment")};
  auto stop_points{table_reader(data, stop_point_table, K_STOP_POINT_STRIDE, "stop point")};
  auto routes{table_reader(data, route_table, K_ROUTE_STRIDE, "route")};
  auto route_steps{table_reader(data, route_step_table, K_ROUTE_STEP_STRIDE, "route step")};
  auto crossings{table_reader(data, crossing_table, K_CROSSING_STRIDE, "crossing")};
  auto conflicts{table_reader(data, conflict_table, K_CONFLICT_STRIDE, "crossing conflict")};
  for (const auto* reader :
      {&trajectories, &segments, &stop_points, &routes, &route_steps, &crossings, &conflicts}) {
    if (!reader->has_value()) {
      return Result{std::unexpect, reader->error()};
    }
  }

  opt.trajectories.reserve(trajectory_table.count);
  for (std::uint32_t index{0}; index < trajectory_table.count; ++index) {
    BinaryReader& reader{trajectories.value()};
    TrajectoryRecord record;
    record.start = read_vec3(reader);
    static_cast<void>(reader.read_u32());
    record.first_segment = read_i16(reader);
    record.first_route = read_i16(reader);
    record.route_count = read_i8(reader);
    record.segment_count = reader.read_u8();
    static_cast<void>(reader.read_u16());
    opt.trajectories.push_back(record);
  }
  opt.segments.reserve(segment_table.count);
  for (std::uint32_t index{0}; index < segment_table.count; ++index) {
    BinaryReader& reader{segments.value()};
    TrajectorySegment segment;
    static_cast<void>(reader.read_u32());
    segment.vector = read_vec3(reader);
    segment.stop_point = read_i16(reader);
    static_cast<void>(reader.read_u16());
    opt.segments.push_back(segment);
  }
  opt.stop_points.reserve(stop_point_table.count);
  for (std::uint32_t index{0}; index < stop_point_table.count; ++index) {
    BinaryReader& reader{stop_points.value()};
    TrajectoryStopPoint point;
    point.offset = read_vec3(reader);
    point.yaw_degrees = reader.read_f32();
    point.animation_id = read_i16(reader);
    point.loop_count = read_i8(reader);
    static_cast<void>(reader.read_u8());
    opt.stop_points.push_back(point);
  }
  opt.routes.reserve(route_table.count);
  for (std::uint32_t index{0}; index < route_table.count; ++index) {
    BinaryReader& reader{routes.value()};
    TrajectoryRoute route;
    static_cast<void>(reader.read_u32());
    route.next_trajectory = read_i16(reader);
    route.first_step = read_i16(reader);
    route.entry_crossing = read_i16(reader);
    route.step_count = read_i8(reader);
    static_cast<void>(reader.read_u8());
    opt.routes.push_back(route);
  }
  opt.route_steps.reserve(route_step_table.count);
  for (std::uint32_t index{0}; index < route_step_table.count; ++index) {
    BinaryReader& reader{route_steps.value()};
    TrajectoryRouteStep step;
    step.vector = read_vec3(reader);
    step.crossing = read_i16(reader);
    static_cast<void>(reader.read_u16());
    opt.route_steps.push_back(step);
  }
  opt.crossings.reserve(crossing_table.count);
  for (std::uint32_t index{0}; index < crossing_table.count; ++index) {
    BinaryReader& reader{crossings.value()};
    TrajectoryCrossing crossing;
    crossing.first_conflict = read_i16(reader);
    crossing.conflict_count = read_i8(reader);
    static_cast<void>(reader.read_u8());
    opt.crossings.push_back(crossing);
  }
  opt.crossing_conflicts.reserve(conflict_table.count);
  for (std::uint32_t index{0}; index < conflict_table.count; ++index) {
    opt.crossing_conflicts.push_back(read_i16(conflicts.value()));
  }

  for (const auto* reader :
      {&trajectories, &segments, &stop_points, &routes, &route_steps, &crossings, &conflicts}) {
    if (reader->value().has_error()) {
      return Result{
          std::unexpect, fmt::format("OPT table read failed: {}", reader->value().error())};
    }
  }
  if (auto valid{validate(opt)}; !valid) {
    return Result{std::unexpect, std::move(valid).error()};
  }
  return opt;
}

}  // namespace App::Omikron
