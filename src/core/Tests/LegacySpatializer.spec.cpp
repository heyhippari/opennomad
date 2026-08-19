#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <array>
#include <cmath>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Audio/LegacySpatializer.hpp"

namespace {

using App::Audio::AudioListenerState;
using App::Audio::SoundEmitterState;
using App::Audio::Vec3;

AudioListenerState listener_at(const Vec3 position) {
  AudioListenerState listener;
  listener.position = position;
  listener.forward = Vec3{0.0F, 0.0F, -1.0F};
  listener.up = Vec3{0.0F, 1.0F, 0.0F};
  return listener;
}

SoundEmitterState emitter_at(const Vec3 position, const float min_distance = 1.0F,
    const float max_distance = 10.0F) {
  SoundEmitterState emitter;
  emitter.position = position;
  emitter.minimum_distance = min_distance;
  emitter.maximum_distance = max_distance;
  return emitter;
}

}  // namespace

TEST_SUITE("Core::Audio::LegacySpatializer") {
  TEST_CASE("Attenuation is full inside the minimum distance") {
    CHECK_EQ(App::Audio::attenuation_gain(0.0F, 1.0F, 10.0F), doctest::Approx(1.0F));
    CHECK_EQ(App::Audio::attenuation_gain(0.5F, 1.0F, 10.0F), doctest::Approx(1.0F));
  }

  TEST_CASE("Attenuation is zero at and beyond the maximum distance") {
    CHECK_EQ(App::Audio::attenuation_gain(10.0F, 1.0F, 10.0F), doctest::Approx(0.0F));
    CHECK_EQ(App::Audio::attenuation_gain(20.0F, 1.0F, 10.0F), doctest::Approx(0.0F));
  }

  TEST_CASE("Attenuation midpoint follows the linear curve") {
    CHECK_EQ(App::Audio::attenuation_gain(5.5F, 1.0F, 10.0F), doctest::Approx(0.5F));
  }

  TEST_CASE("Attenuation handles a degenerate min/max range safely") {
    CHECK_EQ(App::Audio::attenuation_gain(1.0F, 5.0F, 5.0F), doctest::Approx(1.0F));
    CHECK_EQ(App::Audio::attenuation_gain(6.0F, 5.0F, 5.0F), doctest::Approx(0.0F));
  }

  TEST_CASE("Pan favours left when the source is to listener-left") {
    const Vec3 right{1.0F, 0.0F, 0.0F};
    const Vec3 source_left{-1.0F, 0.0F, 0.0F};
    CHECK_LT(App::Audio::pan_factor(source_left, right), 0.0F);
  }

  TEST_CASE("Pan favours right when the source is to listener-right") {
    const Vec3 right{1.0F, 0.0F, 0.0F};
    const Vec3 source_right{1.0F, 0.0F, 0.0F};
    CHECK_GT(App::Audio::pan_factor(source_right, right), 0.0F);
  }

  TEST_CASE("Coincident positions produce neutral finite pan") {
    const Vec3 right{1.0F, 0.0F, 0.0F};
    const float pan{App::Audio::pan_factor(Vec3{0.0F, 0.0F, 0.0F}, right)};
    CHECK(std::isfinite(pan));
    CHECK_EQ(pan, doctest::Approx(0.0F));
  }

  TEST_CASE("Constant-power stereo law is centred at pan 0") {
    const auto [left, right]{App::Audio::constant_power_stereo_gains(0.0F)};
    CHECK_EQ(left, doctest::Approx(0.70710678F));
    CHECK_EQ(right, doctest::Approx(0.70710678F));
  }

  TEST_CASE("Constant-power stereo law reaches full left at pan -1") {
    const auto [left, right]{App::Audio::constant_power_stereo_gains(-1.0F)};
    CHECK_EQ(left, doctest::Approx(1.0F));
    CHECK_EQ(right, doctest::Approx(0.0F));
  }

  TEST_CASE("Constant-power stereo law reaches full right at pan +1") {
    const auto [left, right]{App::Audio::constant_power_stereo_gains(1.0F)};
    CHECK_EQ(left, doctest::Approx(0.0F));
    CHECK_EQ(right, doctest::Approx(1.0F));
  }

  TEST_CASE("Stationary source/listener produce ratio 1") {
    CHECK_EQ(App::Audio::doppler_frequency_ratio(0.0F, 0.0F), doctest::Approx(1.0F));
  }

  TEST_CASE("An approaching source raises pitch") {
    // Positive radial velocity means "toward the other party".
    CHECK_GT(App::Audio::doppler_frequency_ratio(0.0F, 50.0F), 1.0F);
  }

  TEST_CASE("A receding source lowers pitch") {
    CHECK_LT(App::Audio::doppler_frequency_ratio(0.0F, -50.0F), 1.0F);
  }

  TEST_CASE("Doppler ratio is clamped to the SDL3_mixer range") {
    CHECK_GE(App::Audio::doppler_frequency_ratio(0.0F, 5000.0F), App::Audio::k_min_frequency_ratio);
    CHECK_LE(App::Audio::doppler_frequency_ratio(0.0F, -5000.0F),
        App::Audio::k_max_frequency_ratio);
  }

  TEST_CASE("Spatialize returns centred full-gain ratio-1 for coincident positions") {
    const AudioListenerState listener{listener_at(Vec3{0.0F, 0.0F, 0.0F})};
    const SoundEmitterState emitter{emitter_at(Vec3{0.0F, 0.0F, 0.0F})};
    const App::Audio::SpatialResult result{
        App::Audio::spatialize(listener, emitter, -1.0F, 1.0F / 60.0F)};
    CHECK(std::isfinite(result.pan));
    CHECK_EQ(result.attenuation_gain, doctest::Approx(1.0F));
    CHECK_EQ(result.frequency_ratio, doctest::Approx(1.0F));
  }

  TEST_CASE("Spatialize ratio is 1 for a zero/paused delta") {
    const AudioListenerState listener{listener_at(Vec3{0.0F, 0.0F, 0.0F})};
    const SoundEmitterState emitter{emitter_at(Vec3{5.0F, 0.0F, 0.0F})};
    const App::Audio::SpatialResult result{
        App::Audio::spatialize(listener, emitter, -1.0F, 0.0F)};
    CHECK_EQ(result.frequency_ratio, doctest::Approx(1.0F));
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
