#include <doctest/doctest.h>

// NOLINTBEGIN(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Omikron/SCX.hpp"
#include "Core/Script/ScriptRuntime.hpp"
#include "Core/Sprite/SpriteInstance.hpp"

namespace {

constexpr std::uint32_t K_SELECT_CAMERA{0x01000001U};
constexpr std::uint32_t K_INTERPOLATE_CAMERAS{0x01000002U};
constexpr std::uint32_t K_SET_SPRITE_TYPE{0x0400000CU};
constexpr std::uint32_t K_SELECT_BODY_ANIMATION{0x02000004U};
constexpr std::uint32_t K_SELECT_RELATIVE_BODY_ANIMATION{0x0200002AU};
constexpr std::uint32_t K_MOVE_OBJECT_ON_PATH{0x03000008U};
constexpr std::uint32_t K_SCALE_X{0x0400001BU};
constexpr std::uint32_t K_SCALE_Y{0x0400001CU};
constexpr std::uint32_t K_ROLL{0x0400001DU};
constexpr std::uint32_t K_UNKNOWN_20{0x04000020U};
constexpr std::uint32_t K_DISPLAY_3D{0x04000028U};
constexpr std::uint32_t K_SET_FRAME{0x04000029U};
constexpr std::uint32_t K_PLAY_SOUND{0x05000014U};
constexpr std::uint32_t K_PLAY_SYNC_SOUND{0x05000015U};
constexpr std::uint32_t K_STOP_SOUND{0x05000016U};
constexpr std::uint32_t K_WAIT{0x06000017U};

/// Synthetic sprite handle for the fake world.
constexpr App::Sprite::SpriteHandle K_HANDLE{100, 1};

/// Test double for the sprite world service: records every call.
class FakeWorld final : public App::Script::ScriptWorld {
 public:
  std::vector<std::uint16_t> frame_requests;
  std::vector<std::uint16_t> sprite_types;
  std::vector<std::array<float, 3>> positions;
  std::vector<float> scale_x;
  std::vector<float> scale_y;
  std::vector<float> rotations;
  std::vector<std::string> selected_cameras;
  std::vector<App::Script::CameraInterpolationRequest> camera_interpolation_requests;
  bool fail_camera{false};
  std::array<float, 3> fallback_position{1.0F, 2.0F, 3.0F};
  bool fail_ensure{false};
  bool fail_frame{false};
  bool fail_position{false};
  bool fail_sound{false};
  bool fail_owner{false};
  bool fail_owner_position{false};
  bool fail_play{false};
  App::Audio::SoundResourceId sound_resource{7};
  App::Audio::SoundDescriptor sound_descriptor{
      .resource = App::Audio::SoundResourceId{7}, .name = "fx", .h_id = 0, .loaded = true};
  std::vector<App::Audio::SoundPlayRequest> play_requests;
  std::vector<std::pair<App::Audio::SoundResourceId, App::Audio::AudioOwnerToken>> stop_requests;
  std::vector<App::Script::BodyAnimationRequest> select_body_animation_requests;
  std::vector<App::Script::RelativeBodyAnimationRequest> body_animation_requests;
  std::vector<std::int16_t> body_animation_resets;
  std::uint32_t body_animation_max_frame{3};
  bool fail_body_animation{false};
  App::Script::BodyAnimationApplyError body_animation_failure{
      App::Script::BodyAnimationApplyError::k_missing_animation};
  std::vector<App::Script::MoveObjectOnPathRequest> move_object_on_path_requests;
  std::uint32_t move_object_on_path_maximum{10};
  std::optional<std::array<float, 3>> move_object_on_path_captured_rebase_translation;
  std::optional<App::Script::MoveObjectOnPathFailure> move_object_on_path_failure;

  std::expected<App::Sprite::SpriteHandle, std::string> ensure_sprite(
      const std::uint32_t /*source*/) override {
    if (fail_ensure) {
      return std::expected<App::Sprite::SpriteHandle, std::string>{std::unexpect, "no such sprite"};
    }
    return K_HANDLE;
  }
  std::expected<void, std::string> set_sprite_frame(
      App::Sprite::SpriteHandle /*handle*/, const std::uint16_t frame_index) override {
    if (fail_frame) {
      return std::expected<void, std::string>{std::unexpect, "bad frame"};
    }
    frame_requests.push_back(frame_index);
    return {};
  }
  void set_sprite_position(
      App::Sprite::SpriteHandle /*handle*/, const std::array<float, 3> position) override {
    positions.push_back(position);
  }
  void set_sprite_type(App::Sprite::SpriteHandle /*handle*/, const std::uint16_t type) override {
    sprite_types.push_back(type);
  }
  void set_sprite_scale_x(App::Sprite::SpriteHandle /*handle*/, const float value) override {
    scale_x.push_back(value);
  }
  void set_sprite_scale_y(App::Sprite::SpriteHandle /*handle*/, const float value) override {
    scale_y.push_back(value);
  }
  void set_sprite_rotation(App::Sprite::SpriteHandle /*handle*/, const float value) override {
    rotations.push_back(value);
  }
  std::expected<std::array<float, 3>, std::string> resolve_position(
      const std::uint32_t /*xyz*/) override {
    if (fail_position) {
      return std::expected<std::array<float, 3>, std::string>{std::unexpect, "no xyz"};
    }
    return fallback_position;
  }

  std::expected<App::Audio::SoundDescriptor, std::string> resolve_sound(
      const std::uint32_t /*index*/) override {
    if (fail_sound) {
      return std::expected<App::Audio::SoundDescriptor, std::string>{std::unexpect, "no sound"};
    }
    return sound_descriptor;
  }
  std::expected<App::Audio::AudioOwnerToken, std::string> resolve_audio_owner(
      const std::int32_t object_index) override {
    if (fail_owner) {
      return std::expected<App::Audio::AudioOwnerToken, std::string>{std::unexpect, "no object"};
    }
    if (object_index == -1) {
      return App::Audio::AudioOwnerToken{};
    }
    return App::Audio::AudioOwnerToken{.scenario = this,
        .object_index = static_cast<std::uint32_t>(object_index),
        .generation = 1};
  }
  std::expected<App::Audio::Vec3, std::string> resolve_owner_position(
      const App::Audio::AudioOwnerToken& /*owner*/) override {
    if (fail_owner_position) {
      return std::expected<App::Audio::Vec3, std::string>{std::unexpect, "no position"};
    }
    return App::Audio::Vec3{1.0F, 2.0F, 3.0F};
  }
  std::expected<App::Audio::VoiceHandle, std::string> play_sound(
      const App::Audio::SoundPlayRequest& request) override {
    play_requests.push_back(request);
    if (fail_play) {
      return std::expected<App::Audio::VoiceHandle, std::string>{std::unexpect, "pool full"};
    }
    return App::Audio::VoiceHandle{.index = 3, .generation = 1};
  }
  void stop_sound(
      const App::Audio::SoundResourceId sound, const App::Audio::AudioOwnerToken& owner) override {
    stop_requests.push_back({sound, owner});
  }
  App::Audio::AudioContextInfo audio_context() const override {
    return {};
  }
  std::expected<void, std::string> select_camera(const std::string_view camera_name) override {
    selected_cameras.emplace_back(camera_name);
    if (fail_camera) {
      return std::expected<void, std::string>{std::unexpect, "no camera"};
    }
    return {};
  }
  std::expected<void, std::string> interpolate_cameras(
      const App::Script::CameraInterpolationRequest& request) override {
    camera_interpolation_requests.push_back(request);
    if (fail_camera) {
      return std::expected<void, std::string>{std::unexpect, "no camera"};
    }
    return {};
  }
  std::expected<App::Script::BodyAnimationResult, App::Script::BodyAnimationFailure>
  select_body_animation(const App::Script::BodyAnimationRequest& request) override {
    select_body_animation_requests.push_back(request);
    if (fail_body_animation) {
      return std::expected<App::Script::BodyAnimationResult, App::Script::BodyAnimationFailure>{
          std::unexpect,
          App::Script::BodyAnimationFailure{
              .error = body_animation_failure, .reason_text = "body animation failed"}};
    }
    return App::Script::BodyAnimationResult{.max_frame_index = body_animation_max_frame};
  }
  std::expected<App::Script::RelativeBodyAnimationResult, App::Script::RelativeBodyAnimationFailure>
  select_relative_body_animation(
      const App::Script::RelativeBodyAnimationRequest& request) override {
    body_animation_requests.push_back(request);
    if (fail_body_animation) {
      return std::expected<App::Script::RelativeBodyAnimationResult,
          App::Script::RelativeBodyAnimationFailure>{std::unexpect,
          App::Script::RelativeBodyAnimationFailure{
              .error = body_animation_failure, .reason_text = "body animation failed"}};
    }
    return App::Script::RelativeBodyAnimationResult{.max_frame_index = body_animation_max_frame};
  }
  void reset_body_animation(const std::int16_t character_id) override {
    body_animation_resets.push_back(character_id);
  }
  std::expected<App::Script::MoveObjectOnPathResult, App::Script::MoveObjectOnPathFailure>
  move_object_on_path_max_parameter(const std::uint32_t /*path_descriptor_index*/,
      const std::uint32_t /*subpath_index*/) override {
    if (move_object_on_path_failure.has_value()) {
      return std::expected<App::Script::MoveObjectOnPathResult,
          App::Script::MoveObjectOnPathFailure>{std::unexpect, move_object_on_path_failure.value()};
    }
    return App::Script::MoveObjectOnPathResult{
        .max_parameter = move_object_on_path_maximum, .captured_rebase_translation = std::nullopt};
  }
  std::expected<App::Script::MoveObjectOnPathResult, App::Script::MoveObjectOnPathFailure>
  move_object_on_path(const App::Script::MoveObjectOnPathRequest& request) override {
    move_object_on_path_requests.push_back(request);
    if (move_object_on_path_failure.has_value()) {
      return std::expected<App::Script::MoveObjectOnPathResult,
          App::Script::MoveObjectOnPathFailure>{std::unexpect, move_object_on_path_failure.value()};
    }
    return App::Script::MoveObjectOnPathResult{.max_parameter = move_object_on_path_maximum,
        .captured_rebase_translation = request.capture_rebase_translation
                                           ? move_object_on_path_captured_rebase_translation
                                           : std::optional<std::array<float, 3>>{std::nullopt}};
  }
  std::string_view scenario_name() const override {
    return "test";
  }
};

/// Builds one parsed script command.
App::Omikron::ScxScriptCommand command(const std::uint32_t opcode,
    const std::uint32_t first_value_index,
    const std::uint32_t value_count,
    const std::optional<std::uint32_t> next = std::nullopt,
    const std::uint32_t execution_limit = 1) {
  return App::Omikron::ScxScriptCommand{.opcode = opcode,
      .value_count = value_count,
      .first_value_index = first_value_index,
      .next_linked_command_index = next,
      .execution_limit = execution_limit,
      .initial_execution_count = 0,
      .file_offset = 0};
}

/// Builds a runtime plus one instance of a single-command script, with a
/// value pool of `values` words.
struct RuntimeFixture {
  App::Omikron::ScxData scx;
  FakeWorld world;
  std::unique_ptr<App::Script::ScriptRuntime> runtime;

