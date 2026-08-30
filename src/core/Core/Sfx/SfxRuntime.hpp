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
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/Omikron/SFX.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Sprite/SpriteInstance.hpp"

namespace App::Sfx {

struct SpawnedSprite {
  Sprite::SpriteHandle handle;
  std::size_t frame_count{0};
};

struct StaticEmitterObject {
  std::size_t object_index{0};
  std::string name;
  std::uint32_t flags{0};
};

/// Scenario bridge used by the CPU-only SFX simulation.
class Host {
 public:
  virtual ~Host() = default;
  [[nodiscard]] virtual std::span<const StaticEmitterObject> static_emitter_objects() const = 0;
  [[nodiscard]] virtual std::optional<App::Runtime::Vec3> resolve_static_emitter_world_position(
      std::size_t object_index) const = 0;
  [[nodiscard]] virtual std::expected<std::size_t, std::string> resolve_sfx_sprite_id(
      std::uint16_t authored_sprite_id) const = 0;
  [[nodiscard]] virtual std::expected<SpawnedSprite, std::string> spawn_sfx_sprite(
      std::size_t resource_index, App::Runtime::Vec3 position) = 0;
  [[nodiscard]] virtual Sprite::SpriteInstance* find_sfx_sprite(Sprite::SpriteHandle handle) = 0;
  virtual void destroy_sfx_sprite(Sprite::SpriteHandle handle) = 0;
  [[nodiscard]] virtual std::optional<App::Runtime::Transform> resolve_sfx_character_anchor(
      std::int32_t packed_reference_id) const = 0;
  [[nodiscard]] virtual std::expected<void, std::string> play_sfx_sound(
      std::int32_t authored_h_id, App::Runtime::Vec3 position) = 0;
  [[nodiscard]] virtual std::string_view sfx_scenario_name() const = 0;
  [[nodiscard]] virtual std::string_view sfx_sound_name(std::int32_t authored_h_id) const = 0;
};

enum class EmissionOriginKind : std::uint8_t {
  k_node,
  k_cin_sfx,
  k_static_object,
};

struct EmissionProvenance {
  EmissionOriginKind origin{EmissionOriginKind::k_node};
  std::optional<std::int32_t> node_id;
  std::optional<std::uint16_t> structured_script_trigger_id;
  std::optional<std::uint32_t> animation_id;
  std::string animation_name;
  std::optional<std::uint8_t> cin_channel;
  std::optional<std::size_t> static_object_index;
  std::string static_object_name;
  std::string static_object_prefix;
  std::optional<std::size_t> section_d_record_index;
};

struct SoundStartDiagnostic {
  std::uint64_t logical_tick{0};
  std::string scenario;
  std::int32_t definition_id{0};
  std::string definition_name;
  std::int32_t sound_h_id{0};
  std::string sound_name;
  EmissionProvenance provenance;
  App::Runtime::Vec3 position{};
  std::size_t repeat_count{0};
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
  std::size_t static_emitter_count{0};
  std::size_t active_static_emitter_count{0};
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

  struct StaticEmitterState {
    std::size_t source_record_index{0};
    std::int32_t definition_id{0};
    std::size_t object_index{0};
    std::string object_name;
    float remaining_duration{0.0F};
    float emission_interval{0.0F};
    float interval_phase{0.0F};
    std::size_t emission_count{0};
    std::uint64_t last_emission_tick{0};
    bool active{false};
  };

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
  void emit_definition(
      std::int32_t definition_id, App::Runtime::Vec3 position, EmissionProvenance provenance = {});
  void bind_static_emitters();
  [[nodiscard]] std::size_t static_emitter_count() const;
  [[nodiscard]] const StaticEmitterState& static_emitter_state(std::size_t index) const;
  [[nodiscard]] Diagnostics diagnostics() const;
  [[nodiscard]] std::span<const NodeState> nodes() const;
  [[nodiscard]] std::span<const SoundStartDiagnostic> sound_start_diagnostics() const {
    return m_sound_start_diagnostics;
  }

 private:
  struct EmissionRequest;
  struct Particle;

  Runtime(const Omikron::SfxData& data, Host& host, Random01 random01);
  [[nodiscard]] std::expected<void, std::string> link();
  [[nodiscard]] bool activate_node(std::size_t node_index, std::vector<bool>& activation_stack);
  void service_node(std::size_t node_index);
  void service_requests();
  void service_particles();
  void enqueue_request(const Omikron::SfxDefinition& definition,
      App::Runtime::Vec3 position,
      EmissionProvenance provenance);
  void record_sound_start(const EmissionRequest& request);
  void emit_burst(const Omikron::SfxDefinition& definition, App::Runtime::Vec3 position);
  void create_particle(const Omikron::SfxDefinition& definition, App::Runtime::Vec3 position);
  [[nodiscard]] App::Runtime::Vec3 evaluate_point(
      const Omikron::SfxNode& node, const Omikron::SfxTrackPoint& point) const;
  void evaluate_node_position(std::size_t node_index);
  [[nodiscard]] static float traversal_duration(const Omikron::SfxTrack& track);
  [[nodiscard]] float random01();
  void clear_particles();
  void service_static_emitters();
  [[nodiscard]] std::size_t random_int32();

  const Omikron::SfxData& m_data;
  Host& m_host;
  Random01 m_injected_random;
  std::mt19937 m_generator;
  std::uniform_int_distribution<int> m_runtime_rand{0, 32767};
  std::vector<NodeState> m_nodes;
  std::unordered_map<std::int32_t, std::size_t> m_definition_indices;
  std::vector<const Omikron::SfxDefinition*> m_definitions_by_node;
  std::vector<std::size_t> m_definition_sprite_resources;
  std::vector<EmissionRequest> m_requests;
  std::vector<Particle> m_particles;
  std::vector<StaticEmitterState> m_static_emitters;
  std::vector<SoundStartDiagnostic> m_sound_start_diagnostics;
  std::uint64_t m_logical_tick{0};
  float m_accumulator{0.0F};
  bool m_request_capacity_warned{false};
  bool m_particle_capacity_warned{false};
  bool m_static_emitter_capacity_warned{false};
  bool m_acceleration_mode_warned{false};
};

}  // namespace App::Sfx
