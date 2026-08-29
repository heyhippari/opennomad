#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "Core/Interface/InterfacePresentation.hpp"

namespace App::Interface {

class InterfaceManager;
struct InterfaceInstance;

/// Interface-specific initializer, mirroring Runtime's per-descriptor init
/// function (for interface 29: StartMenu_Initialize @ 0x00479D10). Runs
/// after the generic opener has loaded the descriptor's resources.
using InterfaceInitFn = void (*)(InterfaceManager& manager, InterfaceInstance& instance);

/// Interface-specific teardown, mirroring Runtime's per-descriptor destroy
/// function (for interface 29: @ 0x00479F30). Runs before the generic opener
/// releases the descriptor's resources.
using InterfaceDestroyFn = void (*)(InterfaceManager& manager, InterfaceInstance& instance);

struct InterfaceSoundSet {
  std::string_view navigate;
  std::string_view confirm;
  std::string_view cancel;
};

/// Recovered Runtime catalog entry. This records only established static
/// identity and does not imply that OpenNomad can instantiate the interface.
struct RuntimeInterfaceMetadata {
  std::int32_t id{-1};
  std::string_view name;
};

[[nodiscard]] const RuntimeInterfaceMetadata* runtime_interface_metadata_for_id(std::int32_t id);

/// Runnable OpenNomad interface descriptor. Recovered fields are retained
/// where known; callbacks and presentation policy describe implemented
/// OpenNomad behavior.
struct InterfaceDescriptor {
  std::int32_t id{-1};
  std::string_view name;

  /// Basename loaded from `I2D/bitmaps/<bitmap_name>` (extension included,
  /// e.g. "gfxint.bmp"). Empty when the interface has no bitmap (interface
  /// 35 "OPTIONS").
  std::string_view bitmap_name;

  /// Basename loaded from `IAM/<string_table_name>` (no extension, e.g.
  /// "Menu"). Empty when the interface has no string table.
  std::string_view string_table_name;

  /// Interface preloaded/pre-registered during this interface's lifetime
  /// (interface 29 owns interface 35 "OPTIONS" in Runtime). OpenNomad keeps
  /// the descriptor relationship and may also keep both interface instances
  /// resident while the companion is focused.
  std::optional<std::int32_t> companion_interface;

  std::optional<InterfaceSoundSet> sounds;

  InterfaceInitFn init{nullptr};
  InterfaceDestroyFn destroy{nullptr};

  /// Preserved recovered flag word (interface 29: 0x20000400). Exact bit
  /// semantics are not yet established; kept for RE correlation.
  std::uint32_t runtime_flags{0};

  /// OpenNomad-only presentation policy. Default-empty so recovered interfaces
  /// remain instantaneous unless their descriptor explicitly opts in.
  InterfacePresentationHints presentation_hints{};
};

}  // namespace App::Interface
