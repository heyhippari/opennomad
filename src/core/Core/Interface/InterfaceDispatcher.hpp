#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
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

/// Lightweight identity of one opened interface instance. The generation
/// disambiguates completions from an older instance of the same interface ID.
struct InterfaceHandle {
  std::uint16_t interface_id{0};
  std::uint32_t generation{0};

  friend constexpr bool operator==(const InterfaceHandle&, const InterfaceHandle&) = default;
};

/// One deferred interface completion: the instance that completed and the
/// provisional result value (exact Runtime semantics remain unresolved).
struct InterfaceCompletion {
  InterfaceHandle handle;
  std::int16_t result{0};
};

/// Caller-wired generic interface opener: constructs the requested interface
/// through the interface system and reports the opened instance handle (or a
/// construction failure). Wired by the application to the InterfaceManager;
/// the dispatcher itself performs no interface-specific work.
using InterfaceOpenSink =
    std::function<std::expected<InterfaceHandle, std::string>(const InterfaceOpenRequest&)>;

/// Caller-wired completion sink: delivered an interface completion so the
/// scenario controller can resume a waiting AREA script.
using InterfaceCompletionSink =
    std::function<void(const InterfaceCompletion&)>;

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

  /// Wires the completion sink (scenario resumption).
  void set_interface_completion_sink(InterfaceCompletionSink sink);

  /// Dispatches one interface-open request (the final main-menu instance).
  /// Delegates to the wired sink and marks the main menu active when
  /// interface 29 opened successfully. Returns the opened instance handle.
  [[nodiscard]] std::expected<InterfaceHandle, std::string> open(
      const InterfaceOpenRequest& request);

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

  /// True once interface 29 has been requested, the main menu is active, and
  /// it has not yet completed.
  [[nodiscard]] bool main_menu_active() const {
    return m_main_menu_active;
  }

  /// The handle of the active main-menu instance, or nullopt when none.
  [[nodiscard]] std::optional<InterfaceHandle> active_handle() const {
    return m_active_handle;
  }

  /// Delivers a deferred interface completion. When the handle matches the
  /// active main-menu instance, the menu lifecycle is updated (active becomes
  /// false) and the completion is forwarded to the completion sink. Stale or
  /// unknown handles are logged and ignored.
  void notify_completion(const InterfaceCompletion& completion);

  /// The most recent request (diagnostics).
  [[nodiscard]] const InterfaceOpenRequest& last_request() const {
    return m_last_request;
  }

 private:
  bool m_main_menu_active{false};
  bool m_preliminary_29_active{false};
  InterfaceOpenRequest m_last_request;
  InterfaceOpenSink m_interface_open_sink;
  InterfaceCompletionSink m_interface_completion_sink;
  std::optional<InterfaceHandle> m_active_handle;
};

}  // namespace App
