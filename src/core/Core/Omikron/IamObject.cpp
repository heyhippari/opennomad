#include "Core/Omikron/IamObject.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Core/Omikron/IamArchive.hpp"

namespace App::Omikron {

namespace {

template <typename Value>
[[nodiscard]] Value read_at(const std::span<const std::byte> bytes, const std::size_t offset) {
  Value value{};
  std::memcpy(&value, bytes.subspan(offset, sizeof(Value)).data(), sizeof(Value));
  return value;
}

[[nodiscard]] std::expected<std::string, std::string> read_nul_string(
    const std::span<const std::byte> bytes, const std::size_t offset, const std::size_t end) {
  if (offset >= end || end > bytes.size()) {
    return std::expected<std::string, std::string>{std::unexpect,
        fmt::format("IAM/OBJECT string range [{:#x}, {:#x}) is invalid", offset, end)};
  }
  const void* raw{bytes.data() + offset};
  const void* nul{std::memchr(raw, '\0', end - offset)};
  if (nul == nullptr) {
    return std::expected<std::string, std::string>{std::unexpect,
        fmt::format("IAM/OBJECT string at {:#x} is not NUL-terminated in its fixed field", offset)};
  }
  const auto* begin{static_cast<const char*>(raw)};
  const auto* terminator{static_cast<const char*>(nul)};
  return std::string{begin, terminator};
}

}  // namespace

std::expected<IamObjectRecord, std::string> IamObjectRecord::load(
    const std::span<const std::byte> record) {
  if (record.size() != k_serialized_size) {
    return std::expected<IamObjectRecord, std::string>{std::unexpect,
        fmt::format("IAM/OBJECT record has {} bytes; expected fixed size {}",
            record.size(),
            k_serialized_size)};
  }
  auto audio_stem{read_nul_string(record, k_offset_audio_stem, k_offset_subtitle)};
  if (!audio_stem) {
    return std::expected<IamObjectRecord, std::string>{
        std::unexpect, std::move(audio_stem).error()};
  }
  auto subtitle{read_nul_string(record, k_offset_subtitle, record.size())};
  if (!subtitle) {
    return std::expected<IamObjectRecord, std::string>{std::unexpect, std::move(subtitle).error()};
  }
  return IamObjectRecord{std::vector<std::byte>{record.begin(), record.end()},
      read_at<std::uint16_t>(record, k_offset_type),
      std::move(audio_stem).value(),
      std::move(subtitle).value()};
}

std::expected<IamObjectRecord, std::string> IamObjectRecord::load_from_archive(
    const std::span<const std::byte> archive, const std::uint16_t object_id) {
  const IamFixedStrideArchive fixed{archive, k_serialized_size, k_archive_stride};
  auto record{fixed.read_record(object_id)};
  if (!record) {
    return std::expected<IamObjectRecord, std::string>{
        std::unexpect, fmt::format("IAM/OBJECT {}: {}", object_id, std::move(record).error())};
  }
  return load(record.value());
}

}  // namespace App::Omikron
