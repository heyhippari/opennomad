#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace App::Interface {

/// Runtime OPTIONS row types recovered from the descriptor table beginning at
/// 0x004DA574 (stride 0x8C). Phase 1 only renders submenu/back rows, but the
/// semantic vocabulary is established now so later phases can add values
/// declaratively instead of growing interface-specific switch statements.
enum class OptionsRowKind : std::uint8_t {
  k_enum = 0,
  k_slider = 1,
  k_submenu = 2,
  k_binding = 3,
  k_dynamic = 4,
  k_command = 5,
  k_back = 6,
};

/// One Runtime choice belonging to a type-0 option descriptor.
struct OptionsChoiceDefinition {
  std::int16_t runtime_string_index{-1};
  std::int32_t raw_value{0};
  std::string_view literal_label;
};

/// One declarative OPTIONS row, preserving both descriptor provenance and the
/// actual IAM string index used for its visible label.
///
/// This distinction fixes an ambiguity in Phase 1: Runtime page builders pass
/// an *option descriptor index* to 0x00490F90, while the descriptor's +0x18
/// field supplies a separate IAM/Options string index.
struct OptionsRowDefinition {
  std::string_view stable_id;
  std::int16_t runtime_option_index{-1};
  std::int16_t runtime_label_string_index{-1};
  OptionsRowKind kind{OptionsRowKind::k_submenu};
  std::span<const OptionsChoiceDefinition> choices;
  std::size_t default_choice{0};
  std::string_view literal_label;
  bool accent{false};
};

/// A page is just a stable ID plus an ordered row span. Runtime used a fixed
/// 16-row scratch pool; OpenNomad deliberately does not preserve that limit.
/// Later phases can register additional pages or append OpenNomad-only rows
/// without changing the navigation/layout machinery.
struct OptionsPageDefinition {
  std::string_view stable_id;
  std::span<const OptionsRowDefinition> rows;
};

/// Runtime type-3 option descriptor. +0x5C selects one of four control groups
/// and +0x70 selects one of fourteen slots inside that group. The three
/// physical-device values live in OMK_SAVE's parallel 4x14 tables.
struct OptionsBindingRowDefinition {
  std::string_view stable_id;
  std::int16_t runtime_option_index{-1};
  std::int16_t runtime_label_string_index{-1};
  std::uint8_t group{0};
  std::uint8_t slot{0};
};

struct OptionsBindingPageDefinition {
  std::string_view stable_id;
  std::int16_t runtime_title_option_index{-1};
  std::int16_t runtime_title_string_index{-1};
  std::uint8_t group{0};
  std::span<const OptionsBindingRowDefinition> bindings;
};

inline constexpr std::uint16_t k_options_interface_id{35};
inline constexpr char k_options_root_font_key{'S'};
inline constexpr char k_options_value_font_key{'J'};
inline constexpr std::uint32_t k_options_text_flags{0x80000010U};
inline constexpr std::uint32_t k_options_value_text_flags{0x80000008U};
inline constexpr int k_options_row_x{0};
inline constexpr int k_options_row_width{640};
inline constexpr int k_options_row_height{40};

/// Runtime Options_Initialize @ 0x00490D50 distinguishes two known hosting
/// contexts by probing resident interface 9 and interface 29. Only the START
/// MENU path is exercised by this phase; the enum preserves the other known
/// mode without assigning interface 9 a speculative semantic name.
enum class OptionsInvocationMode : std::uint8_t {
  k_interface_9 = 0,
  k_start_menu = 1,
};

/// Effective row start used by Runtime helper 0x004910B0.
[[nodiscard]] constexpr int runtime_options_row_start_y(const OptionsInvocationMode mode) {
  return mode == OptionsInvocationMode::k_start_menu ? 0x78 : 0x50;
}

