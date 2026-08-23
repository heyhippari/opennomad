#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)

#include <array>
#include <cstddef>
#include <cstdint>

#include "Core/Omikron/Model3DO.hpp"
#include "Core/WorldPresentation.hpp"

TEST_SUITE("Core::WorldUvPhaseState") {
  TEST_CASE("Runtime global phases advance in elapsed seconds") {
    App::WorldUvPhaseState state;
    CHECK_EQ(state.u_phase(), 0.0);
    CHECK_EQ(state.v_phase(), 0.0);

    state.update(1.0F / 30.0F);
    CHECK(state.u_phase() == doctest::Approx(0.0004).epsilon(1e-7));
    CHECK(state.v_phase() == doctest::Approx(0.0004).epsilon(1e-7));

    App::WorldUvPhaseState one_second;
    one_second.update(1.0F);
    CHECK(one_second.u_phase() == doctest::Approx(0.012).epsilon(1e-12));
    CHECK(one_second.v_phase() == doctest::Approx(0.012).epsilon(1e-12));
  }

  TEST_CASE("Runtime global phases wrap and preserve fractional excess") {
    App::WorldUvPhaseState exact_period;
    for (std::size_t second{0}; second < 83U; ++second) {
      exact_period.update(1.0F);
    }
    exact_period.update(1.0F / 3.0F);
    CHECK(exact_period.u_phase() >= 0.0);
    CHECK(exact_period.u_phase() < 1.0);
    CHECK(exact_period.u_phase() == doctest::Approx(0.0).epsilon(1e-6));

    App::WorldUvPhaseState wrapped;
    wrapped.update(100.0F);
    CHECK(wrapped.u_phase() == doctest::Approx(0.2).epsilon(1e-12));
    CHECK(wrapped.v_phase() == doctest::Approx(0.2).epsilon(1e-12));

    App::WorldUvPhaseState large_delta;
    large_delta.update(250.5F);
    CHECK(large_delta.u_phase() == doctest::Approx(0.006).epsilon(1e-12));
    CHECK(large_delta.v_phase() == doctest::Approx(0.006).epsilon(1e-12));
  }

  TEST_CASE("Mesh flags independently select U and V phases") {
    constexpr float u_phase{0.25F};
    constexpr float v_phase{0.75F};
    constexpr std::uint32_t u_flag{
        static_cast<std::uint32_t>(App::Omikron::MeshFlags::k_uv_scroll_u)};
    constexpr std::uint32_t v_flag{
        static_cast<std::uint32_t>(App::Omikron::MeshFlags::k_uv_scroll_v)};

    CHECK_EQ(
        App::Omikron::uv_scroll_offset(0U, u_phase, v_phase), (std::array<float, 2>{0.0F, 0.0F}));
    CHECK_EQ(App::Omikron::uv_scroll_offset(u_flag, u_phase, v_phase),
        (std::array<float, 2>{u_phase, 0.0F}));
    CHECK_EQ(App::Omikron::uv_scroll_offset(v_flag, u_phase, v_phase),
        (std::array<float, 2>{0.0F, v_phase}));
    CHECK_EQ(App::Omikron::uv_scroll_offset(u_flag | v_flag, u_phase, v_phase),
        (std::array<float, 2>{u_phase, v_phase}));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while)
