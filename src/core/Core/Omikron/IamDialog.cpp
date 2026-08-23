#include "Core/Omikron/IamDialog.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Omikron/IamArchive.hpp"
#include "Core/Omikron/IamCamera.hpp"

namespace App::Omikron {

namespace {

template <typename Value>
Value read_at(const std::span<const std::byte> data, const std::size_t offset) {
  Value value{};
  std::memcpy(&value, data.subspan(offset, sizeof(Value)).data(), sizeof(Value));
  return value;
}

std::string fixed_string(const std::span<const std::byte> bytes) {
  const void* raw{bytes.data()};
  const auto* begin{static_cast<const char*>(raw)};
  const void* nul{std::memchr(raw, '\0', bytes.size())};
  const std::size_t length{nul == nullptr
                               ? bytes.size()
                               : static_cast<std::size_t>(static_cast<const char*>(nul) - begin)};
  return std::string{begin, length};
}

std::expected<std::array<std::string_view, IamDialogRecord::k_string_count>, std::string>
read_node_strings(const std::span<const std::byte> data,
    const std::uint32_t offset,
    const std::size_t node_index) {
  if (offset == 0U || offset >= data.size()) {
    return std::expected<std::array<std::string_view, IamDialogRecord::k_string_count>,
        std::string>{std::unexpect,
        fmt::format("IAM/DIALOG node {}: strings offset {:#x} is outside the {:#x}-byte record",
            node_index,
            offset,
            data.size())};
  }

  std::array<std::string_view, IamDialogRecord::k_string_count> strings;
  std::size_t cursor{offset};
  for (std::size_t index{0}; index < strings.size(); ++index) {
    const auto begin{data.begin() + static_cast<std::ptrdiff_t>(cursor)};
    const auto end{std::find(begin, data.end(), std::byte{})};
    if (end == data.end()) {
      return std::expected<decltype(strings), std::string>{std::unexpect,
          fmt::format("IAM/DIALOG node {}: string {} at {:#x} is not NUL-terminated",
              node_index,
              index,
              cursor)};
    }
    const std::size_t length{static_cast<std::size_t>(end - begin)};
    const void* raw{data.subspan(cursor, length).data()};
    strings.at(index) = std::string_view{static_cast<const char*>(raw), length};
    cursor += length + 1U;
  }
  return strings;
}

bool valid_relative_offset(const std::uint32_t offset, const std::size_t size) {
  return offset == 0U || offset < size;
}

}  // namespace

std::expected<IamDialogRecord, std::string> IamDialogRecord::load_from_archive(
    const std::span<const std::byte> archive, const std::uint16_t dialog_id) {
  const IamIndexedArchive indexed{archive};
  auto record{indexed.read_record(dialog_id)};
  if (!record) {
    return std::expected<IamDialogRecord, std::string>{
        std::unexpect, fmt::format("IAM/DIALOG {}: {}", dialog_id, record.error())};
  }
  return load(record.value());
}

std::expected<IamDialogRecord, std::string> IamDialogRecord::load(
    const std::span<const std::byte> record) {
  APP_PROFILE_FUNCTION();

  if (record.size() < k_header_size) {
    return std::expected<IamDialogRecord, std::string>{std::unexpect,
        fmt::format("IAM/DIALOG record: too small ({} bytes, expected at least {:#x})",
            record.size(),
            k_header_size)};
  }

  const std::int16_t nodes{read_at<std::int16_t>(record, 0x02U)};
  const std::int16_t cameras{read_at<std::int16_t>(record, 0x04U)};
  const std::int16_t camera_mirror{read_at<std::int16_t>(record, 0x06U)};
  if (nodes <= 0) {
    return std::expected<IamDialogRecord, std::string>{
        std::unexpect, fmt::format("IAM/DIALOG record: invalid node count {}", nodes)};
  }
  if (cameras < 0 || camera_mirror < 0) {
    return std::expected<IamDialogRecord, std::string>{std::unexpect,
        fmt::format("IAM/DIALOG record: invalid camera counts {}/{}", cameras, camera_mirror)};
  }
  if (cameras != camera_mirror) {
    return std::expected<IamDialogRecord, std::string>{std::unexpect,
        fmt::format(
            "IAM/DIALOG record: camera count mirror mismatch ({} != {})", cameras, camera_mirror)};
  }

  const std::uint64_t node_end{
      k_header_size + (static_cast<std::uint64_t>(nodes) * IamDialogNode::k_serialized_size)};
  const std::uint64_t camera_end{
      node_end + (static_cast<std::uint64_t>(cameras) * IamCameraRecord::k_serialized_size)};
  if (node_end > record.size()) {
    return std::expected<IamDialogRecord, std::string>{std::unexpect,
        fmt::format("IAM/DIALOG record: node table ends at {:#x}, outside {:#x}-byte record",
            node_end,
            record.size())};
  }
  if (camera_end > record.size()) {
    return std::expected<IamDialogRecord, std::string>{std::unexpect,
        fmt::format("IAM/DIALOG record: camera table ends at {:#x}, outside {:#x}-byte record",
            camera_end,
            record.size())};
  }

  std::vector<std::int16_t> camera_ids;
  camera_ids.reserve(static_cast<std::size_t>(cameras));
  for (std::int16_t index{0}; index < cameras; ++index) {
    const std::size_t camera_index{static_cast<std::size_t>(index)};
    const std::size_t offset{
        static_cast<std::size_t>(node_end) + (camera_index * IamCameraRecord::k_serialized_size)};
    auto camera{parse_iam_camera(record.subspan(offset, IamCameraRecord::k_serialized_size))};
    if (!camera) {
      return std::expected<IamDialogRecord, std::string>{
          std::unexpect, fmt::format("IAM/DIALOG camera {}: {}", index, camera.error())};
    }
    camera_ids.push_back(camera->camera_id);
  }

  for (std::int16_t index{0}; index < nodes; ++index) {
    const std::size_t node_index{static_cast<std::size_t>(index)};
    const std::size_t offset{k_header_size + (node_index * IamDialogNode::k_serialized_size)};
    const std::span<const std::byte> node{record.subspan(offset, IamDialogNode::k_serialized_size)};
    for (std::size_t field{0}; field < 9U; ++field) {
      const std::uint32_t relative{read_at<std::uint32_t>(node, field * 4U)};
      if (!valid_relative_offset(relative, record.size())) {
        return std::expected<IamDialogRecord, std::string>{std::unexpect,
            fmt::format("IAM/DIALOG node {}: relative offset {} value {:#x} is outside the "
                        "{:#x}-byte record",
                node_index,
                field,
                relative,
                record.size())};
      }
      if (relative != 0U && relative < camera_end) {
        return std::expected<IamDialogRecord, std::string>{std::unexpect,
            fmt::format("IAM/DIALOG node {}: relative offset {} value {:#x} points into the "
                        "fixed node/camera tables ending at {:#x}",
                node_index,
                field,
                relative,
                camera_end)};
      }
    }

    const std::uint32_t strings_offset{read_at<std::uint32_t>(node, 0x20U)};
    if (auto strings{read_node_strings(record, strings_offset, node_index)}; !strings) {
      return std::expected<IamDialogRecord, std::string>{std::unexpect, strings.error()};
    }

    const std::int16_t node_id{read_at<std::int16_t>(node, 0x2CU)};
    if (node_id != index) {
      return std::expected<IamDialogRecord, std::string>{std::unexpect,
          fmt::format("IAM/DIALOG node {}: serialized node ID is {} (expected {})",
              node_index,
              node_id,
              node_index)};
    }
    for (std::size_t slot{0}; slot < IamDialogNode::k_response_count; ++slot) {
      const std::int16_t target{read_at<std::int16_t>(node, 0x24U + (slot * 2U))};
      if (target >= nodes) {
        return std::expected<IamDialogRecord, std::string>{std::unexpect,
            fmt::format("IAM/DIALOG node {} response {}: target node {} does not exist",
                node_index,
                slot,
                target)};
      }
    }
    for (const std::size_t camera_field : {0x38U, 0x3AU, 0x3CU, 0x3EU}) {
      const std::int16_t camera_id{read_at<std::int16_t>(node, camera_field)};
      if (camera_id >= 0 && std::ranges::find(camera_ids, camera_id) == camera_ids.end()) {
        return std::expected<IamDialogRecord, std::string>{std::unexpect,
            fmt::format("IAM/DIALOG node {}: camera ID {} at +{:#x} does not exist",
                node_index,
                camera_id,
                camera_field)};
      }
    }
  }

  return IamDialogRecord{std::vector<std::byte>{record.begin(), record.end()}};
}

std::int16_t IamDialogRecord::character_id() const {
  return read_at<std::int16_t>(m_bytes, 0x00U);
}
std::int16_t IamDialogRecord::node_count() const {
  return read_at<std::int16_t>(m_bytes, 0x02U);
}
std::int16_t IamDialogRecord::camera_count() const {
  return read_at<std::int16_t>(m_bytes, 0x04U);
}
std::int16_t IamDialogRecord::camera_count_mirror() const {
  return read_at<std::int16_t>(m_bytes, 0x06U);
}

IamDialogNode IamDialogRecord::node_at(const std::size_t index) const {
  const std::span<const std::byte> node{std::span<const std::byte>{m_bytes}.subspan(
      k_header_size + (index * IamDialogNode::k_serialized_size),
      IamDialogNode::k_serialized_size)};
  IamDialogNode parsed;
  for (std::size_t slot{0}; slot < IamDialogNode::k_response_count; ++slot) {
    parsed.condition_script_offsets.at(slot) = read_at<std::uint32_t>(node, slot * 4U);
    parsed.action_script_offsets.at(slot) = read_at<std::uint32_t>(node, 0x10U + (slot * 4U));
    parsed.target_node_ids.at(slot) = read_at<std::int16_t>(node, 0x24U + (slot * 2U));
  }
  parsed.strings_offset = read_at<std::uint32_t>(node, 0x20U);
  parsed.node_id = read_at<std::int16_t>(node, 0x2CU);
  parsed.face_motion_base = fixed_string(node.subspan(0x2EU, 10U));
  parsed.response_camera_ids = {
      read_at<std::int16_t>(node, 0x38U), read_at<std::int16_t>(node, 0x3AU)};
  parsed.line_camera_ids = {read_at<std::int16_t>(node, 0x3CU), read_at<std::int16_t>(node, 0x3EU)};
  return parsed;
}

std::optional<IamDialogNode> IamDialogRecord::node_by_id(const std::int16_t node_id) const {
  if (node_id < 0 || node_id >= node_count()) {
    return std::nullopt;
  }
  return node_at(static_cast<std::size_t>(node_id));
}

std::optional<IamCameraRecord> IamDialogRecord::camera_by_id(const std::int16_t camera_id) const {
  const std::size_t start{
      k_header_size + (static_cast<std::size_t>(node_count()) * IamDialogNode::k_serialized_size)};
  const std::int16_t count{camera_count()};
  for (std::int16_t index{0}; index < count; ++index) {
    const std::size_t camera_index{static_cast<std::size_t>(index)};
    const auto camera{parse_iam_camera(std::span<const std::byte>{m_bytes}.subspan(
        start + (camera_index * IamCameraRecord::k_serialized_size),
        IamCameraRecord::k_serialized_size))};
    if (camera && camera->camera_id == camera_id) {
      return camera.value();
    }
  }
  return std::nullopt;
}

std::string_view IamDialogRecord::string_at(
    const IamDialogNode& node, const std::size_t index) const {
  std::size_t cursor{node.strings_offset};
  const std::span<const std::byte> data{m_bytes};
  for (std::size_t current{0}; current <= index; ++current) {
    const auto begin{data.begin() + static_cast<std::ptrdiff_t>(cursor)};
    const auto end{std::find(begin, data.end(), std::byte{})};
    const std::size_t length{static_cast<std::size_t>(end - begin)};
    if (current == index) {
      const void* raw{data.subspan(cursor, length).data()};
      return std::string_view{static_cast<const char*>(raw), length};
    }
    cursor += length + 1U;
  }
  return {};
}

std::string_view IamDialogRecord::main_line(const IamDialogNode& node) const {
  return string_at(node, 0U);
}
std::string_view IamDialogRecord::response_text(
    const IamDialogNode& node, const std::size_t slot) const {
  return slot < IamDialogNode::k_response_count ? string_at(node, slot + 1U) : std::string_view{};
}
std::string_view IamDialogRecord::automatic_player_line(const IamDialogNode& node) const {
  return string_at(node, 5U);
}

std::span<const std::byte> IamDialogRecord::program_at(const std::uint32_t offset) const {
  return offset == 0U ? std::span<const std::byte>{}
                      : std::span<const std::byte>{m_bytes}.subspan(offset);
}
std::span<const std::byte> IamDialogRecord::condition_program(
    const IamDialogNode& node, const std::size_t slot) const {
  return slot < IamDialogNode::k_response_count ? program_at(node.condition_script_offsets.at(slot))
                                                : std::span<const std::byte>{};
}
std::span<const std::byte> IamDialogRecord::action_program(
    const IamDialogNode& node, const std::size_t slot) const {
  return slot < IamDialogNode::k_response_count ? program_at(node.action_script_offsets.at(slot))
                                                : std::span<const std::byte>{};
}

}  // namespace App::Omikron
