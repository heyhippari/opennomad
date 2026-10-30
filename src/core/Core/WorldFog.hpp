#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace App {

class Shader;

namespace Fog {

/// Runtime day length in clock units (0x004C2C0C).
inline constexpr std::int64_t K_DAY_LENGTH{3'600'000};
/// Four equal day phases (0x004C2C10 = day length / 4).
inline constexpr std::int64_t K_DAY_PHASE_LENGTH{K_DAY_LENGTH / 4};
/// Clock written when a new game starts (0x0040E12A / 0x0040E252).
inline constexpr std::int64_t K_NEW_GAME_CLOCK{2'000'000};
/// 0x0041E600 adds 0xA6 to the clock once its frame accumulator exceeds 5.
inline constexpr std::int64_t K_CLOCK_STEP{0xA6};
inline constexpr float K_FRAMES_PER_CLOCK_STEP{5.0F};
/// Nominal engine-frame rate driving the clock accumulator.
inline constexpr float K_ENGINE_FRAMES_PER_SECOND{30.0F};

/// Runtime 0x00440BE0: linear table fog starts at a quarter of the distance.
inline constexpr float K_FOG_START_FRACTION{0.25F};
/// Runtime 0x00440BE0: the far alpha fade starts at 95 % of the distance.
inline constexpr float K_FAR_FADE_START_FRACTION{0.95F};
/// Metres to inches (0x004BC198).
inline constexpr double K_INCHES_PER_METRE{39.37007874015748};
/// Bucket 0x0800 (3DO flag bit 16) doubles the fog range: the D3D path
/// doubles start/end, the CPU path halves the depth.
inline constexpr float K_DOUBLED_RANGE_DEPTH_SCALE{0.5F};

/// Runtime's game clock (0x004C2BD0), advanced by the engine frame.
class DayClock {
 public:
  /// Advances by real seconds expressed as nominal 30 Hz engine frames.
  void advance(float delta_seconds);
  /// One engine frame of 0x0041E600.
  void tick();

  void set(std::int64_t value);
  [[nodiscard]] std::int64_t value() const {
    return m_value;
  }

 private:
  std::int64_t m_value{K_NEW_GAME_CLOCK};
  float m_accumulator{0.0F};
  float m_pending_frames{0.0F};
};

/// One resident AREA's environment entry (Runtime table 0x009103E0, stride 0x84).
struct Environment {
  std::array<std::uint32_t, 4> colors{};  ///< 0x00RRGGBB per day phase.
  std::int32_t distance_metres{0};        ///< 0 selects the Clip Distance preference.
  /// High word of AREA +0xB0: this scene drives the clear colour (0x0090EFB0).
  bool day_cycle_owner{false};
};

/// Evaluated fog of one render context.
struct Parameters {
  std::uint32_t color_rgb{0};     ///< Context +0x150, packed 0x00RRGGBB.
  float start_inches{0.0F};       ///< Context +0x148.
  float end_inches{0.0F};         ///< Context +0x154; also the far-cull distance.
  float fade_start_inches{0.0F};  ///< Context +0x14C, far alpha-fade start.
};

/// Runtime 0x0041E7A0 colour: linear interpolation of each byte between the
/// current phase colour and the next, truncated like `_ftol`.
[[nodiscard]] std::uint32_t day_color(
    const std::array<std::uint32_t, 4>& colors, std::int64_t clock);

/// Runtime 0x0041D4D0: zero or a value above the preference selects the preference.
[[nodiscard]] std::int32_t effective_distance_metres(
    std::int32_t area_metres, std::int32_t clip_metres);

[[nodiscard]] Parameters evaluate(
    const Environment& environment, std::int64_t clock, std::int32_t clip_metres);

/// Packed Runtime colour as encoded (display-space) RGB in [0, 1].
[[nodiscard]] std::array<float, 3> encoded_color(std::uint32_t color_rgb);

/// Uniform state shared by every fogged world shader.
struct ShaderFog {
  bool enabled{false};
  bool radial{true};  ///< Euclidean eye distance instead of Runtime's planar eye depth.
  Parameters parameters{};
};

/// Writes `u_fog_*` uniforms on the bound shader.
void apply_uniforms(const Shader& shader, const ShaderFog& fog, float depth_scale = 1.0F);

/// Writes `u_far_fade_*` uniforms (blended surfaces) on the bound shader.
/// `fog.enabled` is ignored: Runtime fades blended buckets it never fogs.
void apply_far_fade_uniforms(const Shader& shader, const ShaderFog& fog, float depth_scale = 1.0F);

}  // namespace Fog

}  // namespace App
