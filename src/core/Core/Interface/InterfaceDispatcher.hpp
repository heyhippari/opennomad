#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>

namespace App {

/// One interface-open instruction decoded from the AREA bytecode (opcode
/// 0x46). Operands -1 and 19 are preserved verbatim even though their full
/// meanings remain unknown.
struct InterfaceOpenRequest {
  std::uint16_t interface_id{0};
  std::int16_t operand_b{-1};
  std::int16_t operand_c{19};
};

/// Caller-wired generic interface opener: constructs the requested interface
/// through the interface system and reports construction failure. Wired by
/// the application to the InterfaceManager; the dispatcher itself performs
/// no interface-specific work.
using InterfaceOpenSink =
    std::function<std::expected<void, std::string>(std::uint16_t interface_id)>;

/// Routes interface-open requests to the generic interface system. Interface
/// 29 is the main menu, tracked separately so the startup orchestration can
/// assert that the area script really opened it.
///
/// Interface 29 has two distinct roles during startup: the preliminary
/// splash instance (opened while the splash is shown) and the final main
/// menu (opened by AREA bytecode opcode 0x46). They are tracked separately
/// so "interface 29 exists" alone is never mistaken for the real menu.
class InterfaceDispatcher {
 public:
  /// Interface ID of the main menu (confirmed from IAM/AREA record 118).
  static constexpr std::uint16_t k_main_menu_interface{29};

  /// Wires the generic interface-opening sink. Without a sink, every request
  /// is reported as unsupported.
  void set_interface_open_sink(InterfaceOpenSink sink);

  /// Dispatches one interface-open request (the final main-menu instance).
  /// Delegates to the wired sink and marks the main menu active when
  /// interface 29 opened successfully.
  [[nodiscard]] std::expected<void, std::string> open(const InterfaceOpenRequest& request);

  /// Opens the preliminary interface 29 instance shown while the splash runs.
  /// This is not the final main menu; close_preliminary_29() must be called
  /// before the area script opens interface 29 as the real menu.
  void open_preliminary_29();

  /// Closes the preliminary interface 29 instance (idempotent).
  void close_preliminary_29();

  /// True while the preliminary splash interface 29 instance is open.
  [[nodiscard]] bool preliminary_29_active() const {
    return m_preliminary_29_active;
  }

  /// True once interface 29 has been requested and the main menu is active.
  [[nodiscard]] bool main_menu_active() const {
    return m_main_menu_active;
  }

  /// The most recent request (diagnostics).
  [[nodiscard]] const InterfaceOpenRequest& last_request() const {
    return m_last_request;
  }

 private:
  bool m_main_menu_active{false};
  bool m_preliminary_29_active{false};
  InterfaceOpenRequest m_last_request;
  InterfaceOpenSink m_interface_open_sink;
};

}  // namespace App
