#include <doctest/doctest.h>

#include "Core/Interface/RuntimeText.hpp"
#include "Core/WorldPresentation.hpp"

TEST_SUITE("Core::WorldTextState") {
  TEST_CASE("accepts matching world text and expires by recovered raw-byte milliseconds") {
    App::WorldTextState state;
    CHECK(state.apply_command(
        App::WorldTextCommand{.scene_id = 5,
            .scene_generation = 9,
            .document = App::Interface::parse_runtime_text("{fD}short"),
            .provenance = App::WorldTextProvenance{.source_kind = App::TextSourceKind::k_iam_object,
                .object_id = 141,
                .audio_resource = "VOICEOFF/IMPASSE.ADP",
                .role = App::TextPresentationRole::k_unknown,
                .modernization_policy = App::TextModernizationPolicy::k_faithful_only},
            .duration_ms = 2000U},
        5U,
        9U));
    REQUIRE(state.active());
    REQUIRE(state.document() != nullptr);
    CHECK_EQ(state.document()->authored_bytes(), "{fD}short");
    CHECK_EQ(App::Interface::runtime_text_plain_bytes(*state.document()), "short");
    CHECK_EQ(state.provenance().object_id, 141);
    CHECK_EQ(state.provenance().role, App::TextPresentationRole::k_unknown);
    state.update(1.5F);
    CHECK(state.active());
    CHECK(state.remaining_seconds() == doctest::Approx(0.5F));
    CHECK_EQ(state.presentation_time_ms(), 1500U);
    state.update(0.5F);
    CHECK_FALSE(state.active());
  }

  TEST_CASE("does not accept stale world text") {
    App::WorldTextState state;
    CHECK_FALSE(state.apply_command(App::WorldTextCommand{.scene_id = 4,
                                        .scene_generation = 9,
                                        .document = App::Interface::parse_runtime_text("stale"),
                                        .provenance = {},
                                        .duration_ms = 2400U},
        5U,
        9U));
    CHECK_FALSE(state.active());
  }
}