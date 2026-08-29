#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Core/GameDataLoader.hpp"
#include "Core/Omikron/CtlControlSet.hpp"

namespace {

struct RetailCtlFact {
  std::string_view path;
  std::uint32_t format_version;
  std::size_t move_count;
  std::size_t state_count;
};

}  // namespace

TEST_CASE("[RETAIL] adventure and combat CTL banks parse without truncation") {
  constexpr std::array<RetailCtlFact, 4> k_banks{{
      {"ANIMS/H1Avnt.CTL", 0x101U, 57U, 274U},
      {"ANIMS/F1Avnt.CTL", 0x101U, 57U, 274U},
      {"ANIMS/H1Cmbt.CTL", 0x103U, 25U, 233U},
      {"ANIMS/F1Cmbt.CTL", 0x203U, 25U, 233U},
  }};

  for (const RetailCtlFact& fact : k_banks) {
    CAPTURE(fact.path);
    const auto file{App::load_game_file(fact.path)};
    REQUIRE_MESSAGE(file.has_value(), file.error());
    const auto control_set{App::Omikron::CtlControlSet::load(file->bytes)};
    REQUIRE_MESSAGE(control_set.has_value(), control_set.error());
    CHECK_EQ(control_set->format_version(), fact.format_version);
    CHECK_EQ(control_set->moves().size(), fact.move_count);
    CHECK_EQ(control_set->states().size(), fact.state_count);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)