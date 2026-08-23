#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "Core/Omikron/SFX.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Sprite/SpriteInstance.hpp"

namespace App::Sfx {

struct SpawnedSprite {
  Sprite::SpriteHandle handle;
  std::size_t frame_count{0};
};

/// Scenario bridge used by the CPU-only SFX simulation.
class Host {
 public:
  virtual ~Host() = default;
  [[nodiscard]] virtual std::expected<std::size_t, std::string> resolve_sfx_sprite_id(
      std::uint16_t authored_sprite_id) const = 0;
  [[nodiscard]] virtual std::expected<SpawnedSprite, std::string> spawn_sfx_sprite(
      std::size_t resource_index, App::Runtime::Vec3 position) = 0;
  [[nodiscard]] virtual Sprite::SpriteInstance* find_sfx_sprite(Sprite::SpriteHandle handle) = 0;
  virtual void destroy_sfx_sprite(Sprite::SpriteHandle handle) = 0;
  [[nodiscard]] virtual std::optional<App::Runtime::Transform> resolve_sfx_character_anchor(
      std::int32_t packed_reference_id) const = 0;
};

struct NodeState {
  std::uint32_t flags{0};
  float elapsed{0.0F};
  float track_duration{0.0F};
  std::int32_t repeat_index{0};
  std::size_t current_point_index{0};
  App::Runtime::Vec3 current_position{};

  [[nodiscard]] bool active() const {
    return (flags & 0x01U) != 0U;
  }
  [[nodiscard]] bool current_reverse() const {
    return (flags & 0x08U) != 0U;
  }
  [[nodiscard]] bool delaying() const {
    return (flags & 0x20U) != 0U;
  }
};

struct Diagnostics {
  bool loaded{false};
  std::size_t definition_count{0};
  std::size_t node_count{0};
  std::size_t track_count{0};
  std::size_t active_node_count{0};
  std::size_t queued_request_count{0};
  std::size_t active_particle_count{0};
  std::size_t attached_sprite_count{0};
};

/// Per-scenario mutable retail SFX emitter/request/particle simulation.
class Runtime {
 public:
  using Random01 = std::function<float()>;
  static constexpr std::size_t k_request_capacity{100U};
  static constexpr std::size_t k_particle_capacity{1000U};
  static constexpr float k_fixed_step_seconds{1.0F / 30.0F};

  [[nodiscard]] static std::expected<std::unique_ptr<Runtime>, std::string> create(
      const Omikron::SfxData& data, Host& host, Random01 random01 = {});
  ~Runtime();

  Runtime(const Runtime&) = delete;
  Runtime(Runtime&&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime& operator=(Runtime&&) = delete;

  void tick(float real_delta_seconds);
  void step();
  [[nodiscard]] std::size_t trigger(std::int32_t type, std::int32_t id);
  [[nodiscard]] Diagnostics diagnostics() const;
  [[nodiscard]] std::span<const NodeState> nodes() const;

 private:
  struct EmissionRequest;
  struct Particle;

  Runtime(const Omikron::SfxData& data, Host& host, Random01 random01);
  [[nodiscard]] std::expected<void, std::string> link();
  [[nodiscard]] bool activate_node(std::size_t node_index, std::vector<bool>& activation_stack);
  void service_node(std::size_t node_index);
  void service_requests();
  void service_particles();
  void enqueue_request(const Omikron::SfxDefinition& definition, App::Runtime::Vec3 position);
  void emit_burst(const Omikron::SfxDefinition& definition, App::Runtime::Vec3 position);
  void create_particle(const Omikron::SfxDefinition& definition, App::Runtime::Vec3 position);
  [[nodiscard]] App::Runtime::Vec3 evaluate_point(
      const Omikron::SfxNode& node, const Omikron::SfxTrackPoint& point) const;
  void evaluate_node_position(std::size_t node_index);
  [[nodiscard]] static float traversal_duration(const Omikron::SfxTrack& track);
  [[nodiscard]] float random01();
  void clear_particles();

  const Omikron::SfxData& m_data;
  Host& m_host;
  Random01 m_injected_random;
  std::mt19937 m_generator;
  std::uniform_int_distribution<int> m_runtime_rand{0, 32767};
  std::vector<NodeState> m_nodes;
  std::vector<const Omikron::SfxDefinition*> m_definitions_by_node;
  std::vector<std::size_t> m_definition_sprite_resources;
  std::vector<EmissionRequest> m_requests;
  std::vector<Particle> m_particles;
  float m_accumulator{0.0F};
  bool m_request_capacity_warned{false};
  bool m_particle_capacity_warned{false};
  bool m_sound_unsupported_warned{false};
  bool m_acceleration_mode_warned{false};
};

}  // namespace App::Sfx
