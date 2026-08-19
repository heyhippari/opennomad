#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, readability-suspicious-call-argument)

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Omikron/IamStringTable.hpp"

namespace {

using App::Omikron::IamStringTable;
using namespace std::string_literals;

std::vector<std::byte> make_bytes(const std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const char character : text) {
    result.push_back(static_cast<std::byte>(character));
  }
  return result;
}

}  // namespace

TEST_SUITE("Core::Omikron::IamStringTable") {
  TEST_CASE("parses the recovered IAM/Menu prefix") {
    const auto table{IamStringTable::load(make_bytes(
        "New Game\0Load Game\0Delete\0Rename\0Options\0Quit\0"s))};
    REQUIRE(table.has_value());
    CHECK_EQ(table->size(), 6U);
    CHECK(table->at(0) == "New Game");
    CHECK(table->at(1) == "Load Game");
    CHECK(table->at(4) == "Options");
    CHECK(table->at(5) == "Quit");
  }

  TEST_CASE("a trailing NUL terminates the last entry without adding an empty one") {
    const auto table{IamStringTable::load(make_bytes("A\0B\0"s))};
    REQUIRE(table.has_value());
    CHECK_EQ(table->size(), 2U);
    CHECK(table->at(0) == "A");
    CHECK(table->at(1) == "B");
  }

  TEST_CASE("a missing trailing NUL still yields the final entry") {
    const auto table{IamStringTable::load(make_bytes("A\0B"s))};
    REQUIRE(table.has_value());
    CHECK_EQ(table->size(), 2U);
    CHECK(table->at(1) == "B");
  }

  TEST_CASE("consecutive NULs preserve empty entries") {
    const auto table{IamStringTable::load(make_bytes("A\0\0B\0"s))};
    REQUIRE(table.has_value());
    CHECK_EQ(table->size(), 3U);
    CHECK(table->at(0) == "A");
    CHECK(table->at(1) == "");
    CHECK(table->at(2) == "B");
  }

  TEST_CASE("a single NUL is one empty entry") {
    const auto table{IamStringTable::load(make_bytes("\0"s))};
    REQUIRE(table.has_value());
    CHECK_EQ(table->size(), 1U);
    CHECK(table->at(0) == "");
  }

  TEST_CASE("empty input is a valid empty table") {
    const auto table{IamStringTable::load(make_bytes(""s))};
    REQUIRE(table.has_value());
    CHECK_EQ(table->size(), 0U);
  }

  TEST_CASE("out-of-range indices are reported") {
    const auto table{IamStringTable::load(make_bytes("A\0B\0"s))};
    REQUIRE(table.has_value());
    CHECK_FALSE(table->try_at(2).has_value());
    CHECK(table->at(99) == "");
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, readability-suspicious-call-argument)
