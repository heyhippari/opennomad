#include "Core/Interface/InterfaceDispatcher.hpp"

#include <fmt/format.h>

#include <expected>
#include <string>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"

namespace App {

void InterfaceDispatcher::set_menu_activation_sink(MenuActivationSink sink) {
  m_menu_activation_sink = std::move(sink);
}

std::expected<void, std::string> InterfaceDispatcher::open(const InterfaceOpenRequest& request) {
  APP_PROFILE_FUNCTION();

  m_last_request = request;
  App::Log::info("interface {} requested (operands {}, {})",
      request.interface_id,
      request.operand_b,
      request.operand_c);

  if (request.interface_id == k_main_menu_interface) {
    if (!m_menu_activation_sink) {
      return std::expected<void, std::string>{
          std::unexpect, "interface 29 requested but no menu activation sink is wired"};
    }
    if (auto result{m_menu_activation_sink()}; !result) {
      return result;
    }
    m_main_menu_active = true;
    App::Log::info("main menu active");
    return {};
  }

  return std::expected<void, std::string>{
      std::unexpect, fmt::format("interface {} is unsupported", request.interface_id)};
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
