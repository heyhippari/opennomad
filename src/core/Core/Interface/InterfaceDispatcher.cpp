#include "Core/Interface/InterfaceDispatcher.hpp"

#include <expected>
#include <optional>
#include <string>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"

namespace App {

void InterfaceDispatcher::set_interface_open_sink(InterfaceOpenSink sink) {
  m_interface_open_sink = std::move(sink);
}

void InterfaceDispatcher::set_interface_completion_sink(InterfaceCompletionSink sink) {
  m_interface_completion_sink = std::move(sink);
}

std::expected<InterfaceHandle, std::string> InterfaceDispatcher::open(
    const InterfaceOpenRequest& request) {
  APP_PROFILE_FUNCTION();

  m_last_request = request;
  App::Log::info("interface {} requested (operands {}, {})",
      request.interface_id,
      request.operand_b,
      request.operand_c);

  if (!m_interface_open_sink) {
    return std::expected<InterfaceHandle, std::string>{
        std::unexpect, "no interface open sink is wired"};
  }
  auto result{m_interface_open_sink(request)};
  if (!result) {
    return result;
  }

  const InterfaceHandle handle{result.value()};
  if (request.interface_id == k_main_menu_interface) {
    m_main_menu_active = true;
    m_active_handle = handle;
    App::Log::info("main menu active (handle id={} gen={})",
        handle.interface_id,
        handle.generation);
  }
  return handle;
}

void InterfaceDispatcher::notify_completion(const InterfaceCompletion& completion) {
  APP_PROFILE_FUNCTION();

  if (m_active_handle.has_value() && completion.handle == m_active_handle.value()) {
    m_main_menu_active = false;
    m_active_handle.reset();
    App::Log::info("main menu no longer active (interface {} completed)",
        completion.handle.interface_id);
  } else {
    App::Log::warn("interface completion ignored: stale or unknown handle (id={} gen={})",
        completion.handle.interface_id,
        completion.handle.generation);
    return;
  }

  if (m_interface_completion_sink) {
    m_interface_completion_sink(completion);
  }
}

void InterfaceDispatcher::open_preliminary_29() {
  APP_PROFILE_FUNCTION();

  m_preliminary_29_active = true;
  App::Log::info("preliminary interface 29 opened (splash)");
}

void InterfaceDispatcher::close_preliminary_29() {
  APP_PROFILE_FUNCTION();

  if (m_preliminary_29_active) {
    m_preliminary_29_active = false;
    App::Log::info("preliminary interface 29 closed");
  }
}

}  // namespace App
