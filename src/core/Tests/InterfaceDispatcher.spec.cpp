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
    .interface_id = 29, .operand_b = -1, .operand_c = 19};

}  // namespace

TEST_SUITE("Core::Interface::InterfaceDispatcher") {
  TEST_CASE("an open request without a wired sink is unsupported") {
    InterfaceDispatcher dispatcher;

    const auto result{dispatcher.open(k_menu_request)};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("no interface open sink") != std::string::npos);
  }

  TEST_CASE("a successful sink returns the handle and records the request") {
    InterfaceDispatcher dispatcher;
    std::vector<InterfaceOpenRequest> received;
    const InterfaceHandle handle{.interface_id = 29, .generation = 3};
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
    CHECK_EQ(received.at(0).interface_id, 29U);
    CHECK_EQ(received.at(0).operand_b, -1);
    CHECK_EQ(received.at(0).operand_c, 19);
    CHECK_EQ(dispatcher.last_request().interface_id, 29U);
  }

  TEST_CASE("a failing sink reports its error") {
    InterfaceDispatcher dispatcher;
    dispatcher.set_interface_open_sink([](const InterfaceOpenRequest& /*request*/)
                                           -> std::expected<InterfaceHandle, std::string> {
      return std::expected<InterfaceHandle, std::string>{std::unexpect, "open failed"};
    });

    const auto result{dispatcher.open(k_menu_request)};
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("open failed") != std::string::npos);
  }

  TEST_CASE("any interface ID delegates to the sink unchanged") {
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
  }

  TEST_CASE("a completion is forwarded to the completion sink verbatim") {
    InterfaceDispatcher dispatcher;
    std::vector<InterfaceCompletion> completions;
    dispatcher.set_interface_completion_sink(
        [&completions](const InterfaceCompletion& completion) {
          completions.push_back(completion);
        });

    const InterfaceCompletion completion{
        .handle = InterfaceHandle{.interface_id = 29, .generation = 5}, .result = 0};
    dispatcher.notify_completion(completion);
    REQUIRE_EQ(completions.size(), 1U);
    CHECK(completions.at(0).handle == completion.handle);
    CHECK_EQ(completions.at(0).result, 0);
  }

  TEST_CASE("a completion without a wired sink is a harmless no-op") {
    InterfaceDispatcher dispatcher;
    dispatcher.notify_completion(
        InterfaceCompletion{.handle = InterfaceHandle{.interface_id = 29, .generation = 1},
            .result = 0});
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, readability-suspicious-call-argument)
