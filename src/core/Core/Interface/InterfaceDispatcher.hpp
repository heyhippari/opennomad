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

/// Routes interface-open requests to the generic interface system and
/// forwards completions back to the scenario controller. Pure transport: it
/// performs no interface-specific lifecycle work. Startup-specific tracking
/// (interface 29, the preliminary splash phase) lives in
/// ScenarioStartupController, which owns the dispatcher instance.
class InterfaceDispatcher {
 public:
  /// Wires the generic interface-opening sink. Without a sink, every request
  /// is reported as unsupported.
  void set_interface_open_sink(InterfaceOpenSink sink);

  /// Wires the completion sink (scenario resumption).
  void set_interface_completion_sink(InterfaceCompletionSink sink);

  /// Dispatches one interface-open request. Delegates to the wired sink and
  /// returns the opened instance handle, or the sink's failure.
  [[nodiscard]] std::expected<InterfaceHandle, std::string> open(
      const InterfaceOpenRequest& request);

  /// Forwards a deferred interface completion to the completion sink. Stale
  /// handle filtering is the scenario controller's responsibility, not the
  /// transport's.
  void notify_completion(const InterfaceCompletion& completion);

  /// The most recent request (diagnostics).
  [[nodiscard]] const InterfaceOpenRequest& last_request() const {
    return m_last_request;
  }

 private:
  InterfaceOpenRequest m_last_request;
  InterfaceOpenSink m_interface_open_sink;
  InterfaceCompletionSink m_interface_completion_sink;
};

}  // namespace App
