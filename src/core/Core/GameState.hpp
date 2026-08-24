#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace App::Omikron {
class IamStart;
}

namespace App {

/// Fixed-capacity persistent IAM object-ID collection. `object_ids` retains
/// all serialized slots, including `-1` empty entries, for save fidelity.
struct PersistentObjectCollection {
  std::size_t capacity{0};
  std::vector<std::int16_t> object_ids;
};

/// Mutable, session-owned state initialized from immutable IAM/START data.
/// It deliberately has no dependency on transient AREA/SCENE contexts.
class GameState {
 public:
  /// Creates a mutable session copy of recovered persistent START regions.
  [[nodiscard]] static std::expected<GameState, std::string> from_start(
      const Omikron::IamStart& start);

  /// Reads one persistent ADDRESS bit. Out-of-range IDs are false; mutation
  /// is the checked operation used by compact VM handlers.
  [[nodiscard]] bool address_flag(std::uint16_t address_id) const;

  /// Sets or clears one persistent ADDRESS bit.
  [[nodiscard]] std::expected<void, std::string> set_address_flag(
      std::uint16_t address_id, bool enabled);

  /// Immutable raw ADDRESS bytes for inspection or later serialization.
  [[nodiscard]] std::span<const std::uint8_t> address_flags_raw() const;

  /// Full fixed-capacity object-ID slots for a supported persistent kind.
  [[nodiscard]] std::expected<std::span<const std::int16_t>, std::string>
  persistent_object_collection(std::uint16_t kind) const;

  /// Inserts an object ID at the front. Kinds 0/1 allow duplicates; kind 2
  /// suppresses an existing ID. Returns false for a full or suppressed add.
  [[nodiscard]] std::expected<bool, std::string> add_object_to_collection(
      std::uint16_t kind, std::int16_t object_id);

 private:
  std::vector<std::uint8_t> m_address_flags;
  std::array<PersistentObjectCollection, 3> m_object_collections;
};

}  // namespace App
