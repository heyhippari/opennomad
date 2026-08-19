#include "Core/Interface/InterfaceDispatcher.hpp"

#include <expected>
#include <string>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"

namespace App {

void InterfaceDispatcher::set_interface_open_sink(InterfaceOpenSink sink) {
  m_interface_open_sink = std::move(sink);
}

std::expected<void, std::string> InterfaceDispatcher::open(const InterfaceOpenRequest& request) {
  APP_PROFILE_FUNCTION();

  m_last_request = request;
  App::Log::info("interface {} requested (operands {}, {})",
      request.interface_id,
      request.operand_b,
      request.operand_c);

  if (!m_interface_open_sink) {
    return std::expected<void, std::string>{
        std::unexpect, "no interface open sink is wired"};
  }
  if (auto result{m_interface_open_sink(request.interface_id)}; !result) {
    return result;
  }

  if (request.interface_id == k_main_menu_interface) {
    m_main_menu_active = true;
    App::Log::info("main menu active");
  }
  return {};
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
