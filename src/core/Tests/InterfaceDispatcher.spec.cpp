#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, readability-suspicious-call-argument)

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "Core/Interface/InterfaceDispatcher.hpp"

namespace {

using App::InterfaceCompletion;
using App::InterfaceDispatcher;
using App::InterfaceHandle;
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

  TEST_CASE("a successful sink activates the main menu and returns the handle") {
    InterfaceDispatcher dispatcher;
    std::vector<InterfaceOpenRequest> received;
    const InterfaceHandle handle{.interface_id = InterfaceDispatcher::k_main_menu_interface,
        .generation = 3};
    dispatcher.set_interface_open_sink(
        [&received, handle](const InterfaceOpenRequest& request)
            -> std::expected<InterfaceHandle, std::string> {
          received.push_back(request);
          return handle;
        });

    const auto result{dispatcher.open(k_menu_request)};
    REQUIRE(result.has_value());
    CHECK(result.value() == handle);
    REQUIRE_EQ(received.size(), 1U);
    CHECK_EQ(received.at(0).interface_id, InterfaceDispatcher::k_main_menu_interface);
    CHECK_EQ(received.at(0).operand_b, -1);
    CHECK_EQ(received.at(0).operand_c, 19);
    CHECK(dispatcher.main_menu_active());
    CHECK(dispatcher.active_handle() == handle);
    CHECK_EQ(dispatcher.last_request().interface_id, InterfaceDispatcher::k_main_menu_interface);
  }

  TEST_CASE("a failing sink reports its error and never activates the menu") {
    InterfaceDispatcher dispatcher;
    dispatcher.set_interface_open_sink([](const InterfaceOpenRequest& /*request*/)
                                           -> std::expected<InterfaceHandle, std::string> {
      return std::expected<InterfaceHandle, std::string>{std::unexpect, "menu construction failed"};
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
        [&received_id](const InterfaceOpenRequest& request)
            -> std::expected<InterfaceHandle, std::string> {
          received_id = request.interface_id;
          return InterfaceHandle{.interface_id = request.interface_id, .generation = 1};
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

  TEST_CASE("a matching completion deactivates the menu and forwards to the sink") {
    InterfaceDispatcher dispatcher;
    const InterfaceHandle handle{.interface_id = InterfaceDispatcher::k_main_menu_interface,
        .generation = 5};
    dispatcher.set_interface_open_sink(
        [handle](const InterfaceOpenRequest&) -> std::expected<InterfaceHandle, std::string> {
          return handle;
        });

    std::vector<InterfaceCompletion> completions;
    dispatcher.set_interface_completion_sink(
        [&completions](const InterfaceCompletion& completion) {
          completions.push_back(completion);
        });

    REQUIRE(dispatcher.open(k_menu_request).has_value());
    CHECK(dispatcher.main_menu_active());

    dispatcher.notify_completion(InterfaceCompletion{.handle = handle, .result = 0});
    CHECK_FALSE(dispatcher.main_menu_active());
    CHECK_FALSE(dispatcher.active_handle().has_value());
    REQUIRE_EQ(completions.size(), 1U);
    CHECK(completions.at(0).handle == handle);
  }

  TEST_CASE("a stale completion is rejected and never forwards") {
    InterfaceDispatcher dispatcher;
    const InterfaceHandle handle{.interface_id = InterfaceDispatcher::k_main_menu_interface,
        .generation = 5};
    dispatcher.set_interface_open_sink(
        [handle](const InterfaceOpenRequest&) -> std::expected<InterfaceHandle, std::string> {
          return handle;
        });

    std::size_t forwarded{0};
    dispatcher.set_interface_completion_sink(
        [&forwarded](const InterfaceCompletion& /*completion*/) { ++forwarded; });

    REQUIRE(dispatcher.open(k_menu_request).has_value());
    CHECK(dispatcher.main_menu_active());

    const InterfaceCompletion stale{
        .handle = InterfaceHandle{.interface_id = InterfaceDispatcher::k_main_menu_interface,
            .generation = 99},
        .result = 0};
    dispatcher.notify_completion(stale);
    CHECK(dispatcher.main_menu_active());  // Still active: stale ignored.
    CHECK_EQ(forwarded, 0U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, readability-suspicious-call-argument)