  RuntimeFixture(
      std::vector<App::Omikron::ScxScript> scripts, std::vector<App::Omikron::ScriptValue> values) {
    scx.scripts = std::move(scripts);
    scx.shared_values = std::move(values);
    runtime = std::move(App::Script::ScriptRuntime::create(scx, world, "test").value());
  }
};

/// Builds a script with a single root command (no linked chain).
App::Omikron::ScxScript single_root_script(const App::Omikron::ScxScriptCommand& root) {
  App::Omikron::ScxScript script;
  script.name = "test";
  script.root_command_count = 1;
  script.linked_command_count = 0;
  script.repeat_limit = 1;
  script.root_commands.push_back(root);
  return script;
}

App::Omikron::ScxScript relative_body_animation_script(const std::uint32_t execution_limit = 1) {
  App::Omikron::ScxScript script{single_root_script(
      command(K_SELECT_RELATIVE_BODY_ANIMATION, 0, 12, std::nullopt, execution_limit))};
  script.binding_table_a.entries.push_back(App::Omikron::ScxBindingEntry{.name = "RootBody"});
  return script;
}

App::Omikron::ScxScript body_animation_script(const std::uint32_t execution_limit = 1) {
  App::Omikron::ScxScript script{
      single_root_script(command(K_SELECT_BODY_ANIMATION, 0, 10, std::nullopt, execution_limit))};
  script.binding_table_a.entries.push_back(App::Omikron::ScxBindingEntry{.name = "RootBody"});
  return script;
}

App::Omikron::ScxScript select_camera_script() {
  App::Omikron::ScxScript script{single_root_script(command(K_SELECT_CAMERA, 0, 1))};
  script.binding_table_a.entries.push_back(App::Omikron::ScxBindingEntry{.name = "WRONG_TABLE"});
  script.binding_table_b.entries.push_back(App::Omikron::ScxBindingEntry{.name = "CLOSEUP"});
  return script;
}

App::Omikron::ScxScript interpolate_cameras_script() {
  App::Omikron::ScxScript script{single_root_script(command(K_INTERPOLATE_CAMERAS, 0, 4))};
  script.binding_table_b.entries.push_back(App::Omikron::ScxBindingEntry{.name = "CAM_A"});
  script.binding_table_b.entries.push_back(App::Omikron::ScxBindingEntry{.name = "CAM_B"});
  return script;
}

App::Omikron::ScxScript move_object_on_path_script(const std::uint32_t execution_limit = 1) {
  App::Omikron::ScxScript script{
      single_root_script(command(K_MOVE_OBJECT_ON_PATH, 0, 15, std::nullopt, execution_limit))};
  script.binding_table_a.entries.push_back(App::Omikron::ScxBindingEntry{.name = "Crate"});
  return script;
}

std::vector<App::Omikron::ScriptValue> move_object_on_path_values(
    const std::uint32_t direction, const std::uint32_t transform_rebase_mode = 0U) {
  std::vector<App::Omikron::ScriptValue> values(15);
  values.at(0).set_unsigned(0);
  values.at(1).set_unsigned(1);
  values.at(2).set_unsigned(2);
  values.at(3).set_unsigned(1);
  values.at(4).set_unsigned(direction);
  values.at(5).set_unsigned(transform_rebase_mode);
  values.at(6).set_float(20.0F);
  const float authored_progress{direction == 0U ? 99.0F : 0.0F};
  values.at(7).set_float(authored_progress);
  values.at(8).set_float(authored_progress);
  return values;
}

std::vector<App::Omikron::ScriptValue> relative_body_animation_values() {
  std::vector<App::Omikron::ScriptValue> values(12);
  values.at(0).set_unsigned(0);
  values.at(1).set_unsigned(2);
  values.at(2).set_float(99.0F);
  values.at(3).set_float(77.0F);
  values.at(4).set_float(4.0F);
  values.at(5).set_float(5.0F);
  values.at(6).set_float(6.0F);
  values.at(7).set_unsigned(7);
  values.at(8).set_unsigned(8);
  values.at(9).set_float(9.0F);
  values.at(10).set_float(10.0F);
  values.at(11).set_float(11.0F);
  return values;
}

std::vector<App::Omikron::ScriptValue> body_animation_values() {
  std::vector<App::Omikron::ScriptValue> values(10);
  values.at(0).set_unsigned(0);
  values.at(1).set_unsigned(2);
  values.at(2).set_float(99.0F);
  values.at(3).set_float(77.0F);
  values.at(4).set_float(4.0F);
  values.at(5).set_float(5.0F);
  values.at(6).set_float(6.0F);
  values.at(7).set_float(9.0F);
  values.at(8).set_float(10.0F);
  values.at(9).set_float(11.0F);
  return values;
}

/// Fills `values` with `count` zero words, then overwrites from `words`.
std::vector<App::Omikron::ScriptValue> make_values(
    const std::size_t count, const std::vector<std::uint32_t> words = {}) {
  std::vector<App::Omikron::ScriptValue> values(count);
  for (std::size_t index{0}; index < words.size() && index < count; ++index) {
    values.at(index).raw = words.at(index);
  }
  return values;
}

}  // namespace