/// Recovered dynamic spacing from Runtime helper 0x004910B0.
///
/// The helper first counts rows whose option index is not -1. For one row the
/// step is unused/zero. For 2-3 active rows it distributes them over a 140-unit
/// span; for 4+ rows it uses a 280-unit span. The integer arithmetic below is
/// intentionally the Runtime arithmetic rather than an aesthetic replacement.
[[nodiscard]] constexpr int runtime_options_row_step(const std::size_t active_count) {
  if (active_count <= 1U) {
    return 0;
  }
  const int count{static_cast<int>(active_count)};
  const int span{active_count > 3U ? 0x118 : 0x8C};
  return ((span - (count * 20)) / (count - 1)) + 20;
}

/// Effective Y coordinate for one active row after 0x004910B0 has laid out the
/// page. OpenNomad stores only active rows, so row_index is the active ordinal
/// rather than an index into Runtime's fixed 16-row scratch pool.
[[nodiscard]] constexpr int runtime_options_row_y(
    const std::size_t row_index, const std::size_t active_count, const OptionsInvocationMode mode) {
  return runtime_options_row_start_y(mode) +
         (static_cast<int>(row_index) * runtime_options_row_step(active_count));
}

/// Runtime root page builder @ 0x00491200:
///   IAM/Options[1]  -> state 0x004DD4A0 (Video)
///   IAM/Options[9]  -> state 0x004DD508 (Audio)
///   IAM/Options[14] -> state 0x004DD570 (Game)
///   IAM/Options[19] -> state 0x004DD5D8 (Controls)
///   IAM/Options[72] -> Back (present on the START MENU invocation path)
///
/// The numbers above are Runtime option-descriptor indices, not the visible
/// IAM string indices. Descriptor +0x18 yields 0/16/21/28/80 respectively.
inline constexpr std::array<OptionsRowDefinition, 6> k_options_root_rows{{
    {.stable_id = "video",
        .runtime_option_index = 1,
        .runtime_label_string_index = 0,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "audio",
        .runtime_option_index = 9,
        .runtime_label_string_index = 16,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "game",
        .runtime_option_index = 14,
        .runtime_label_string_index = 21,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "controls",
        .runtime_option_index = 19,
        .runtime_label_string_index = 28,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "enhancements",
        .runtime_option_index = -1,
        .runtime_label_string_index = -1,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = "Enhancements",
        .accent = false},
    {.stable_id = "back",
        .runtime_option_index = 72,
        .runtime_label_string_index = 80,
        .kind = OptionsRowKind::k_back,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
}};
inline constexpr OptionsPageDefinition k_options_root_page{
    .stable_id = "root", .rows = std::span<const OptionsRowDefinition>{k_options_root_rows}};

// Runtime type-0 value tables recovered from the descriptor records beginning
// at 0x004DA574. Labels remain in IAM/Options; raw values are what the original
// callbacks write into OMK_SAVE/runtime state.
inline constexpr std::array<OptionsChoiceDefinition, 5> k_options_clipping_choices{{
    {.runtime_string_index = 7, .raw_value = 25, .literal_label = {}},
    {.runtime_string_index = 6, .raw_value = 50, .literal_label = {}},
    {.runtime_string_index = 5, .raw_value = 100, .literal_label = {}},
    {.runtime_string_index = 4, .raw_value = 150, .literal_label = {}},
    {.runtime_string_index = 3, .raw_value = 200, .literal_label = {}},
}};
inline constexpr std::array<OptionsChoiceDefinition, 2> k_options_yes_no_choices{{
    {.runtime_string_index = 60, .raw_value = 0, .literal_label = {}},
    {.runtime_string_index = 61, .raw_value = 1, .literal_label = {}},
}};
inline constexpr std::array<OptionsChoiceDefinition, 5> k_options_street_activity_choices{{
    {.runtime_string_index = 15, .raw_value = 0, .literal_label = {}},
    {.runtime_string_index = 14, .raw_value = 1, .literal_label = {}},
    {.runtime_string_index = 13, .raw_value = 2, .literal_label = {}},
    {.runtime_string_index = 12, .raw_value = 3, .literal_label = {}},
    {.runtime_string_index = 11, .raw_value = 4, .literal_label = {}},
}};
inline constexpr std::array<OptionsChoiceDefinition, 3> k_options_detail_choices{{
    {.runtime_string_index = 73, .raw_value = 0, .literal_label = {}},
    {.runtime_string_index = 65, .raw_value = 1, .literal_label = {}},
    {.runtime_string_index = 75, .raw_value = 2, .literal_label = {}},
}};

inline constexpr std::array<OptionsChoiceDefinition, 2> k_options_3d_sound_choices{{
    {.runtime_string_index = 62, .raw_value = 0, .literal_label = {}},
    {.runtime_string_index = 63, .raw_value = 1, .literal_label = {}},
}};

inline constexpr std::array<OptionsChoiceDefinition, 3> k_options_difficulty_choices{{
    {.runtime_string_index = 64, .raw_value = 0, .literal_label = {}},
    {.runtime_string_index = 65, .raw_value = 1, .literal_label = {}},
    {.runtime_string_index = 66, .raw_value = 2, .literal_label = {}},
}};

inline constexpr std::array<OptionsChoiceDefinition, 2> k_options_fight_camera_choices{{
    {.runtime_string_index = 23, .raw_value = 0, .literal_label = {}},
    {.runtime_string_index = 24, .raw_value = 1, .literal_label = {}},
}};

/// Runtime Video page builder @ 0x004913F0 activates descriptors
/// 1,2,3,4,5,6,7,8 and 72 in this exact order.
///
/// Defaults mirror Runtime's initialization:
///   clipping = raw 50          (OMK_SAVE +0x14)
///   display sky = 1            (OMK_SAVE +0x10)
///   display shadow = 1         (OMK_SAVE +0x11)
///   street activity = 3        (0x0090E726)
///   detail level = 1           (0x0090E727)
///
/// Resolution and renderer are Runtime type-4 dynamic values. OpenNomad seeds
/// those from the live window/OpenGL environment rather than inventing legacy
/// DirectDraw device choices.
inline constexpr std::array<OptionsRowDefinition, 9> k_options_video_rows{{
    {.stable_id = "video.title",
        .runtime_option_index = 1,
        .runtime_label_string_index = 0,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = true},
    {.stable_id = "video.resolution",
        .runtime_option_index = 2,
        .runtime_label_string_index = 1,
        .kind = OptionsRowKind::k_dynamic,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "video.clipping_distance",
        .runtime_option_index = 3,
        .runtime_label_string_index = 2,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_clipping_choices},
        .default_choice = 1,
        .literal_label = {},
        .accent = false},
    {.stable_id = "video.display_sky",
        .runtime_option_index = 4,
        .runtime_label_string_index = 8,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_yes_no_choices},
        .default_choice = 1,
        .literal_label = {},
        .accent = false},
    {.stable_id = "video.display_shadow",
        .runtime_option_index = 5,
        .runtime_label_string_index = 9,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_yes_no_choices},
        .default_choice = 1,
        .literal_label = {},
        .accent = false},
    {.stable_id = "video.street_activity",
        .runtime_option_index = 6,
        .runtime_label_string_index = 10,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_street_activity_choices},
        .default_choice = 3,
        .literal_label = {},
        .accent = false},
    {.stable_id = "video.detail_level",
        .runtime_option_index = 7,
        .runtime_label_string_index = 72,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_detail_choices},
        .default_choice = 1,
        .literal_label = {},
        .accent = false},
    {.stable_id = "video.renderer",
        .runtime_option_index = 8,
        .runtime_label_string_index = 67,
        .kind = OptionsRowKind::k_dynamic,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "video.back",
        .runtime_option_index = 72,
        .runtime_label_string_index = 80,
        .kind = OptionsRowKind::k_back,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
}};
inline constexpr OptionsPageDefinition k_options_video_page{
    .stable_id = "video", .rows = std::span<const OptionsRowDefinition>{k_options_video_rows}};

