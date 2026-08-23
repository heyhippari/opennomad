#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/RuntimeMath.hpp"

namespace App::Audio {
class AudioSystem;
}
namespace App::Character {
class Runtime;
}
namespace App::Dialog {
class DialogRuntime;
}
namespace App::Omikron {
class ThreeDM;
struct Model3DOData;
}

namespace App::Dialog {

struct ThreeDmBinding {
  std::vector<std::size_t> object_mesh_indices;
  std::size_t root_object_slot{0};
  std::size_t root_mesh_index{0};
  std::optional<std::size_t> face_mesh_index;
  std::string face_diagnostic;
};

[[nodiscard]] std::expected<ThreeDmBinding, std::string> bind_three_dm(
    const Omikron::ThreeDM& clip, const Omikron::Model3DOData& model);

/// Authored 30 Hz held-sample clock. Frame zero is available immediately.
class DialogPerformanceClock {
 public:
  void start(std::size_t frame_count);
  void reset();
  [[nodiscard]] std::optional<std::size_t> advance(float real_delta_seconds);
  [[nodiscard]] bool active() const {
    return m_active;
  }

 private:
  std::size_t m_frame_count{0};
  double m_elapsed_seconds{0.0};
  bool m_active{false};
};

/// Session-level coordinator for a DialogRuntime generation's synchronized
/// object, face and embedded voice performance.
class DialogPerformanceRuntime {
 public:
  using ClipLoader = std::function<std::expected<std::shared_ptr<const Omikron::ThreeDM>,
      std::string>(std::string_view basename)>;

  DialogPerformanceRuntime();
  explicit DialogPerformanceRuntime(ClipLoader loader);

  void tick(float real_delta_seconds,
      const DialogRuntime& dialog,
      Character::Runtime* characters,
      std::uint64_t world_identity,
      Audio::AudioSystem* audio);
  void reset();
  void stop_for_world_change();
  [[nodiscard]] bool active() const {
    return m_clock.active();
  }
  [[nodiscard]] std::optional<std::size_t> current_frame() const {
    return m_current_frame;
  }

 private:
  [[nodiscard]] static std::expected<std::shared_ptr<const Omikron::ThreeDM>, std::string>
  load_clip(std::string_view basename);
  void stop(bool stop_voice);
  void start_generation(const DialogRuntime& dialog,
      Character::Runtime* characters,
      std::uint64_t world_identity,
      Audio::AudioSystem* audio);
  void apply_frame(std::size_t frame_index);

  ClipLoader m_loader;
  std::unordered_map<std::string, std::shared_ptr<const Omikron::ThreeDM>> m_cache;
  std::shared_ptr<const Omikron::ThreeDM> m_clip;
  std::optional<ThreeDmBinding> m_binding;
  Character::Runtime* m_characters{nullptr};
  Audio::AudioSystem* m_audio{nullptr};
  std::int16_t m_character_id{0};
  std::uint64_t m_world_identity{0};
  std::uint64_t m_generation{0};
  Runtime::Vec3 m_root_origin{};
  DialogPerformanceClock m_clock;
  std::optional<std::size_t> m_current_frame;
};

}  // namespace App::Dialog
