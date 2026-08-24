#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace App::Omikron {

/// Checked, deliberately partial representation of one fixed IAM/OBJECT
/// record. Fields outside the confirmed 0x5C voice-over path remain opaque in
/// the owning byte copy rather than being guessed as structure.
class IamObjectRecord {
 public:
  static constexpr std::size_t k_serialized_size{0x518};
  /// Runtime 0x00409AE0 calls the generic IAM loader with a positive 0x800
  /// final argument, selecting fixed-stride storage rather than a paged
  /// {offset,size} index.
  static constexpr std::size_t k_archive_stride{0x800};
  static constexpr std::size_t k_offset_type{0x02};
  static constexpr std::size_t k_offset_audio_stem{0x0E};
  static constexpr std::size_t k_offset_subtitle{0x118};

  [[nodiscard]] static std::expected<IamObjectRecord, std::string> load(
      std::span<const std::byte> record);
  [[nodiscard]] static std::expected<IamObjectRecord, std::string> load_from_archive(
      std::span<const std::byte> archive, std::uint16_t object_id);

  [[nodiscard]] std::uint16_t object_type() const {
    return m_object_type;
  }
  [[nodiscard]] const std::string& audio_stem() const {
    return m_audio_stem;
  }
  [[nodiscard]] const std::string& subtitle() const {
    return m_subtitle;
  }
  [[nodiscard]] std::span<const std::byte> bytes() const {
    return m_bytes;
  }

 private:
  explicit IamObjectRecord(std::vector<std::byte> bytes,
      std::uint16_t object_type,
      std::string audio_stem,
      std::string subtitle)
      : m_bytes(std::move(bytes)),
        m_object_type(object_type),
        m_audio_stem(std::move(audio_stem)),
        m_subtitle(std::move(subtitle)) {}

  std::vector<std::byte> m_bytes;
  std::uint16_t m_object_type{0};
  std::string m_audio_stem;
  std::string m_subtitle;
};

}  // namespace App::Omikron