/// Runtime Audio page builder @ 0x00491640 activates descriptors
/// 9,10,11,12,13 and 72.
///
/// The three type-1 rows are bounded 0..100 values. Runtime's generic options
/// input handler changes them by 10 per left/right press and clamps at the
/// endpoints. OMK_SAVE initializes the corresponding attenuation fields to
/// zero, which the Runtime callbacks present as UI volume 100.
inline constexpr std::array<OptionsRowDefinition, 6> k_options_audio_rows{{
    {.stable_id = "audio.title",
        .runtime_option_index = 9,
        .runtime_label_string_index = 16,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = true},
    {.stable_id = "audio.dialogue_volume",
        .runtime_option_index = 10,
        .runtime_label_string_index = 17,
        .kind = OptionsRowKind::k_slider,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "audio.ambient_volume",
        .runtime_option_index = 11,
        .runtime_label_string_index = 18,
        .kind = OptionsRowKind::k_slider,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "audio.sfx_volume",
        .runtime_option_index = 12,
        .runtime_label_string_index = 19,
        .kind = OptionsRowKind::k_slider,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "audio.3d_sound",
        .runtime_option_index = 13,
        .runtime_label_string_index = 20,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_3d_sound_choices},
        .default_choice = 1,
        .literal_label = {},
        .accent = false},
    {.stable_id = "audio.back",
        .runtime_option_index = 72,
        .runtime_label_string_index = 80,
        .kind = OptionsRowKind::k_back,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
}};
inline constexpr OptionsPageDefinition k_options_audio_page{
    .stable_id = "audio", .rows = std::span<const OptionsRowDefinition>{k_options_audio_rows}};