TEST_SUITE("Core::Script::ScriptRuntime") {
  TEST_CASE("Opcode registry resolves confirmed opcodes") {
    REQUIRE_NE(App::Script::opcode_info(K_SELECT_CAMERA), nullptr);
    CHECK_EQ(std::string{App::Script::opcode_name(K_SELECT_CAMERA)}, "SelectCamera");
    CHECK(App::Script::opcode_info(K_SELECT_CAMERA)->support ==
          App::Script::OpcodeSupport::k_supported);
    REQUIRE_NE(App::Script::opcode_info(K_INTERPOLATE_CAMERAS), nullptr);
    CHECK_EQ(
        std::string{App::Script::opcode_name(K_INTERPOLATE_CAMERAS)}, "Script_InterpolateCameras");
    CHECK(App::Script::opcode_info(K_INTERPOLATE_CAMERAS)->support ==
          App::Script::OpcodeSupport::k_supported);
    REQUIRE_NE(App::Script::opcode_info(K_SELECT_BODY_ANIMATION), nullptr);
    CHECK_EQ(std::string{App::Script::opcode_name(K_SELECT_BODY_ANIMATION)},
        "Script_SelectBodyAnimation");
    CHECK(App::Script::opcode_info(K_SELECT_BODY_ANIMATION)->support ==
          App::Script::OpcodeSupport::k_supported);
    REQUIRE_NE(App::Script::opcode_info(K_SELECT_RELATIVE_BODY_ANIMATION), nullptr);
    CHECK_EQ(std::string{App::Script::opcode_name(K_SELECT_RELATIVE_BODY_ANIMATION)},
        "Script_SelectRelativeBodyAnimation");
    CHECK(App::Script::opcode_info(K_SELECT_RELATIVE_BODY_ANIMATION)->support ==
          App::Script::OpcodeSupport::k_supported);
    CHECK_NE(App::Script::opcode_info(K_SET_SPRITE_TYPE), nullptr);
    CHECK_EQ(std::string{App::Script::opcode_name(K_SET_SPRITE_TYPE)}, "SetSpriteType");
    CHECK_NE(App::Script::opcode_info(K_SET_FRAME), nullptr);
    CHECK_EQ(std::string{App::Script::opcode_name(K_SET_FRAME)}, "SetSpriteFrame");
    CHECK_EQ(std::string{App::Script::opcode_name(K_SCALE_X)}, "ScaleSpriteOnX");
    CHECK_EQ(std::string{App::Script::opcode_name(K_SCALE_Y)}, "ScaleSpriteOnY");
    CHECK_EQ(std::string{App::Script::opcode_name(K_ROLL)}, "SetSpriteRolling");
    CHECK_EQ(std::string{App::Script::opcode_name(K_DISPLAY_3D)}, "Display3DSprite");
    CHECK(App::Script::opcode_owns_sprite(K_DISPLAY_3D));
    CHECK_FALSE(App::Script::opcode_owns_sprite(K_SET_FRAME));
    CHECK(App::Script::opcode_info(0x99999999U) == nullptr);
    CHECK_EQ(std::string{App::Script::opcode_name(K_PLAY_SOUND)}, "PlaySound");
    CHECK_EQ(std::string{App::Script::opcode_name(K_PLAY_SYNC_SOUND)}, "PlaySyncSound");
    CHECK_EQ(std::string{App::Script::opcode_name(K_STOP_SOUND)}, "StopSound");
    CHECK_FALSE(App::Script::opcode_owns_sprite(K_PLAY_SOUND));
    CHECK_EQ(std::string{App::Script::opcode_name(K_WAIT)}, "Wait");
    CHECK_FALSE(App::Script::opcode_owns_sprite(K_WAIT));
  }

  TEST_CASE("SelectCamera resolves its camera name through binding table B") {
    RuntimeFixture fixture{{select_camera_script()}, make_values(1, {0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.selected_cameras.size(), std::size_t{1});
    CHECK_EQ(fixture.world.selected_cameras.front(), "CLOSEUP");
    CHECK(fixture.runtime->instances().front().completed);
  }

  TEST_CASE("InterpolateCameras reinitializes elapsed and uses native remaining-time fractions") {
    std::vector<App::Omikron::ScriptValue> values(4);
    values.at(0).set_unsigned(0);
    values.at(1).set_unsigned(1);
    values.at(2).set_float(4.0F);
    values.at(3).set_float(99.0F);
    RuntimeFixture fixture{{interpolate_cameras_script()}, std::move(values)};
    const std::size_t id{fixture.runtime->create_instance(0).value()};

    REQUIRE(fixture.runtime->instance(id) != nullptr);
    CHECK(fixture.runtime->instance(id)->value_pool.at(3).as_float() == doctest::Approx(0.0F));

    fixture.runtime->step_tick(1.0F);
    REQUIRE_EQ(fixture.world.camera_interpolation_requests.size(), std::size_t{1});
    const App::Script::CameraInterpolationRequest& first{
        fixture.world.camera_interpolation_requests.at(0)};
    CHECK_EQ(first.camera_a, "CAM_A");
    CHECK_EQ(first.camera_b, "CAM_B");
    CHECK(first.fraction == doctest::Approx(0.25F));
    CHECK_FALSE(first.snap_to_target);
    CHECK(fixture.runtime->instance(id)->value_pool.at(3).as_float() == doctest::Approx(1.0F));
    CHECK_FALSE(fixture.runtime->instance(id)->completed);

    // The public fixed-step helper accepts native script frames directly, so
    // the remaining three frames complete this four-frame interpolation in
    // one scheduler update. Runtime first performs the normal update and then
    // snaps A exactly to B on the endpoint crossing.
    fixture.runtime->step_tick(3.0F);
    REQUIRE_EQ(fixture.world.camera_interpolation_requests.size(), std::size_t{3});
    const auto& crossing{fixture.world.camera_interpolation_requests.at(1)};
    CHECK(crossing.fraction == doctest::Approx(1.0F));
    CHECK_FALSE(crossing.snap_to_target);
    const auto& snap{fixture.world.camera_interpolation_requests.at(2)};
    CHECK(snap.snap_to_target);
    CHECK(fixture.runtime->instances().front().completed);
    CHECK_EQ(fixture.runtime->instances().front().root_commands.front().execution_count, 1U);

    REQUIRE(fixture.runtime->reset_instance(id).has_value());
    CHECK(fixture.runtime->instance(id)->value_pool.at(3).as_float() == doctest::Approx(0.0F));
  }

  TEST_CASE("SelectBodyAnimation preserves its ten-slot ABI and progresses in script frames") {
    RuntimeFixture fixture{{body_animation_script()}, body_animation_values()};
    const std::size_t id{fixture.runtime
            ->create_instance(
                0, App::Script::ScriptLaunchContext{.character_id = 310, .parameter = 0})
            .value()};
    const App::Script::ScriptInstance* initial{fixture.runtime->instance(id)};
    REQUIRE(initial != nullptr);
    CHECK_EQ(initial->value_pool.at(2).as_float(), doctest::Approx(0.0F));
    CHECK_EQ(initial->value_pool.at(3).as_float(), doctest::Approx(1.0F));
    CHECK_EQ(initial->value_pool.at(9).as_float(), doctest::Approx(11.0F));

    fixture.runtime->step_tick(1.0F);
    REQUIRE_EQ(fixture.world.select_body_animation_requests.size(), std::size_t{1});
    const App::Script::BodyAnimationRequest& first{
        fixture.world.select_body_animation_requests.at(0)};
    CHECK_EQ(first.character_id, 310);
    CHECK_EQ(first.object_binding, "RootBody");
    CHECK_EQ(first.animation_index, 2U);
    CHECK_EQ(first.previous_progress, doctest::Approx(0.0F));
    CHECK_EQ(first.current_progress, doctest::Approx(1.0F));
    CHECK(first.first_tick);
    CHECK_EQ(first.body_animation_vector.at(0), doctest::Approx(4.0F));
    CHECK_EQ(first.body_animation_vector.at(2), doctest::Approx(6.0F));
    CHECK_EQ(first.authored_offset.at(0), doctest::Approx(9.0F));
    CHECK_EQ(first.authored_offset.at(2), doctest::Approx(11.0F));
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(2).as_float(), doctest::Approx(1.0F));
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(3).as_float(), doctest::Approx(2.0F));

    fixture.runtime->step_tick(1.0F);
    fixture.runtime->step_tick(1.0F);
    REQUIRE_EQ(fixture.world.select_body_animation_requests.size(), std::size_t{3});
    CHECK_EQ(
        fixture.world.select_body_animation_requests.at(2).current_progress, doctest::Approx(3.0F));
    CHECK_EQ(fixture.runtime->instances().at(0).root_commands.at(0).execution_count, 1U);
    CHECK(fixture.runtime->instances().at(0).completed);

    REQUIRE(fixture.runtime->reset_instance(id).has_value());
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(2).as_float(), doctest::Approx(0.0F));
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(3).as_float(), doctest::Approx(1.0F));
    REQUIRE_EQ(fixture.world.body_animation_resets.size(), std::size_t{1});
    CHECK_EQ(fixture.world.body_animation_resets.at(0), 310);
  }

  TEST_CASE("SelectBodyAnimation wraps executions and keeps absent bodies unresolved") {
    RuntimeFixture fixture{{body_animation_script(2)}, body_animation_values()};
    REQUIRE(fixture.runtime
            ->create_instance(
                0, App::Script::ScriptLaunchContext{.character_id = 310, .parameter = 0})
            .has_value());
    fixture.world.body_animation_max_frame = 1;

    fixture.runtime->step_tick(1.0F);
    const App::Script::ScriptInstance& wrapped{fixture.runtime->instances().at(0)};
    CHECK_EQ(wrapped.root_commands.at(0).execution_count, 1U);
    CHECK_EQ(wrapped.value_pool.at(2).as_float(), doctest::Approx(0.0F));
    CHECK_EQ(wrapped.value_pool.at(3).as_float(), doctest::Approx(1.0F));
    CHECK_FALSE(wrapped.completed);

    fixture.world.fail_body_animation = true;
    fixture.world.body_animation_failure =
        App::Script::BodyAnimationApplyError::k_character_unavailable;
    fixture.runtime->step_tick(1.0F);
    const App::Script::ScriptInstance& unavailable{fixture.runtime->instances().at(0)};
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_running);
    CHECK_FALSE(unavailable.paused);
    CHECK_FALSE(unavailable.completed);

    fixture.world.body_animation_failure = App::Script::BodyAnimationApplyError::k_invalid_binding;
    fixture.runtime->step_tick(1.0F);
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(
        fixture.runtime->pause_info().reason, App::Script::ScriptPauseReason::k_missing_resource);
  }

  TEST_CASE("SelectBodyAnimation requires a bound character and all ten value slots") {
    RuntimeFixture missing_character{{body_animation_script()}, body_animation_values()};
    REQUIRE(missing_character.runtime->create_instance(0).has_value());
    missing_character.runtime->step_tick(1.0F);
    CHECK_EQ(
        missing_character.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(missing_character.runtime->pause_info().reason_text,
        "Script_SelectBodyAnimation requires a character-bound instance");

    App::Omikron::ScxScript malformed{body_animation_script()};
    malformed.root_commands.at(0).value_count = 9;
    RuntimeFixture short_abi{{std::move(malformed)}, make_values(10)};
    REQUIRE(short_abi.runtime
            ->create_instance(
                0, App::Script::ScriptLaunchContext{.character_id = 310, .parameter = 0})
            .has_value());
    short_abi.runtime->step_tick(1.0F);
    CHECK_EQ(short_abi.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(short_abi.runtime->pause_info().reason,
        App::Script::ScriptPauseReason::k_invalid_argument_count);
  }

  TEST_CASE("SelectRelativeBodyAnimation reinitializes only mutable progress arguments") {
    RuntimeFixture fixture{{relative_body_animation_script()}, relative_body_animation_values()};
    const std::size_t id{fixture.runtime
            ->create_instance(
                0, App::Script::ScriptLaunchContext{.character_id = 310, .parameter = 0})
            .value()};
    const App::Script::ScriptInstance* instance{fixture.runtime->instance(id)};
    REQUIRE(instance != nullptr);
    CHECK_EQ(instance->value_pool.at(2).as_float(), doctest::Approx(0.0F));
    CHECK_EQ(instance->value_pool.at(3).as_float(), doctest::Approx(1.0F));
    CHECK_EQ(instance->value_pool.at(4).as_float(), doctest::Approx(4.0F));
    CHECK_EQ(instance->value_pool.at(11).as_float(), doctest::Approx(11.0F));

    REQUIRE(fixture.runtime->reset_instance(id).has_value());
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(2).as_float(), doctest::Approx(0.0F));
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(3).as_float(), doctest::Approx(1.0F));
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(9).as_float(), doctest::Approx(9.0F));
    REQUIRE_EQ(fixture.world.body_animation_resets.size(), std::size_t{1});
    CHECK_EQ(fixture.world.body_animation_resets.at(0), 310);
  }

  TEST_CASE(
      "SelectRelativeBodyAnimation advances exact progress windows and completes at endpoint") {
    RuntimeFixture fixture{{relative_body_animation_script()}, relative_body_animation_values()};
    REQUIRE(fixture.runtime
            ->create_instance(
                0, App::Script::ScriptLaunchContext{.character_id = 310, .parameter = 0})
            .has_value());

    fixture.runtime->step_tick(1.0F);
    fixture.runtime->step_tick(0.5F);
    fixture.runtime->step_tick(0.5F);
    fixture.runtime->step_tick(0.5F);

    REQUIRE_EQ(fixture.world.body_animation_requests.size(), std::size_t{4});
    const auto& first{fixture.world.body_animation_requests.at(0)};
    CHECK_EQ(first.character_id, 310);
    CHECK_EQ(first.object_binding, "RootBody");
    CHECK_EQ(first.animation_index, 2U);
    CHECK_EQ(first.previous_progress, doctest::Approx(0.0F));
    CHECK_EQ(first.current_progress, doctest::Approx(1.0F));
    CHECK(first.first_tick);
    CHECK_EQ(first.path_index, 7U);
    CHECK_EQ(first.subpath_index, 8U);
    CHECK_EQ(first.body_animation_vector.at(2), doctest::Approx(6.0F));
    CHECK_EQ(first.authored_offset.at(0), doctest::Approx(9.0F));
    const auto& second{fixture.world.body_animation_requests.at(1)};
    CHECK_EQ(second.previous_progress, doctest::Approx(1.0F));
    CHECK_EQ(second.current_progress, doctest::Approx(2.0F));
    const auto& final{fixture.world.body_animation_requests.back()};
    CHECK_EQ(final.previous_progress, doctest::Approx(2.5F));
    CHECK_EQ(final.current_progress, doctest::Approx(3.0F));
    CHECK_EQ(fixture.runtime->instances().at(0).root_commands.at(0).execution_count, 1U);
    CHECK(fixture.runtime->instances().at(0).completed);
  }

  TEST_CASE(
      "SelectRelativeBodyAnimation remains running when its body is deliberately unavailable") {
    RuntimeFixture fixture{
        {relative_body_animation_script(0xFFFFFFFFU)}, relative_body_animation_values()};
    REQUIRE(fixture.runtime
            ->create_instance(
                0, App::Script::ScriptLaunchContext{.character_id = 310, .parameter = 0})
            .has_value());

    fixture.runtime->step_tick(1.0F);
    REQUIRE_EQ(fixture.world.body_animation_requests.size(), 1U);

    fixture.world.fail_body_animation = true;
    fixture.world.body_animation_failure =
        App::Script::BodyAnimationApplyError::k_character_unavailable;
    fixture.runtime->step_tick(1.0F);
    fixture.runtime->step_tick(1.0F);

    const App::Script::ScriptInstance& instance{fixture.runtime->instances().at(0)};
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_running);
    CHECK_FALSE(instance.paused);
    CHECK_FALSE(instance.completed);
    CHECK_EQ(fixture.world.body_animation_requests.size(), 3U);
  }

  TEST_CASE("SelectRelativeBodyAnimation still pauses on a malformed resource failure") {
    RuntimeFixture fixture{{relative_body_animation_script()}, relative_body_animation_values()};
    fixture.world.fail_body_animation = true;
    REQUIRE(fixture.runtime
            ->create_instance(
                0, App::Script::ScriptLaunchContext{.character_id = 310, .parameter = 0})
            .has_value());

    fixture.runtime->step_tick(1.0F);

    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(
        fixture.runtime->pause_info().reason, App::Script::ScriptPauseReason::k_missing_resource);
  }

  TEST_CASE("MoveObjectOnPath preserves all 15 arguments and blocks through its endpoint") {
    RuntimeFixture fixture{{move_object_on_path_script()}, move_object_on_path_values(0)};
    const std::size_t id{fixture.runtime->create_instance(0).value()};
    const App::Script::ScriptInstance* initial{fixture.runtime->instance(id)};
    REQUIRE(initial != nullptr);
    CHECK_EQ(initial->value_pool.at(7).as_float(), doctest::Approx(0.0F));
    CHECK_EQ(initial->value_pool.at(8).as_float(), doctest::Approx(0.0F));

    fixture.runtime->step_tick(1.0F);
    REQUIRE_EQ(fixture.world.move_object_on_path_requests.size(), 1U);
    const auto& first{fixture.world.move_object_on_path_requests.at(0)};
    CHECK_EQ(first.object_binding, "Crate");
    CHECK_EQ(first.path_descriptor_index, 1U);
    CHECK_EQ(first.subpath_index, 2U);
    CHECK_EQ(first.interpolation_mode, 1U);
    CHECK_EQ(first.direction, 0U);
    CHECK_EQ(first.transform_rebase_mode, 0U);
    CHECK_EQ(first.duration_frames, doctest::Approx(20.0F));
    CHECK_EQ(first.current_parameter, doctest::Approx(0.0F));
    CHECK_FALSE(first.capture_rebase_translation);
    CHECK_EQ(first.rebase_translation.at(0), doctest::Approx(0.0F));
    CHECK_EQ(first.rotation_offset.at(0), doctest::Approx(0.0F));
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(8).as_float(), doctest::Approx(0.0F));
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(7).as_float(), doctest::Approx(0.5F));

    fixture.runtime->step_tick(19.0F);
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(7).as_float(), doctest::Approx(10.0F));
    CHECK_FALSE(fixture.runtime->instance(id)->completed);
    fixture.runtime->step_tick(0.0F);
    CHECK(fixture.runtime->instance(id)->completed);
    CHECK_EQ(fixture.runtime->instances().at(0).root_commands.at(0).execution_count, 1U);
  }

  TEST_CASE("MoveObjectOnPath captures and persists a forward rebase translation") {
    RuntimeFixture fixture{{move_object_on_path_script()}, move_object_on_path_values(0, 1)};
    fixture.world.move_object_on_path_captured_rebase_translation =
        std::array<float, 3>{100.0F, 200.0F, 300.0F};
    const std::size_t id{fixture.runtime->create_instance(0).value()};

    fixture.runtime->step_tick(1.0F);
    REQUIRE_EQ(fixture.world.move_object_on_path_requests.size(), 1U);
    const auto& first{fixture.world.move_object_on_path_requests.at(0)};
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_running);
    CHECK_EQ(first.transform_rebase_mode, 1U);
    CHECK(first.capture_rebase_translation);
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(9).as_float(), doctest::Approx(100.0F));
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(10).as_float(), doctest::Approx(200.0F));
    CHECK_EQ(fixture.runtime->instance(id)->value_pool.at(11).as_float(), doctest::Approx(300.0F));

    fixture.runtime->step_tick(1.0F);
    REQUIRE_EQ(fixture.world.move_object_on_path_requests.size(), 2U);
    const auto& second{fixture.world.move_object_on_path_requests.at(1)};
    CHECK_FALSE(second.capture_rebase_translation);
    CHECK_EQ(second.rebase_translation.at(0), doctest::Approx(100.0F));
    CHECK_EQ(second.rebase_translation.at(1), doctest::Approx(200.0F));
    CHECK_EQ(second.rebase_translation.at(2), doctest::Approx(300.0F));
  }

  TEST_CASE("MoveObjectOnPath reverse rebase starts at maximum and captures there") {
    RuntimeFixture fixture{{move_object_on_path_script()}, move_object_on_path_values(1, 1)};
    fixture.world.move_object_on_path_captured_rebase_translation =
        std::array<float, 3>{10.0F, 20.0F, 30.0F};
    REQUIRE(fixture.runtime->create_instance(0).has_value());
    fixture.runtime->step_tick(1.0F);
    REQUIRE_EQ(fixture.world.move_object_on_path_requests.size(), 1U);
    const auto& first{fixture.world.move_object_on_path_requests.at(0)};
    CHECK_EQ(first.current_parameter, doctest::Approx(10.0F));
    CHECK(first.capture_rebase_translation);
    CHECK_EQ(
        fixture.runtime->instances().at(0).value_pool.at(8).as_float(), doctest::Approx(10.0F));
    CHECK_EQ(
        fixture.runtime->instances().at(0).value_pool.at(9).as_float(), doctest::Approx(10.0F));
    CHECK_EQ(fixture.runtime->instances().at(0).value_pool.at(7).as_float(), doctest::Approx(9.5F));
  }

  TEST_CASE("MoveObjectOnPath reports unsupported transform fields structurally") {
    RuntimeFixture fixture{{move_object_on_path_script()}, move_object_on_path_values(0)};
    SUBCASE("nonzero rotation offset") {
      fixture.scx.shared_values.at(12).set_float(1.0F);
    }
    SUBCASE("unknown transform/rebase mode") {
      fixture.scx.shared_values.at(5).set_unsigned(2U);
    }
    REQUIRE(fixture.runtime->create_instance(0).has_value());
    fixture.runtime->step_tick(1.0F);
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(fixture.runtime->pause_info().reason,
        App::Script::ScriptPauseReason::k_unsupported_variant);
    CHECK_EQ(fixture.world.move_object_on_path_requests.size(), 0U);
  }

  TEST_CASE("Launch context distinguishes world and explicit-character instances") {
    RuntimeFixture fixture{
        {single_root_script(command(K_SET_FRAME, 0, 2))}, make_values(2, {0, 1})};

    const std::size_t world_id{fixture.runtime->create_instance(0).value()};
    const std::size_t character_id{fixture.runtime
            ->create_instance(
                0, App::Script::ScriptLaunchContext{.character_id = 310, .parameter = -7})
            .value()};
    REQUIRE_NE(world_id, character_id);
    const App::Script::ScriptInstance* world{fixture.runtime->instance(world_id)};
    const App::Script::ScriptInstance* character{fixture.runtime->instance(character_id)};
    REQUIRE(world != nullptr);
    REQUIRE(character != nullptr);
    CHECK_FALSE(world->launch_context.character_id.has_value());
    CHECK_EQ(world->launch_context.parameter, 0);
    CHECK_EQ(character->launch_context.character_id, std::optional<std::int16_t>{310});
    CHECK_EQ(character->launch_context.parameter, -7);

    fixture.runtime->instances().at(1).value_pool.at(1).set_unsigned(99U);
    CHECK_EQ(fixture.runtime->instances().at(0).value_pool.at(1).as_unsigned(), 1U);
  }

  TEST_CASE("Wait advances mutable elapsed time in 30 Hz script-frame units") {
    // Wait5sec in Grid.SCX uses exactly: duration=150.0, elapsed=0.0.
    RuntimeFixture fixture{
        {single_root_script(command(K_WAIT, 0, 2))}, make_values(2, {0x43160000U, 0U})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(60.0F);
    CHECK_EQ(
        fixture.runtime->instances().at(0).value_pool.at(1).as_float(), doctest::Approx(60.0F));
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_running);

    fixture.runtime->step_tick(60.0F);
    CHECK_EQ(
        fixture.runtime->instances().at(0).value_pool.at(1).as_float(), doctest::Approx(120.0F));

    fixture.runtime->step_tick(30.0F);
    CHECK_EQ(
        fixture.runtime->instances().at(0).value_pool.at(1).as_float(), doctest::Approx(150.0F));
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_completed);
  }

  TEST_CASE("Script repeat limits count whole passes and reinitialize Wait state") {
    SUBCASE("one pass") {
      App::Omikron::ScxScript script{single_root_script(command(K_WAIT, 0, 2))};
      script.repeat_limit = 1;
      RuntimeFixture fixture{{script}, make_values(2, {0x3F800000U, 0U})};
      REQUIRE(fixture.runtime->create_instance(0).has_value());

      fixture.runtime->step_tick(1.0F);

      const App::Script::ScriptInstance& instance{fixture.runtime->instances().at(0)};
      CHECK(instance.completed);
      CHECK_EQ(instance.repeat_index, 1U);
      CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_completed);
    }

    SUBCASE("two passes preserve identity and launch context") {
      App::Omikron::ScxScriptCommand wait{command(K_WAIT, 0, 2, std::nullopt, 3)};
      wait.initial_execution_count = 2;
      App::Omikron::ScxScript script{single_root_script(wait)};
      script.repeat_limit = 2;
      RuntimeFixture fixture{{script}, make_values(2, {0x3F800000U, 0U})};
      const App::Script::ScriptLaunchContext launch{.character_id = 310, .parameter = -7};
      const std::size_t id{fixture.runtime->create_instance(0, launch).value()};

      fixture.runtime->step_tick(1.0F);

      const App::Script::ScriptInstance& first_pass{fixture.runtime->instances().at(0)};
      CHECK_FALSE(first_pass.completed);
      CHECK_EQ(first_pass.instance_id, id);
      CHECK_EQ(first_pass.launch_context.character_id, launch.character_id);
      CHECK_EQ(first_pass.launch_context.parameter, launch.parameter);
      CHECK_EQ(first_pass.repeat_index, 1U);
      CHECK_EQ(first_pass.current_group_index, 0U);
      CHECK_EQ(first_pass.elapsed_script_frames, 0.0F);
      CHECK_EQ(first_pass.value_pool.at(1).as_float(), 0.0F);
      CHECK_EQ(first_pass.root_commands.at(0).initial_execution_count, 2U);
      CHECK_EQ(first_pass.root_commands.at(0).execution_count, 2U);

      fixture.runtime->step_tick(1.0F);
      const App::Script::ScriptInstance& second_pass{fixture.runtime->instances().at(0)};
      CHECK(second_pass.completed);
      CHECK_EQ(second_pass.instance_id, id);
      CHECK_EQ(second_pass.repeat_index, 2U);
    }

    SUBCASE("minus one repeats indefinitely") {
      App::Omikron::ScxScript script{single_root_script(command(K_SET_FRAME, 0, 2))};
      script.repeat_limit = -1;
      RuntimeFixture fixture{{script}, make_values(2, {0, 5})};
      const std::size_t id{fixture.runtime->create_instance(0).value()};

      fixture.runtime->step_tick(1.0F);
      fixture.runtime->step_tick(1.0F);
      fixture.runtime->step_tick(1.0F);

      const App::Script::ScriptInstance& instance{fixture.runtime->instances().at(0)};
      CHECK_FALSE(instance.completed);
      CHECK_EQ(instance.instance_id, id);
      CHECK_EQ(instance.repeat_index, 3U);
      CHECK_EQ(instance.current_group_index, 0U);
      CHECK_EQ(instance.root_commands.at(0).execution_count, 0U);
      CHECK_EQ(fixture.world.frame_requests.size(), 3U);
      CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_running);
    }
  }

  TEST_CASE("Script repeat limit does not override finite command eligibility") {
    App::Omikron::ScxScriptCommand exhausted{command(K_SET_FRAME, 0, 2)};
    exhausted.initial_execution_count = 1;
    App::Omikron::ScxScript script{single_root_script(exhausted)};
    script.repeat_limit = -1;
    RuntimeFixture fixture{{script}, make_values(2, {0, 5})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);
    fixture.runtime->step_tick(1.0F);

    CHECK(fixture.world.frame_requests.empty());
    CHECK_EQ(fixture.runtime->instances().at(0).repeat_index, 2U);
    CHECK_EQ(fixture.runtime->instances().at(0).root_commands.at(0).execution_count, 1U);
  }

  TEST_CASE("Unlimited command remains eligible on every scheduler tick") {
    App::Omikron::ScxScript script{
        single_root_script(command(K_SET_FRAME, 0, 2, std::nullopt, 0xFFFFFFFFU))};
    script.repeat_limit = -1;
    RuntimeFixture fixture{{script}, make_values(2, {0, 5})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);
    fixture.runtime->step_tick(1.0F);

    CHECK_EQ(fixture.world.frame_requests.size(), 2U);
    CHECK_EQ(fixture.runtime->instances().at(0).repeat_index, 0U);
    CHECK_FALSE(fixture.runtime->instances().at(0).completed);
  }

  TEST_CASE("Infinite Wait5sec repeats every 150 frames without completing") {
    App::Omikron::ScxScript script{single_root_script(command(K_WAIT, 0, 2))};
    script.repeat_limit = -1;
    RuntimeFixture fixture{{script}, make_values(2, {0x43160000U, 0U})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(149.0F);
    CHECK_EQ(fixture.runtime->instances().at(0).value_pool.at(1).as_float(), 149.0F);
    CHECK_EQ(fixture.runtime->instances().at(0).repeat_index, 0U);

    fixture.runtime->step_tick(1.0F);
    const App::Script::ScriptInstance& instance{fixture.runtime->instances().at(0)};
    CHECK_FALSE(instance.completed);
    CHECK_EQ(instance.repeat_index, 1U);
    CHECK_EQ(instance.value_pool.at(1).as_float(), 0.0F);
    CHECK_EQ(instance.root_commands.at(0).execution_count, 0U);
  }

  TEST_CASE("Infinite character body animation reenters its root after each whole pass") {
    App::Omikron::ScxScript script{relative_body_animation_script()};
    script.repeat_limit = -1;
    RuntimeFixture fixture{{script}, relative_body_animation_values()};
    const std::size_t id{fixture.runtime
            ->create_instance(
                0, App::Script::ScriptLaunchContext{.character_id = 310, .parameter = 0})
            .value()};

    for (std::size_t tick{0}; tick < 8U; ++tick) {
      fixture.runtime->step_tick(1.0F);
    }

    const App::Script::ScriptInstance& instance{fixture.runtime->instances().at(0)};
    CHECK_FALSE(instance.completed);
    CHECK_EQ(instance.instance_id, id);
    CHECK(instance.repeat_index >= 2U);
    CHECK(fixture.world.body_animation_requests.size() >= 8U);
    CHECK(fixture.world.body_animation_requests.at(0).first_tick);
    CHECK_FALSE(fixture.world.body_animation_requests.at(1).first_tick);
    CHECK_FALSE(fixture.world.body_animation_requests.at(2).first_tick);
    CHECK(fixture.world.body_animation_requests.at(3).first_tick);
    CHECK_EQ(fixture.world.body_animation_requests.at(3).previous_progress, doctest::Approx(0.0F));
    CHECK(fixture.world.body_animation_resets.empty());
    CHECK_EQ(instance.launch_context.character_id, std::optional<std::int16_t>{310});
  }

  TEST_CASE("Ordinary body animation marks every command execution repeat as a fresh seed") {
    // A command execution limit of two makes the command wrap once internally
    // before it completes; this is distinct from a whole-script repeat.
    App::Omikron::ScxScript script{body_animation_script(2)};
    RuntimeFixture fixture{{script}, body_animation_values()};
    REQUIRE(fixture.runtime
            ->create_instance(
                0, App::Script::ScriptLaunchContext{.character_id = 310, .parameter = 0})
            .has_value());

    for (std::size_t tick{0}; tick < 4U; ++tick) {
      fixture.runtime->step_tick(1.0F);
    }

    REQUIRE_EQ(fixture.world.select_body_animation_requests.size(), 4U);
    const auto& first{fixture.world.select_body_animation_requests.at(0)};
    const auto& second{fixture.world.select_body_animation_requests.at(1)};
    const auto& endpoint{fixture.world.select_body_animation_requests.at(2)};
    const auto& repeated{fixture.world.select_body_animation_requests.at(3)};
    CHECK(first.first_tick);
    CHECK_FALSE(second.first_tick);
    CHECK_FALSE(endpoint.first_tick);
    CHECK(repeated.first_tick);
    CHECK_EQ(repeated.previous_progress, doctest::Approx(0.0F));
    CHECK_EQ(repeated.current_progress, doctest::Approx(1.0F));
    CHECK_EQ(repeated.execution_count, 1U);
  }

  TEST_CASE("SetSpriteFrame selects the frame and completes in one tick") {
    RuntimeFixture fixture{
        {single_root_script(command(K_SET_FRAME, 0, 2))}, make_values(2, {0, 5})};

    REQUIRE(fixture.runtime->create_instance(0).has_value());
    fixture.runtime->tick(1.0F);

    REQUIRE_EQ(fixture.world.frame_requests.size(), 1U);
    CHECK_EQ(fixture.world.frame_requests.at(0), 5U);
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_completed);
  }

  TEST_CASE("ScaleSpriteOnX interpolates with the confirmed equation and order") {
    // args: sprite, initial=1, target=3, current=1, duration=10, elapsed=0.
    RuntimeFixture fixture{
        {single_root_script(command(K_SCALE_X, 0, 6, std::nullopt, 0xFFFFFFFFU))},
        make_values(6, {0, 0x3F800000U, 0x40400000U, 0x3F800000U, 0x41200000U, 0})};

    REQUIRE(fixture.runtime->create_instance(0).has_value());
    fixture.runtime->step_tick(2.0F);  // 2 script frames.

    // Visible scale set to the pre-advance current value (1.0) first.
    REQUIRE_EQ(fixture.world.scale_x.size(), 1U);
    CHECK_EQ(fixture.world.scale_x.at(0), doctest::Approx(1.0F));

    const App::Script::ScriptInstance& instance{fixture.runtime->instances().at(0)};
    // current = 1 + ((3 - 1) / 10) * 2 = 1.4; elapsed = 2.
    CHECK_EQ(instance.value_pool.at(3).as_float(), doctest::Approx(1.4F));
    CHECK_EQ(instance.value_pool.at(5).as_float(), doctest::Approx(2.0F));
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_running);
  }

  TEST_CASE("Scale completion snaps to the target and resets for repetition") {
    // Infinite limit keeps the command repeatable: first completion snaps to
    // target, then current/elapsed reset for the next cycle.
    RuntimeFixture fixture{
        {single_root_script(command(K_SCALE_X, 0, 6, std::nullopt, 0xFFFFFFFFU))},
        make_values(6, {0, 0x3F800000U, 0x40000000U, 0x3F800000U, 0x3F800000U, 0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->tick(2.0F);  // elapsed 0 < 1 -> interpolate.
    fixture.runtime->tick(0.0F);  // elapsed 2 >= 1 -> completion branch.

    // The completion branch applied the raw target (2.0).
    CHECK_EQ(fixture.world.scale_x.back(), doctest::Approx(2.0F));
    const App::Script::ScriptInstance& instance{fixture.runtime->instances().at(0)};
    // Reset for repetition: elapsed 0, current = initial (1).
    CHECK_EQ(instance.value_pool.at(5).as_float(), doctest::Approx(0.0F));
    CHECK_EQ(instance.value_pool.at(3).as_float(), doctest::Approx(1.0F));
  }

  TEST_CASE("SetSpriteRolling converts degrees to radians during interpolation") {
    // initial=0, target=90, current=0, duration=90, elapsed=0, infinite.
    RuntimeFixture fixture{{single_root_script(command(K_ROLL, 0, 6, std::nullopt, 0xFFFFFFFFU))},
        make_values(6, {0, 0, 0x42B40000U, 0, 0x42B40000U, 0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(45.0F);  // apply current 0; advance to 45 degrees.
    fixture.runtime->step_tick(45.0F);  // apply current 45 degrees (converted).

    REQUIRE_GE(fixture.world.rotations.size(), 2U);
    CHECK_EQ(fixture.world.rotations.at(0), doctest::Approx(0.0F));
    CHECK_EQ(fixture.world.rotations.at(1), doctest::Approx(0.7853981633974483F));
  }

  TEST_CASE("SetSpriteRolling completion passes the raw target unconverted") {
    // initial=0, target=90, duration=1, elapsed=0, finite limit 1.
    RuntimeFixture fixture{{single_root_script(command(K_ROLL, 0, 6))},
        make_values(6, {0, 0, 0x42B40000U, 0, 0x3F800000U, 0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->tick(2.0F);  // elapsed 0 < 1 -> interpolate (roll 0).
    fixture.runtime->tick(0.0F);  // elapsed 2 >= 1 -> completion branch.

    // Interpolation emitted 0; completion emitted the raw target 90.
    CHECK_EQ(fixture.world.rotations.at(0), doctest::Approx(0.0F));
    CHECK_EQ(fixture.world.rotations.at(1), doctest::Approx(90.0F));
  }

  TEST_CASE("Display3DSprite resolves the position and advances elapsed") {
    RuntimeFixture fixture{{single_root_script(command(K_DISPLAY_3D, 0, 4))},
        make_values(4, {0, 0, 0x41200000U /*10*/, 0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->tick(1.0F);

    REQUIRE_EQ(fixture.world.positions.size(), 1U);
    CHECK_EQ(fixture.world.positions.at(0).at(0), doctest::Approx(1.0F));
    CHECK_EQ(fixture.world.positions.at(0).at(1), doctest::Approx(2.0F));
    CHECK_EQ(fixture.world.positions.at(0).at(2), doctest::Approx(3.0F));
  }

  TEST_CASE("Missing sprite resource produces a structured error pause") {
    RuntimeFixture fixture{
        {single_root_script(command(K_SET_FRAME, 0, 2))}, make_values(2, {0, 1})};
    fixture.world.fail_ensure = true;
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->tick(1.0F);

    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(
        fixture.runtime->pause_info().reason, App::Script::ScriptPauseReason::k_missing_resource);
  }

  TEST_CASE("Unhandled opcode pauses before mutating anything") {
    RuntimeFixture fixture{
        {single_root_script(command(K_UNKNOWN_20, 0, 2))}, make_values(2, {7, 8})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->tick(1.0F);

    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_unhandled);
    const App::Script::ScriptPauseInfo& info{fixture.runtime->pause_info()};
    CHECK_EQ(info.opcode, K_UNKNOWN_20);
    CHECK_EQ(info.value_count, 2U);
    REQUIRE_EQ(info.arguments.size(), 2U);
    CHECK_EQ(info.arguments.at(0).raw, 7U);
    CHECK_EQ(info.arguments.at(1).raw, 8U);

    const App::Script::ScriptInstance& instance{fixture.runtime->instances().at(0)};
    CHECK_EQ(instance.root_commands.at(0).execution_count, 0U);  // Not mutated.
    CHECK_EQ(instance.value_pool.at(0).raw, 7U);                 // Not mutated.
  }

  TEST_CASE("A cycle in the linked chain is detected and pauses") {
    App::Omikron::ScxScript script;
    script.name = "cycle";
    script.root_command_count = 1;
    script.linked_command_count = 2;
    script.repeat_limit = 1;
    script.root_commands.push_back(command(K_SET_SPRITE_TYPE, 0, 2, 0));
    script.linked_commands.push_back(command(K_SET_FRAME, 2, 2, 1));
    script.linked_commands.push_back(command(K_SET_FRAME, 4, 2, 0));  // loops back.

    RuntimeFixture fixture{{script}, make_values(6)};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->tick(1.0F);

    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(fixture.runtime->pause_info().reason,
        App::Script::ScriptPauseReason::k_invalid_linked_command);
  }

  TEST_CASE("Group advances once when every finite command completes") {
    App::Omikron::ScxScript script;
    script.name = "group";
    script.root_command_count = 2;
    script.linked_command_count = 0;
    script.repeat_limit = 1;
    script.root_commands.push_back(command(K_SET_FRAME, 0, 2));  // group 0
    script.root_commands.push_back(command(K_SET_FRAME, 2, 2));  // group 1

    RuntimeFixture fixture{{script}, make_values(4, {0, 1, 0, 2})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->tick(1.0F);  // group 0 completes.
    const App::Script::ScriptInstance& instance{fixture.runtime->instances().at(0)};
    CHECK_EQ(instance.current_group_index, 1U);
    CHECK_FALSE(instance.completed);

    fixture.runtime->tick(1.0F);  // group 1 completes -> instance done.
    CHECK_EQ(instance.completed, true);
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_completed);
  }

  TEST_CASE("Reset restores initial values and counters") {
    App::Omikron::ScxScript script{
        single_root_script(command(K_SCALE_X, 0, 6, std::nullopt, 0xFFFFFFFFU))};
    script.initial_repeat_index = 7;
    RuntimeFixture fixture{
        {script}, make_values(6, {0, 0x3F800000U, 0x40000000U, 0x3F800000U, 0x41200000U, 0})};
    const std::size_t id{fixture.runtime->create_instance(0).value()};

    fixture.runtime->step_tick(2.0F);
    // Interpolation mutated the working current value (1 + (2-1)/10 * 2 = 1.2).
    CHECK_EQ(fixture.runtime->instances().at(0).value_pool.at(3).as_float(), doctest::Approx(1.2F));

    REQUIRE(fixture.runtime->reset_instance(id).has_value());
    const App::Script::ScriptInstance& instance{fixture.runtime->instances().at(0)};
    CHECK_EQ(instance.root_commands.at(0).execution_count, 0U);
    CHECK_EQ(instance.repeat_index, 7U);
    CHECK_EQ(instance.initial_repeat_index, 7U);
    CHECK_EQ(instance.value_pool.at(3).as_float(), doctest::Approx(1.0F));  // current reset.
    CHECK_EQ(instance.value_pool.at(5).as_float(), doctest::Approx(0.0F));  // elapsed reset.
  }

  TEST_CASE("Two instances of the same script do not share mutable state") {
    RuntimeFixture fixture{
        {single_root_script(command(K_SCALE_X, 0, 6, std::nullopt, 0xFFFFFFFFU))},
        make_values(6, {0, 0x3F800000U, 0x40000000U, 0x3F800000U, 0x41200000U, 0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(2.0F);

    auto& instances{fixture.runtime->instances()};
    CHECK_EQ(instances.at(0).value_pool.at(3).as_float(), doctest::Approx(1.2F));
    CHECK_EQ(instances.at(1).value_pool.at(3).as_float(), doctest::Approx(1.2F));
    // Both advanced identically, but their pools are independent objects.
    CHECK_NE(instances.at(0).value_pool.data(), instances.at(1).value_pool.data());
    instances.at(0).value_pool.at(3).set_float(99.0F);
    CHECK_EQ(instances.at(1).value_pool.at(3).as_float(), doctest::Approx(1.2F));
  }

  TEST_CASE("Invalid argument count pauses with a structured reason") {
    // SetSpriteFrame needs 2 arguments; supply 1.
    RuntimeFixture fixture{{single_root_script(command(K_SET_FRAME, 0, 1))}, make_values(1, {0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->tick(1.0F);

    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(fixture.runtime->pause_info().reason,
        App::Script::ScriptPauseReason::k_invalid_argument_count);
  }

  TEST_CASE("Display3DSprite pauses on a zero duration") {
    RuntimeFixture fixture{{single_root_script(command(K_DISPLAY_3D, 0, 4))},
        make_values(4, {0, 0, 0 /*duration 0*/, 0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->tick(1.0F);

    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(
        fixture.runtime->pause_info().reason, App::Script::ScriptPauseReason::k_invalid_duration);
  }

  TEST_CASE("Paused execution does not advance time or mutate commands") {
    RuntimeFixture fixture{
        {single_root_script(command(K_UNKNOWN_20, 0, 2))}, make_values(2, {7, 8})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->tick(1.0F);  // Pauses.
    const std::uint64_t tick_after_pause{fixture.runtime->tick_count()};

    fixture.runtime->tick(1.0F);  // No-op while paused.
    CHECK_EQ(fixture.runtime->tick_count(), tick_after_pause);
    CHECK_EQ(fixture.runtime->instances().at(0).root_commands.at(0).execution_count, 0U);
  }

  TEST_CASE("SetSpriteType writes the requested type and increments once") {
    RuntimeFixture fixture{
        {single_root_script(command(K_SET_SPRITE_TYPE, 0, 2))}, make_values(2, {0, 7})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.sprite_types.size(), 1U);
    CHECK_EQ(fixture.world.sprite_types.at(0), 7U);
    CHECK_EQ(fixture.runtime->instances().at(0).root_commands.at(0).execution_count, 1U);
  }

  TEST_CASE("SetSpriteType writes zero explicitly") {
    RuntimeFixture fixture{
        {single_root_script(command(K_SET_SPRITE_TYPE, 0, 2))}, make_values(2, {0, 0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.sprite_types.size(), 1U);
    CHECK_EQ(fixture.world.sprite_types.at(0), 0U);
  }

  TEST_CASE("SetSpriteType truncates to the low 16 bits") {
    RuntimeFixture fixture{
        {single_root_script(command(K_SET_SPRITE_TYPE, 0, 2))}, make_values(2, {0, 0x10005U})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.sprite_types.size(), 1U);
    CHECK_EQ(fixture.world.sprite_types.at(0), 5U);
  }

  TEST_CASE("SetSpriteType does not mutate an already exhausted command") {
    App::Omikron::ScxScript script;
    script.name = "exhausted";
    script.root_command_count = 1;
    script.linked_command_count = 1;
    script.repeat_limit = 1;
    script.root_commands.push_back(command(K_SET_SPRITE_TYPE, 0, 2, 0));
    script.linked_commands.push_back(command(K_SET_FRAME, 2, 2, std::nullopt, 0xFFFFFFFFU));

    RuntimeFixture fixture{{script}, make_values(4, {0, 7, 0, 5})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);
    fixture.runtime->step_tick(1.0F);

    // The type was written exactly once (not re-applied on the second tick).
    REQUIRE_EQ(fixture.world.sprite_types.size(), 1U);
    CHECK_EQ(fixture.world.sprite_types.at(0), 7U);
    CHECK_EQ(fixture.runtime->instances().at(0).root_commands.at(0).execution_count, 1U);
  }

  TEST_CASE("SetSpriteType pauses on a missing sprite") {
    RuntimeFixture fixture{
        {single_root_script(command(K_SET_SPRITE_TYPE, 0, 2))}, make_values(2, {0, 7})};
    fixture.world.fail_ensure = true;
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(
        fixture.runtime->pause_info().reason, App::Script::ScriptPauseReason::k_missing_resource);
  }

  TEST_CASE("SetSpriteType pauses on a malformed argument count") {
    RuntimeFixture fixture{
        {single_root_script(command(K_SET_SPRITE_TYPE, 0, 1))}, make_values(1, {0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(fixture.runtime->pause_info().reason,
        App::Script::ScriptPauseReason::k_invalid_argument_count);
  }

  TEST_CASE("Real delta converts to script frames with the three-frame clamp") {
    CHECK_EQ(App::Script::convert_real_delta_to_script_frames(1.0F / 30.0F), doctest::Approx(1.0F));
    CHECK_EQ(App::Script::convert_real_delta_to_script_frames(1.0F / 60.0F), doctest::Approx(0.5F));
    CHECK_EQ(App::Script::convert_real_delta_to_script_frames(0.1F), doctest::Approx(3.0F));
  }

  TEST_CASE("Display3DSprite with duration 60 exhausts on the 60th frame at delta 1.0") {
    RuntimeFixture fixture{
        {single_root_script(command(K_DISPLAY_3D, 0, 4))}, make_values(4, {0, 0, 0x42700000U, 0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    for (int update{0}; update < 59; ++update) {
      fixture.runtime->step_tick(1.0F);
      CHECK_FALSE(fixture.runtime->instances().at(0).completed);
    }
    CHECK_EQ(fixture.runtime->instances().at(0).current_group_index, 0U);

    fixture.runtime->step_tick(1.0F);  // 60th update.
    CHECK_EQ(fixture.runtime->instances().at(0).current_group_index, 1U);
    CHECK_EQ(fixture.runtime->instances().at(0).completed, true);
  }

  TEST_CASE("Display3DSprite with duration 60 exhausts on the 120th frame at delta 0.5") {
    RuntimeFixture fixture{
        {single_root_script(command(K_DISPLAY_3D, 0, 4))}, make_values(4, {0, 0, 0x42700000U, 0})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    for (int update{0}; update < 119; ++update) {
      fixture.runtime->step_tick(0.5F);
      CHECK_FALSE(fixture.runtime->instances().at(0).completed);
    }
    fixture.runtime->step_tick(0.5F);  // 120th update.
    CHECK_EQ(fixture.runtime->instances().at(0).completed, true);
  }

  TEST_CASE("A six-command chain dispatches in one tick in order") {
    App::Omikron::ScxScript script;
    script.name = "effect";
    script.root_command_count = 1;
    script.linked_command_count = 5;
    script.repeat_limit = 1;
    script.root_commands.push_back(command(K_SET_SPRITE_TYPE, 0, 2, 0));
    script.linked_commands.push_back(command(K_SET_FRAME, 2, 2, 1));
    script.linked_commands.push_back(command(K_ROLL, 4, 6, 2));
    script.linked_commands.push_back(command(K_SCALE_X, 10, 6, 3));
    script.linked_commands.push_back(command(K_SCALE_Y, 16, 6, 4));
    script.linked_commands.push_back(command(K_DISPLAY_3D, 22, 4, std::nullopt));

    std::vector<std::uint32_t> words(26, 0);
    words.at(8) = 0x42700000U;   // roll duration 60
    words.at(11) = 0x3F800000U;  // scaleX initial 1
    words.at(12) = 0x3F800000U;  // scaleX target 1
    words.at(13) = 0x3F800000U;  // scaleX current 1
    words.at(14) = 0x42700000U;  // scaleX duration 60
    words.at(17) = 0x3F800000U;  // scaleY initial 1
    words.at(18) = 0x3F800000U;  // scaleY target 1
    words.at(19) = 0x3F800000U;  // scaleY current 1
    words.at(20) = 0x42700000U;  // scaleY duration 60
    words.at(24) = 0x42700000U;  // display duration 60

    RuntimeFixture fixture{{script}, make_values(26, words)};
    fixture.runtime->set_trace_enabled(true);
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    const auto& trace{fixture.runtime->trace()};
    REQUIRE_EQ(trace.size(), 6U);
    CHECK_EQ(trace.at(0).opcode_name, "SetSpriteType");
    CHECK_EQ(trace.at(1).opcode_name, "SetSpriteFrame");
    CHECK_EQ(trace.at(2).opcode_name, "SetSpriteRolling");
    CHECK_EQ(trace.at(3).opcode_name, "ScaleSpriteOnX");
    CHECK_EQ(trace.at(4).opcode_name, "ScaleSpriteOnY");
    CHECK_EQ(trace.at(5).opcode_name, "Display3DSprite");

    const auto& instance{fixture.runtime->instances().at(0)};
    CHECK_EQ(instance.root_commands.at(0).execution_count, 1U);
    CHECK_EQ(instance.linked_commands.at(0).execution_count, 1U);
    CHECK_EQ(instance.linked_commands.at(1).execution_count, 0U);
    CHECK_EQ(instance.value_pool.at(9).as_float(), doctest::Approx(1.0F));  // roll elapsed
    CHECK_FALSE(instance.completed);
  }

  TEST_CASE("An out-of-range linked index pauses") {
    App::Omikron::ScxScript script;
    script.name = "oor";
    script.root_command_count = 1;
    script.linked_command_count = 1;
    script.repeat_limit = 1;
    script.root_commands.push_back(command(K_SET_SPRITE_TYPE, 0, 2, 5));
    script.linked_commands.push_back(command(K_SET_FRAME, 2, 2, std::nullopt));

    RuntimeFixture fixture{{script}, make_values(4)};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_error);
    CHECK_EQ(fixture.runtime->pause_info().reason,
        App::Script::ScriptPauseReason::k_invalid_linked_command);
  }

  TEST_CASE("An unhandled opcode in the middle of a group pauses without advancing") {
    App::Omikron::ScxScript script;
    script.name = "mid";
    script.root_command_count = 1;
    script.linked_command_count = 2;
    script.repeat_limit = 1;
    script.root_commands.push_back(command(K_SET_SPRITE_TYPE, 0, 2, 0));
    script.linked_commands.push_back(command(K_UNKNOWN_20, 2, 2, 1));
    script.linked_commands.push_back(command(K_SET_FRAME, 4, 2, std::nullopt));

    RuntimeFixture fixture{{script}, make_values(6)};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_paused_on_unhandled);
    CHECK_EQ(fixture.runtime->instances().at(0).current_group_index, 0U);
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Audio opcodes
  // ─────────────────────────────────────────────────────────────────────────

  TEST_CASE("PlaySound {9,0,0,-1} queues one nonspatial one-shot and latches") {
    RuntimeFixture fixture{
        {single_root_script(command(K_PLAY_SOUND, 0, 4))}, make_values(4, {9, 0, 0, 0xFFFFFFFFU})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.play_requests.size(), 1U);
    const App::Audio::SoundPlayRequest& request{fixture.world.play_requests.at(0)};
    CHECK_EQ(request.resource.index, 7U);
    CHECK_FALSE(request.loop);
    CHECK_FALSE(request.emitter.has_value());
    CHECK(request.owner.is_null());
    CHECK_EQ(request.scenario_sound_index, 9U);
    CHECK_EQ(request.sound_name, "fx");
    CHECK_EQ(request.provenance.origin, App::Audio::AudioOrigin::k_structured_script);
    REQUIRE(request.provenance.source_script_index.has_value());
    CHECK_EQ(request.provenance.source_script_index.value(), 0U);
    REQUIRE(request.provenance.script_instance_id.has_value());
    CHECK_EQ(request.provenance.script_instance_id.value(), 1U);
    REQUIRE(request.provenance.function_id.has_value());
    CHECK_EQ(request.provenance.function_id.value(), K_PLAY_SOUND);
    CHECK_EQ(fixture.runtime->instances().at(0).value_pool.at(2).raw, 1U);  // latch set.
  }

  TEST_CASE("PlaySound does not queue twice") {
    RuntimeFixture fixture{
        {single_root_script(command(K_PLAY_SOUND, 0, 4))}, make_values(4, {9, 0, 0, 0xFFFFFFFFU})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);
    fixture.runtime->step_tick(1.0F);

    CHECK_EQ(fixture.world.play_requests.size(), 1U);
  }

  TEST_CASE("PlaySound loop flag bit 0 produces looping") {
    RuntimeFixture fixture{
        {single_root_script(command(K_PLAY_SOUND, 0, 4))}, make_values(4, {9, 1, 0, 0xFFFFFFFFU})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.play_requests.size(), 1U);
    CHECK(fixture.world.play_requests.at(0).loop);
  }

  TEST_CASE("PlaySound preserves unknown flag bits without inventing behaviour") {
    RuntimeFixture fixture{{single_root_script(command(K_PLAY_SOUND, 0, 4))},
        make_values(4, {9, 0x08, 0, 0xFFFFFFFFU})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.play_requests.size(), 1U);
    CHECK_FALSE(fixture.world.play_requests.at(0).loop);  // bit 0 clear.
    CHECK_EQ(fixture.world.play_requests.at(0).raw_flags, 0x08U);
  }

  TEST_CASE("Attached PlaySound uses the recovered 78/1170 distance defaults") {
    RuntimeFixture fixture{
        {single_root_script(command(K_PLAY_SOUND, 0, 4))}, make_values(4, {9, 0, 0, 5})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.play_requests.size(), 1U);
    const App::Audio::SoundPlayRequest& request{fixture.world.play_requests.at(0)};
    REQUIRE(request.emitter.has_value());
    CHECK_FALSE(request.owner.is_null());
    CHECK_EQ(request.owner.object_index, 5U);
    CHECK_EQ(request.emitter->minimum_distance, doctest::Approx(78.0F));
    CHECK_EQ(request.emitter->maximum_distance, doctest::Approx(1170.0F));
    CHECK_EQ(request.emitter->position.at(0), doctest::Approx(1.0F));
  }

  TEST_CASE("PlaySound queue failure still latches and exhausts") {
    RuntimeFixture fixture{
        {single_root_script(command(K_PLAY_SOUND, 0, 4))}, make_values(4, {9, 0, 0, 0xFFFFFFFFU})};
    fixture.world.fail_play = true;
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    CHECK_EQ(fixture.runtime->instances().at(0).value_pool.at(2).raw, 1U);
    CHECK_EQ(fixture.runtime->instances().at(0).root_commands.at(0).execution_count, 1U);
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_completed);
  }

  TEST_CASE("PlaySound invalid sound index fails without pausing") {
    RuntimeFixture fixture{{single_root_script(command(K_PLAY_SOUND, 0, 4))},
        make_values(4, {999, 0, 0, 0xFFFFFFFFU})};
    fixture.world.fail_sound = true;
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    CHECK_EQ(fixture.runtime->instances().at(0).value_pool.at(2).raw, 1U);
    CHECK(fixture.world.play_requests.empty());
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_completed);
  }

  TEST_CASE("PlaySyncSound waits for its scheduled time and starts once") {
    // arg1 = 3.0 script frames. ScriptRuntime::step_tick() already speaks
    // native 30 Hz script-frame units; do not convert this authored schedule
    // to seconds a second time.
    RuntimeFixture fixture{{single_root_script(command(K_PLAY_SYNC_SOUND, 0, 5))},
        make_values(5, {9, 0x40400000U, 0, 0, 0xFFFFFFFFU})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);
    fixture.runtime->step_tick(1.0F);
    CHECK(fixture.world.play_requests.empty());  // not due yet.

    fixture.runtime->step_tick(1.0F);  // elapsed reaches frame 3 -> due.
    REQUIRE_EQ(fixture.world.play_requests.size(), 1U);
    // voiceIndex + 1 = 3 + 1.
    CHECK_EQ(fixture.runtime->instances().at(0).value_pool.at(3).raw, 4U);

    fixture.runtime->step_tick(1.0F);  // exhausted: no second start.
    CHECK_EQ(fixture.world.play_requests.size(), 1U);
  }

  TEST_CASE("PlaySyncSound retail-style schedule 180 means six seconds at 30 Hz") {
    // Grid.SCX 1KaylArrives contains synchronized sounds scheduled as high as
    // 180.0 while INTRO1.3DA ends at frame 185. Runtime therefore interprets
    // 180.0 as script frames (6 seconds), not 180 wall-clock seconds.
    RuntimeFixture fixture{{single_root_script(command(K_PLAY_SYNC_SOUND, 0, 5))},
        make_values(5, {9, 0x43340000U, 0, 0, 0xFFFFFFFFU})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    for (std::size_t frame{0}; frame < 179U; ++frame) {
      fixture.runtime->step_tick(1.0F);
    }
    CHECK(fixture.world.play_requests.empty());

    fixture.runtime->step_tick(1.0F);
    REQUIRE_EQ(fixture.world.play_requests.size(), 1U);
    CHECK_EQ(fixture.runtime->instances().at(0).value_pool.at(3).raw, 4U);
  }

  TEST_CASE("Attached PlaySyncSound uses the recovered 0/30 distance defaults") {
    RuntimeFixture fixture{
        {single_root_script(command(K_PLAY_SYNC_SOUND, 0, 5))}, make_values(5, {9, 0, 0, 0, 5})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.play_requests.size(), 1U);
    const App::Audio::SoundPlayRequest& request{fixture.world.play_requests.at(0)};
    REQUIRE(request.emitter.has_value());
    CHECK_EQ(request.emitter->minimum_distance, doctest::Approx(0.0F));
    CHECK_EQ(request.emitter->maximum_distance, doctest::Approx(30.0F));
  }

  TEST_CASE("StopSound -1 stops the first matching null-owner voice") {
    RuntimeFixture fixture{
        {single_root_script(command(K_STOP_SOUND, 0, 2))}, make_values(2, {9, 0xFFFFFFFFU})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.stop_requests.size(), 1U);
    CHECK_EQ(fixture.world.stop_requests.at(0).first.index, 7U);
    CHECK(fixture.world.stop_requests.at(0).second.is_null());
    CHECK_EQ(fixture.runtime->run_state(), App::Script::ScriptRunState::k_completed);
  }

  TEST_CASE("StopSound with an object stops the matching (soundId, owner) voice") {
    RuntimeFixture fixture{
        {single_root_script(command(K_STOP_SOUND, 0, 2))}, make_values(2, {9, 5})};
    REQUIRE(fixture.runtime->create_instance(0).has_value());

    fixture.runtime->step_tick(1.0F);

    REQUIRE_EQ(fixture.world.stop_requests.size(), 1U);
    CHECK_EQ(fixture.world.stop_requests.at(0).first.index, 7U);
    CHECK_FALSE(fixture.world.stop_requests.at(0).second.is_null());
    CHECK_EQ(fixture.world.stop_requests.at(0).second.object_index, 5U);
  }

  TEST_CASE("PlaySound reset clears the latch and stops the matching voice") {
    RuntimeFixture fixture{
        {single_root_script(command(K_PLAY_SOUND, 0, 4))}, make_values(4, {9, 0, 0, 0xFFFFFFFFU})};
    const std::size_t id{fixture.runtime->create_instance(0).value()};

    fixture.runtime->step_tick(1.0F);
    CHECK_EQ(fixture.world.play_requests.size(), 1U);

    REQUIRE(fixture.runtime->reset_instance(id).has_value());
    CHECK_EQ(fixture.world.stop_requests.size(), 1U);
    CHECK(fixture.world.stop_requests.at(0).second.is_null());
    CHECK_EQ(fixture.runtime->instances().at(0).value_pool.at(2).raw, 0U);
  }
}

// NOLINTEND(misc-use-anonymous-namespace, cppcoreguidelines-avoid-do-while, cert-err33-c,
// misc-include-cleaner, cppcoreguidelines-pro-bounds-constant-array-index,
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
