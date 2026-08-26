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

/// One declarative OPTIONS row.
///
/// runtime_string_index >= 0 resolves through IAM/Options. A negative index is
/// reserved for OpenNomad-only settings and uses literal_label instead. This
/// keeps recovered Runtime provenance separate from future enhancement rows.
struct OptionsRowDefinition {
  std::string_view stable_id;
  std::int16_t runtime_string_index{-1};
  OptionsRowKind kind{OptionsRowKind::k_submenu};
  std::string_view literal_label;
};

/// A page is just a stable ID plus an ordered row span. Runtime used a fixed
/// 16-row scratch pool; OpenNomad deliberately does not preserve that limit.
/// Later phases can register additional pages or append OpenNomad-only rows
/// without changing the navigation/layout machinery.
struct OptionsPageDefinition {
  std::string_view stable_id;
  std::span<const OptionsRowDefinition> rows;
};

inline constexpr std::uint16_t k_options_interface_id{35};
inline constexpr char k_options_root_font_key{'S'};
inline constexpr std::uint32_t k_options_text_flags{0x80000010U};
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
[[nodiscard]] constexpr int runtime_options_row_y(const std::size_t row_index,
    const std::size_t active_count,
    const OptionsInvocationMode mode) {
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
/// stable_id is OpenNomad metadata and is deliberately independent of the IAM
/// string index. Later pages/settings can therefore use stable configuration
/// keys even when they are OpenNomad-only.
inline constexpr std::array<OptionsRowDefinition, 5> k_options_root_rows{{
    {"video", 1, OptionsRowKind::k_submenu, {}},
    {"audio", 9, OptionsRowKind::k_submenu, {}},
    {"game", 14, OptionsRowKind::k_submenu, {}},
    {"controls", 19, OptionsRowKind::k_submenu, {}},
    {"back", 72, OptionsRowKind::k_back, {}},
}};
inline constexpr OptionsPageDefinition k_options_root_page{
    .stable_id = "root", .rows = std::span<const OptionsRowDefinition>{k_options_root_rows}};

// The five-entry START MENU root page is the easiest sanity check of the
// recovered layout helper: 120 + N*65.
static_assert(runtime_options_row_step(k_options_root_rows.size()) == 65);
static_assert(runtime_options_row_y(
                  0, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 120);
static_assert(runtime_options_row_y(
                  1, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 185);
static_assert(runtime_options_row_y(
                  2, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 250);
static_assert(runtime_options_row_y(
                  3, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 315);
static_assert(runtime_options_row_y(
                  4, k_options_root_rows.size(), OptionsInvocationMode::k_start_menu) == 380);

}  // namespace App::Interface