/// Runtime Game page builder @ 0x00491810 activates descriptors
/// 14,16,17,18 and 72. All three values default to raw/index 1.
inline constexpr std::array<OptionsRowDefinition, 5> k_options_game_rows{{
    {.stable_id = "game.title",
        .runtime_option_index = 14,
        .runtime_label_string_index = 21,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = true},
    {.stable_id = "game.fight_difficulty",
        .runtime_option_index = 16,
        .runtime_label_string_index = 26,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_difficulty_choices},
        .default_choice = 1,
        .literal_label = {},
        .accent = false},
    {.stable_id = "game.shoot_difficulty",
        .runtime_option_index = 17,
        .runtime_label_string_index = 27,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_difficulty_choices},
        .default_choice = 1,
        .literal_label = {},
        .accent = false},
    {.stable_id = "game.fight_camera",
        .runtime_option_index = 18,
        .runtime_label_string_index = 22,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_fight_camera_choices},
        .default_choice = 1,
        .literal_label = {},
        .accent = false},
    {.stable_id = "game.back",
        .runtime_option_index = 72,
        .runtime_label_string_index = 80,
        .kind = OptionsRowKind::k_back,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
}};
inline constexpr OptionsPageDefinition k_options_game_page{
    .stable_id = "game", .rows = std::span<const OptionsRowDefinition>{k_options_game_rows}};

