#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <vector>

#include "Core/Audio/AudioTypes.hpp"
#include "Core/Audio/VoicePool.hpp"

namespace {

using App::Audio::AudioOwnerToken;
using App::Audio::SoundPlayRequest;
using App::Audio::SoundResourceId;
using App::Audio::VoiceHandle;
using App::Audio::VoicePool;
using App::Audio::VoiceState;

SoundPlayRequest request(const SoundResourceId resource, const AudioOwnerToken owner) {
  SoundPlayRequest req;
  req.resource = resource;
  req.owner = owner;
  return req;
}

}  // namespace

TEST_SUITE("Core::Audio::VoicePool") {
  TEST_CASE("Slots allocate deterministically from 0 upward") {
    VoicePool pool;
    for (std::uint32_t index{0}; index < 16; ++index) {
      const auto handle{pool.allocate()};
      REQUIRE(handle.has_value());
      CHECK_EQ(handle->index, index);
    }
  }

  TEST_CASE("Sixteen simultaneous voices succeed and the 17th fails without stealing") {
    VoicePool pool;
    for (std::uint32_t index{0}; index < 16; ++index) {
      REQUIRE(pool.allocate().has_value());
    }
    const auto seventeenth{pool.allocate()};
    REQUIRE_FALSE(seventeenth.has_value());
    CHECK_EQ(pool.active_count(), 16U);
  }

  TEST_CASE("Release frees a slot and bumps its generation") {
    VoicePool pool;
    const VoiceHandle first{pool.allocate().value()};
    pool.configure(first, request(SoundResourceId{1}, AudioOwnerToken{}));
    pool.release(first);

    CHECK_EQ(pool.free_count(), 16U);
    CHECK_FALSE(pool.generation_matches(first));

    const VoiceHandle reused{pool.allocate().value()};
    CHECK_EQ(reused.index, first.index);
    CHECK_GT(reused.generation, first.generation);
  }

  TEST_CASE("A stale handle cannot reach a newer generation") {
    VoicePool pool;
    const VoiceHandle first{pool.allocate().value()};
    pool.release(first);
    const VoiceHandle second{pool.allocate().value()};

    CHECK(pool.find(first) == nullptr);
    CHECK(pool.find(second) != nullptr);
    // Releasing the stale handle must not free the new generation.
    pool.release(first);
    CHECK(pool.find(second) != nullptr);
  }

  TEST_CASE("Configure resets per-use fields and marks the voice queued") {
    VoicePool pool;
    const VoiceHandle handle{pool.allocate().value()};
    SoundPlayRequest req;
    req.resource = SoundResourceId{3};
    req.loop = true;
    req.raw_flags = 0x08U;
    req.provenance =
        App::Audio::AudioProvenance{.origin = App::Audio::AudioOrigin::k_structured_script,
            .scenario_name = "Grid.SCX",
            .source_script_index = 2U,
            .script_instance_id = 7U,
            .function_id = 0x05000014U};
    pool.configure(handle, req);

    const App::Audio::SoundVoice* voice{pool.find(handle)};
    REQUIRE(voice != nullptr);
    CHECK_EQ(voice->state, VoiceState::k_queued);
    CHECK_EQ(voice->resource.index, 3U);
    CHECK(voice->looping);
    CHECK(voice->unknown_flag);
    CHECK_EQ(voice->attenuation_gain, doctest::Approx(1.0F));
    CHECK_EQ(voice->pan, doctest::Approx(0.0F));
    CHECK_EQ(voice->frequency_ratio, doctest::Approx(1.0F));
    CHECK_EQ(voice->previous_distance, doctest::Approx(-1.0F));
    CHECK_EQ(voice->provenance.origin, App::Audio::AudioOrigin::k_structured_script);
    CHECK_EQ(voice->provenance.scenario_name, "Grid.SCX");
    REQUIRE(voice->provenance.script_instance_id.has_value());
    CHECK_EQ(voice->provenance.script_instance_id.value(), 7U);
  }

  TEST_CASE("find_first_active matches (soundId, owner) and skips free slots") {
    VoicePool pool;
    const VoiceHandle first{pool.allocate().value()};
    pool.configure(first, request(SoundResourceId{1}, AudioOwnerToken{}));
    const VoiceHandle second{pool.allocate().value()};
    pool.configure(second,
        request(SoundResourceId{2},
            AudioOwnerToken{.scenario = reinterpret_cast<const void*>(1), .object_index = 7}));

    const auto match{pool.find_first_active(SoundResourceId{2},
        AudioOwnerToken{.scenario = reinterpret_cast<const void*>(1), .object_index = 7})};
    REQUIRE(match.has_value());
    CHECK_EQ(match->index, second.index);

    CHECK_FALSE(
        pool.find_first_active(SoundResourceId{1},
                AudioOwnerToken{.scenario = reinterpret_cast<const void*>(1), .object_index = 7})
            .has_value());
  }

  TEST_CASE("release_owned_by releases only the matching owner") {
    VoicePool pool;
    const VoiceHandle a{pool.allocate().value()};
    pool.configure(a, request(SoundResourceId{1}, AudioOwnerToken{}));
    const VoiceHandle b{pool.allocate().value()};
    const AudioOwnerToken owner{.scenario = reinterpret_cast<const void*>(2), .object_index = 3};
    pool.configure(b, request(SoundResourceId{2}, owner));

    pool.release_owned_by(owner);
    CHECK(pool.find(a) != nullptr);
    CHECK(pool.find(b) == nullptr);
  }

  TEST_CASE("release_all frees every active voice") {
    VoicePool pool;
    for (std::uint32_t index{0}; index < 5; ++index) {
      const VoiceHandle handle{pool.allocate().value()};
      pool.configure(handle, request(SoundResourceId{1}, AudioOwnerToken{}));
    }
    pool.release_all();
    CHECK_EQ(pool.active_count(), 0U);
    CHECK_EQ(pool.free_count(), 16U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
