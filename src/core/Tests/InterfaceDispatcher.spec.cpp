#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, readability-suspicious-call-argument)

#include <cstdint>
#include <expected>
#include <string>

#include "Core/Interface/InterfaceDispatcher.hpp"

namespace {

using App::InterfaceDispatcher;
using App::InterfaceOpenRequest;

constexpr InterfaceOpenRequest k_menu_request{
    .interface_id = InterfaceDispatcher::k_main_menu_interface, .operand_b = -1, .operand_c = 19};

}  // namespace

TEST_SUITE("Core::Interface::InterfaceDispatcher") {
  TEST_CASE("interface 29 without a wired sink is unsupported and never active") {
    InterfaceDispatcher dispatcher;

    const auto result{dispatcher.open(k_menu_request)};
    REQUIRE_FALSE(result.has_value());
    CHECK_FALSE(dispatcher.main_menu_active());
  }

  TEST_CASE("a successful sink activates the main menu and receives the id") {
    InterfaceDispatcher dispatcher;
    std::uint16_t received_id{0};
    dispatcher.set_interface_open_sink(
        [&received_id](const std::uint16_t id) -> std::expected<void, std::string> {
          received_id = id;
          return {};
        });

    const auto result{dispatcher.open(k_menu_request)};
    REQUIRE(result.has_value());
    CHECK_EQ(received_id, InterfaceDispatcher::k_main_menu_interface);
    CHECK(dispatcher.main_menu_active());
    CHECK_EQ(dispatcher.last_request().interface_id, InterfaceDispatcher::k_main_menu_interface);
  }

  TEST_CASE("a failing sink reports its error and never activates the menu") {
    InterfaceDispatcher dispatcher;
    dispatcher.set_interface_open_sink([](const std::uint16_t /*id*/)
                                           -> std::expected<void, std::string> {
      return std::expected<void, std::string>{std::unexpect, "menu construction failed"};
    });

    const auto result{dispatcher.open(k_menu_request)};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("menu construction failed") != std::string::npos);
    CHECK_FALSE(dispatcher.main_menu_active());
  }

  TEST_CASE("a non-menu interface delegates to the sink without activating the menu") {
    InterfaceDispatcher dispatcher;
    std::uint16_t received_id{0};
    dispatcher.set_interface_open_sink(
        [&received_id](const std::uint16_t id) -> std::expected<void, std::string> {
          received_id = id;
          return {};
        });

    const auto result{dispatcher.open(InterfaceOpenRequest{.interface_id = 7})};
    REQUIRE(result.has_value());
    CHECK_EQ(received_id, 7U);
    CHECK_FALSE(dispatcher.main_menu_active());
  }

  TEST_CASE("the preliminary interface 29 instance is independent of the final menu") {
    InterfaceDispatcher dispatcher;

    CHECK_FALSE(dispatcher.preliminary_29_active());
    dispatcher.open_preliminary_29();
    CHECK(dispatcher.preliminary_29_active());
    CHECK_FALSE(dispatcher.main_menu_active());
    dispatcher.close_preliminary_29();
    CHECK_FALSE(dispatcher.preliminary_29_active());
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, readability-suspicious-call-argument)