inline constexpr std::array<OptionsChoiceDefinition, 2> k_options_interpolation_choices{{
    {.runtime_string_index = -1, .raw_value = 0, .literal_label = "Off"},
    {.runtime_string_index = -1, .raw_value = 1, .literal_label = "On"},
}};
inline constexpr std::array<OptionsChoiceDefinition, 3> k_options_transition_style_choices{{
    {.runtime_string_index = -1, .raw_value = 0, .literal_label = "Modern"},
    {.runtime_string_index = -1, .raw_value = 1, .literal_label = "Classic"},
    {.runtime_string_index = -1, .raw_value = 2, .literal_label = "Reduced Motion"},
}};
inline constexpr std::array<OptionsRowDefinition, 4> k_options_enhancements_rows{{
    {.stable_id = "enhancements.title",
        .runtime_option_index = -1,
        .runtime_label_string_index = -1,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = "Enhancements",
        .accent = true},
    {.stable_id = "enhancements.menu_interpolation",
        .runtime_option_index = -1,
        .runtime_label_string_index = -1,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_interpolation_choices},
        .default_choice = 1,
        .literal_label = "Menu animation interpolation",
        .accent = false},
    {.stable_id = "enhancements.menu_transition_style",
        .runtime_option_index = -1,
        .runtime_label_string_index = -1,
        .kind = OptionsRowKind::k_enum,
        .choices = std::span<const OptionsChoiceDefinition>{k_options_transition_style_choices},
        .default_choice = 0,
        .literal_label = "Menu transition style",
        .accent = false},
    {.stable_id = "enhancements.back",
        .runtime_option_index = 72,
        .runtime_label_string_index = 80,
        .kind = OptionsRowKind::k_back,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
}};
inline constexpr OptionsPageDefinition k_options_enhancements_page{.stable_id = "enhancements",
    .rows = std::span<const OptionsRowDefinition>{k_options_enhancements_rows}};

