#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace App::Startup {

/// Which startup video slot a path belongs to.
enum class StartupVideoSlot : std::uint8_t {
  k_publisher,
  k_developer,
  k_intro,
};

/// Number of startup video slots.
inline constexpr std::size_t k_startup_video_count{3};

/// Media policy controlling the startup-video phases. There is deliberately
/// no command-line parsing in this milestone; callers configure this struct
/// directly (tests use it to exercise the SkippedByConfiguration status).
struct StartupMediaPolicy {
  bool videos_enabled{true};
  std::array<std::string, k_startup_video_count> video_paths{
      "FLIS/EIDOS.mpg", "FLIS/QUANTIC.mpg", "FLIS/GAME.mpg"};
};

/// Trace-event base name of a startup video slot ("StartupVideo.Eidos", ...).
[[nodiscard]] constexpr std::string_view startup_video_event_base(const StartupVideoSlot slot) {
  switch (slot) {
    case StartupVideoSlot::k_publisher:
      return "StartupVideo.Eidos";
    case StartupVideoSlot::k_developer:
      return "StartupVideo.QuanticDream";
    case StartupVideoSlot::k_intro:
      return "StartupVideo.GameIntro";
  }
  return "StartupVideo.Unknown";
}

}  // namespace App::Startup
