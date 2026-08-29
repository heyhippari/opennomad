#include "Core/SplashScene.hpp"

#include <doctest/doctest.h>

#include <array>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)

TEST_SUITE("Core::SplashScene") {
  TEST_CASE("Contain-fit keeps a wide image full-width with letterbox bars") {
    const auto bounds{App::SplashScene::compute_contain_bounds(512, 256, 1024, 1024)};
    CHECK_EQ(bounds, std::array<float, 4>{-1.0F, 1.0F, -0.5F, 0.5F});
  }

  TEST_CASE("Contain-fit keeps a tall image full-height with pillarbox bars") {
    const auto bounds{App::SplashScene::compute_contain_bounds(256, 512, 1024, 1024)};
    CHECK_EQ(bounds, std::array<float, 4>{-0.5F, 0.5F, -1.0F, 1.0F});
  }

  TEST_CASE("Contain-fit fills the viewport when the aspects match") {
    const auto bounds{App::SplashScene::compute_contain_bounds(512, 256, 1024, 512)};
    CHECK_EQ(bounds, std::array<float, 4>{-1.0F, 1.0F, -1.0F, 1.0F});
  }

  TEST_CASE("Contain-fit handles a classic 4:3 image on a 16:9 viewport") {
    const auto bounds{App::SplashScene::compute_contain_bounds(640, 480, 1920, 1080)};
    CHECK(bounds.at(0) == doctest::Approx(-0.75F));
    CHECK(bounds.at(1) == doctest::Approx(0.75F));
    CHECK_EQ(bounds.at(2), -1.0F);
    CHECK_EQ(bounds.at(3), 1.0F);
  }

  TEST_CASE("Contain-fit falls back to the full quad for degenerate dimensions") {
    const auto bounds{App::SplashScene::compute_contain_bounds(0, 0, 1920, 1080)};
    CHECK_EQ(bounds, std::array<float, 4>{-1.0F, 1.0F, -1.0F, 1.0F});
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c)