/// Runtime Controls root @ 0x004919E0. Descriptor 20 enters keyboard/mouse
/// mode, 21 is reserved for future Gamepad controls, 22 Mouse Settings and
/// 72 returns to OPTIONS.
inline constexpr std::array<OptionsRowDefinition, 5> k_options_controls_rows{{
    {.stable_id = "controls.title",
        .runtime_option_index = 19,
        .runtime_label_string_index = 28,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = true},
    {.stable_id = "controls.keyboard_mouse",
        .runtime_option_index = 20,
        .runtime_label_string_index = 77,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "controls.gamepad",
        .runtime_option_index = 21,
        .runtime_label_string_index = -1,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = "Gamepad controls",
        .accent = false},
    {.stable_id = "controls.mouse_settings",
        .runtime_option_index = 22,
        .runtime_label_string_index = 69,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "controls.back",
        .runtime_option_index = 72,
        .runtime_label_string_index = 80,
        .kind = OptionsRowKind::k_back,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
}};
inline constexpr OptionsPageDefinition k_options_controls_page{.stable_id = "controls",
    .rows = std::span<const OptionsRowDefinition>{k_options_controls_rows}};

/// Runtime shared device/category state @ 0x004DD640, builder 0x00491BB0.
/// In device mode 1 its title is descriptor 20. The four category descriptors
/// lead to the four type-3 binding pages.
inline constexpr std::array<OptionsRowDefinition, 6> k_options_keyboard_categories_rows{{
    {.stable_id = "controls.keyboard.title",
        .runtime_option_index = 20,
        .runtime_label_string_index = 77,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = true},
    {.stable_id = "controls.keyboard.group0",
        .runtime_option_index = 28,
        .runtime_label_string_index = 35,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "controls.keyboard.group1",
        .runtime_option_index = 39,
        .runtime_label_string_index = 47,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "controls.keyboard.group2",
        .runtime_option_index = 47,
        .runtime_label_string_index = 50,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "controls.keyboard.group3",
        .runtime_option_index = 61,
        .runtime_label_string_index = 55,
        .kind = OptionsRowKind::k_submenu,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
    {.stable_id = "controls.keyboard.back",
        .runtime_option_index = 72,
        .runtime_label_string_index = 80,
        .kind = OptionsRowKind::k_back,
        .choices = {},
        .default_choice = 0,
        .literal_label = {},
        .accent = false},
}};
inline constexpr OptionsPageDefinition k_options_keyboard_categories_page{
    .stable_id = "controls.keyboard.categories",
    .rows = std::span<const OptionsRowDefinition>{k_options_keyboard_categories_rows}};

inline constexpr std::array<OptionsBindingRowDefinition, 10> K_CONTROL_GROUP0_BINDINGS{{
    {"controls.keyboard.group0.slot2", 29, 36, 0, 2},
    {"controls.keyboard.group0.slot3", 30, 37, 0, 3},
    {"controls.keyboard.group0.slot0", 31, 38, 0, 0},
    {"controls.keyboard.group0.slot1", 32, 39, 0, 1},
    {"controls.keyboard.group0.slot10", 33, 79, 0, 10},
    {"controls.keyboard.group0.slot4", 34, 42, 0, 4},
    {"controls.keyboard.group0.slot5", 35, 43, 0, 5},
    {"controls.keyboard.group0.slot7", 36, 44, 0, 7},
    {"controls.keyboard.group0.slot11", 37, 45, 0, 11},
    {"controls.keyboard.group0.slot13", 38, 46, 0, 13},
}};

inline constexpr std::array<OptionsBindingRowDefinition, 7> K_CONTROL_GROUP1_BINDINGS{{
    {"controls.keyboard.group1.slot2", 40, 36, 1, 2},
    {"controls.keyboard.group1.slot3", 41, 37, 1, 3},
    {"controls.keyboard.group1.slot0", 42, 38, 1, 0},
    {"controls.keyboard.group1.slot1", 43, 39, 1, 1},
    {"controls.keyboard.group1.slot4", 44, 42, 1, 4},
    {"controls.keyboard.group1.slot5", 45, 48, 1, 5},
    {"controls.keyboard.group1.slot11", 46, 49, 1, 11},
}};

inline constexpr std::array<OptionsBindingRowDefinition, 13> K_CONTROL_GROUP2_BINDINGS{{
    {"controls.keyboard.group2.slot2", 48, 36, 2, 2},
    {"controls.keyboard.group2.slot3", 49, 37, 2, 3},
    {"controls.keyboard.group2.slot10", 50, 40, 2, 10},
    {"controls.keyboard.group2.slot11", 51, 41, 2, 11},
    {"controls.keyboard.group2.slot4", 52, 51, 2, 4},
    {"controls.keyboard.group2.slot8", 53, 42, 2, 8},
    {"controls.keyboard.group2.slot5", 54, 52, 2, 5},
    {"controls.keyboard.group2.slot6", 55, 53, 2, 6},
    {"controls.keyboard.group2.slot13", 56, 54, 2, 13},
    {"controls.keyboard.group2.slot0", 57, 38, 2, 0},
    {"controls.keyboard.group2.slot1", 58, 39, 2, 1},
    {"controls.keyboard.group2.slot9", 59, 82, 2, 9},
    {"controls.keyboard.group2.slot12", 60, 83, 2, 12},
}};

inline constexpr std::array<OptionsBindingRowDefinition, 10> K_CONTROL_GROUP3_BINDINGS{{
    {"controls.keyboard.group3.slot1", 62, 36, 3, 1},
    {"controls.keyboard.group3.slot0", 63, 37, 3, 0},
    {"controls.keyboard.group3.slot10", 64, 40, 3, 10},
    {"controls.keyboard.group3.slot11", 65, 41, 3, 11},
    {"controls.keyboard.group3.slot2", 66, 52, 3, 2},
    {"controls.keyboard.group3.slot3", 67, 53, 3, 3},
    {"controls.keyboard.group3.slot4", 68, 56, 3, 4},
    {"controls.keyboard.group3.slot5", 69, 57, 3, 5},
    {"controls.keyboard.group3.slot6", 70, 58, 3, 6},
    {"controls.keyboard.group3.slot7", 71, 59, 3, 7},
}};

inline constexpr OptionsBindingPageDefinition k_options_keyboard_group0_page{
    .stable_id = "controls.keyboard.group0",
    .runtime_title_option_index = 28,
    .runtime_title_string_index = 35,
    .group = 0,
    .bindings = std::span<const OptionsBindingRowDefinition>{K_CONTROL_GROUP0_BINDINGS}};
inline constexpr OptionsBindingPageDefinition k_options_keyboard_group1_page{
    .stable_id = "controls.keyboard.group1",
    .runtime_title_option_index = 39,
    .runtime_title_string_index = 47,
    .group = 1,
    .bindings = std::span<const OptionsBindingRowDefinition>{K_CONTROL_GROUP1_BINDINGS}};
inline constexpr OptionsBindingPageDefinition k_options_keyboard_group2_page{
    .stable_id = "controls.keyboard.group2",
    .runtime_title_option_index = 47,
    .runtime_title_string_index = 50,
    .group = 2,
    .bindings = std::span<const OptionsBindingRowDefinition>{K_CONTROL_GROUP2_BINDINGS}};
inline constexpr OptionsBindingPageDefinition k_options_keyboard_group3_page{
    .stable_id = "controls.keyboard.group3",
    .runtime_title_option_index = 61,
    .runtime_title_string_index = 55,
    .group = 3,
    .bindings = std::span<const OptionsBindingRowDefinition>{K_CONTROL_GROUP3_BINDINGS}};

inline constexpr std::int16_t k_options_restore_defaults_option_index{73};
inline constexpr std::int16_t k_options_restore_defaults_string_index{81};

// The six-entry START MENU root page uses Runtime's 52-unit spacing: 120..380.
static_assert(runtime_options_row_step(k_options_root_rows.size()) == 52);
static_assert(runtime_options_row_y(
                  0, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 120);
static_assert(runtime_options_row_y(
                  1, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 172);
static_assert(runtime_options_row_y(
                  2, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 224);
static_assert(runtime_options_row_y(
                  3, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 276);
static_assert(runtime_options_row_y(
                  4, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 328);
static_assert(runtime_options_row_y(
                  5, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 380);

// Nine Video rows use Runtime's 280-unit branch: start 120, step 32.
static_assert(runtime_options_row_step(k_options_video_rows.size()) == 32);
static_assert(runtime_options_row_y(
                  0, k_options_video_rows.size(), OptionsInvocationMode::k_start_menu) == 120);
static_assert(runtime_options_row_y(
                  8, k_options_video_rows.size(), OptionsInvocationMode::k_start_menu) == 376);

// Six Audio rows use a 52-unit step: 120,172,224,276,328,380.
static_assert(runtime_options_row_step(k_options_audio_rows.size()) == 52);
static_assert(runtime_options_row_y(
                  0, k_options_audio_rows.size(), OptionsInvocationMode::k_start_menu) == 120);
static_assert(runtime_options_row_y(
                  5, k_options_audio_rows.size(), OptionsInvocationMode::k_start_menu) == 380);

// Five Game rows share the root page's 65-unit step: 120..380.
static_assert(runtime_options_row_step(k_options_game_rows.size()) == 65);
static_assert(runtime_options_row_y(
                  4, k_options_game_rows.size(), OptionsInvocationMode::k_start_menu) == 380);

static_assert(runtime_options_row_step(k_options_controls_rows.size()) == 65);
static_assert(runtime_options_row_step(k_options_enhancements_rows.size()) == 86);
static_assert(runtime_options_row_step(k_options_keyboard_categories_rows.size()) == 52);

// Binding pages contain title + binding rows + Restore defaults + Back.
static_assert(runtime_options_row_step(K_CONTROL_GROUP0_BINDINGS.size() + 3U) == 21);
static_assert(runtime_options_row_step(K_CONTROL_GROUP1_BINDINGS.size() + 3U) == 28);
static_assert(runtime_options_row_step(K_CONTROL_GROUP2_BINDINGS.size() + 3U) == 18);
static_assert(runtime_options_row_step(K_CONTROL_GROUP3_BINDINGS.size() + 3U) == 21);

}  // namespace App::Interface