#include "Core/Interface/InterfaceDispatcher.hpp"

#include <expected>
#include <string>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"

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
  App::Log::debug(LogCategory::Interface,
      "interface {} requested — args=({},{})",
      request.interface_id,
      request.operand_b,
      request.operand_c);

  if (!m_interface_open_sink) {
    return std::expected<InterfaceHandle, std::string>{
        std::unexpect, "no interface open sink is wired"};
  }
  return m_interface_open_sink(request);
}

void InterfaceDispatcher::notify_completion(const InterfaceCompletion& completion) {
  APP_PROFILE_FUNCTION();

  if (!m_interface_completion_sink) {
    return;
  }
  m_interface_completion_sink(completion);
}

}  // namespace App
