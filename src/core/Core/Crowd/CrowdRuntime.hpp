#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Omikron/AnimationBankAni.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/TrajectoryOpt.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Crowd {

/// Passerby slots (Runtime pool 0x008F5E94, 200 x 0x48 bytes).
inline constexpr std::size_t K_MAX_PEDESTRIANS{200};
/// Slider slots (Runtime pool 0x008F5E3C, 40 x 0x18 bytes).
inline constexpr std::size_t K_MAX_SLIDERS{40};
/// Instanced level-of-detail roots per prototype (0x00453A70).
inline constexpr std::size_t K_MAX_LEVELS_OF_DETAIL{4};
/// Passerby LOD switch depths 10/20/30/40 m (0x004C8870), in inches.
inline constexpr std::array<float, K_MAX_LEVELS_OF_DETAIL> K_PASSER_LOD_DEPTHS{
    393.700775F, 787.401550F, 1181.102417F, 1574.803101F};
/// Slider LOD switch depths 20/30/40/50 m (0x004C8860), in inches.
inline constexpr std::array<float, K_MAX_LEVELS_OF_DETAIL> K_SLIDER_LOD_DEPTHS{
    787.401550F, 1181.102417F, 1574.803101F, 1968.503906F};
/// Default Crowd Density (0x0090E726) and Detail Level (0x0090E727).
inline constexpr std::uint8_t K_DEFAULT_STREET_ACTIVITY{3};
inline constexpr std::uint8_t K_DEFAULT_DETAIL_LEVEL{1};

/// Runtime renderer 0x0048D7F0 LOD choice. The chain holds levels
/// [first_level, level_count); chain record i switches at switch_depths[i].
/// Skip `1 - detail_level` leading records, then advance while the record's
/// switch depth is <= the camera-space depth. The last record is never culled
/// by depth. Returns the absolute level index.
[[nodiscard]] std::size_t select_level_of_detail(std::size_t level_count,
    std::size_t first_level,
    std::span<const float> switch_depths,
    std::uint8_t detail_level,
    float camera_depth);

/// What an agent is: a walking passerby or a slider-lane vehicle.
enum class AgentKind : std::uint8_t {
  k_passer,
  k_slider,
};

/// One passerby or vehicle model variant with its level-of-detail roots.
struct Prototype {
  AgentKind kind{AgentKind::k_passer};
  std::shared_ptr<const Character::ModelResource> resource;
  /// Parentless objects ordered by descending own polygon count (0x00453A70).
  std::vector<std::size_t> level_roots;
  /// Preorder object indices of each level's subtree. Every level carries the
  /// same skeleton; animation channel IDs address level 0's script IDs.
  std::vector<std::vector<std::size_t>> level_objects;
  /// Pose-independent geometry layout of each level's subtree.
  std::vector<Omikron::PosedGeometryLayout> level_layouts;
  /// First level of the renderer's chain (0x00453910): 1 for the slider
  /// model, whose level 0 is the detailed boarding hull.
  std::size_t first_level{0};
  /// Switch depth of each chain record.
  std::array<float, K_MAX_LEVELS_OF_DETAIL> switch_depths{K_PASSER_LOD_DEPTHS};
  /// Level-0 root bounding radius (descriptor +0x58), slot +0x1C.
  float root_radius{0.0F};
  /// ANI character type: 1 male, 2 female passerby.
  std::int32_t character_type{0};
  /// Remaining allocations before the next variant is used: an equal share of
  /// 100 for passersby (0x00453E80), the table weight 1 for vehicles.
  std::int32_t remaining_share{0};
};

/// Live state of one idle activity at an OPT stop point (0x00539928, 0x30).
struct Activity {
  std::size_t stop_point{0};
  App::Runtime::Vec3 target{};
  float yaw_degrees{0.0F};
  const Omikron::AniAnimation* begin{nullptr};
  const Omikron::AniAnimation* loop{nullptr};
  const Omikron::AniAnimation* end{nullptr};
  /// 0 walking to the point, 1 begin, 2 loop, 3 end.
  std::int32_t state{0};
  std::int16_t loops_remaining{0};
  std::int16_t saved_animation_id{0};
};

/// Runtime node +0xB4 flags.
inline constexpr std::uint32_t K_FLAG_BLOCKED{0x001};
inline constexpr std::uint32_t K_FLAG_ON_NETWORK{0x008};
inline constexpr std::uint32_t K_FLAG_IN_JUNCTION{0x010};
inline constexpr std::uint32_t K_FLAG_SIDE_STEP{0x020};
inline constexpr std::uint32_t K_FLAG_ACTIVITY{0x080};
inline constexpr std::uint32_t K_FLAG_WAITING{0x100};

/// One passerby (Runtime's 0x48-byte slot merged with its 0xC0-byte node) or
/// one slider (0x18-byte slot plus node).
struct Agent {
  AgentKind kind{AgentKind::k_passer};
  std::size_t prototype{0};
  std::int32_t character_type{0};
  const Omikron::AniAnimation* animation{nullptr};
  float progress{1.0F};            ///< Slot +0x0C.
  float frame_count{0.0F};         ///< Slot +0x10.
  float bob{0.0F};                 ///< Slot +0x18, accumulated root-motion Y.
  float ground_offset{0.0F};       ///< Slot +0x1C.
  App::Runtime::Vec3 side_step{};  ///< Slot +0x20/+0x24.
  float side_step_remaining{0.0F};
  float playback_rate{1.0F};  ///< Slot +0x38.

  /// Leading point ("carrot") that advances along the network (+0x00).
  App::Runtime::Vec3 target{};
  App::Runtime::Vec3 segment_start{};  ///< +0x0C.
  App::Runtime::Vec3 direction{};      ///< +0x18.
  /// Walker position on the ground (+0x24).
  App::Runtime::Vec3 position{};
  float remaining{0.0F};                  ///< +0x30, segment length left x256.
  float nominal_speed{0.0F};              ///< +0x34, x256 per Runtime tick.
  float speed{0.0F};                      ///< +0x38.
  float radius{0.0F};                     ///< +0x3C.
  std::size_t list{0};                    ///< Occupancy list containing this pedestrian.
  std::optional<std::size_t> neighbour;   ///< +0x44.
  std::size_t trajectory{0};              ///< +0x48.
  std::size_t route{0};                   ///< +0x4C.
  App::Runtime::Matrix3 facing{};         ///< +0x8C.
  std::optional<std::size_t> stop_point;  ///< +0xB0 before an activity starts.
  std::optional<std::size_t> activity;    ///< +0xB0.
  std::uint32_t flags{0};                 ///< +0xB4.
  /// +0xBA: 1-based segment on a trajectory, or the step inside a junction.
  std::int32_t segment{0};
  /// Instance +0x10: the rendered level-of-detail root position.
  App::Runtime::Vec3 instance{};
  /// Slider slot +0x0C: top speed, x256 per tick.
  float max_speed{0.0F};
  /// Slider slot +0x16: the boardable slider (slot 0).
  bool boardable{false};
};

/// Everything Runtime 0x00453450 needs to populate an AREA's crowd.
struct PopulationRequest {
  std::shared_ptr<const Omikron::TrajectoryOpt> network;
  std::shared_ptr<const Omikron::AnimationBankAni> animations;
  /// Enabled male/female passerby models in Runtime table order.
  std::vector<std::shared_ptr<const Character::ModelResource>> male_models;
  std::vector<std::shared_ptr<const Character::ModelResource>> female_models;
  /// Enabled slider (`sli_fn`, AREA +0xAC with bit 0 forced) and `moto`
  /// (AREA +0xAE) models in Runtime table order.
  std::vector<std::shared_ptr<const Character::ModelResource>> slider_models;
  std::vector<std::shared_ptr<const Character::ModelResource>> moto_models;
  std::uint8_t street_activity{K_DEFAULT_STREET_ACTIVITY};
};

/// Runtime passerby crowd of one world context (0x00452000..0x00458800).
/// Runtime keeps a single crowd for the most recently loaded AREA trajectory
/// network: passersby walk its passerby trajectories and sliders (flying
/// vehicles) fly its slider lanes, sharing occupancy lists and crossings.
class Runtime {
 public:
  /// Replaces the crowd and spawns passersby along the network.
  [[nodiscard]] std::expected<void, std::string> populate(PopulationRequest request);
  /// Releases every passerby (0x004548C0).
  void clear();

  /// Advances every passerby, then every slider, by `delta_units` Runtime
  /// ticks (0x00454BB0).
  void tick(float delta_units);

  [[nodiscard]] bool empty() const {
    return m_agents.empty();
  }
  [[nodiscard]] std::span<const Agent> agents() const {
    return m_agents;
  }
  [[nodiscard]] std::span<const Prototype> prototypes() const {
    return m_prototypes;
  }
  [[nodiscard]] const Omikron::AnimationBankAni* animations() const {
    return m_animations.get();
  }
  [[nodiscard]] const Omikron::Animation3DA* animation(const Agent& pedestrian) const;
  /// Bumped on every populate()/clear(); agent indices are stable within one.
  [[nodiscard]] std::uint32_t generation() const {
    return m_generation;
  }

 private:
  [[nodiscard]] std::int32_t next_random();
  [[nodiscard]] std::optional<std::size_t> list_ahead(std::size_t pedestrian) const;
  [[nodiscard]] std::uint32_t next_counter();
  [[nodiscard]] const Omikron::AniCharacter* ani_character(std::int32_t character_type) const;
  [[nodiscard]] const Omikron::AniAnimation* random_animation(
      std::int32_t character_type, Omikron::AniAnimationKind kind);
  void select_animation(Agent& pedestrian, const Omikron::AniAnimation* animation) const;
  [[nodiscard]] App::Runtime::Vec3 root_motion(
      const Agent& pedestrian, float previous, float current) const;
  [[nodiscard]] float root_speed(const Agent& pedestrian) const;

  void spawn(std::uint8_t street_activity);
  void spawn_sliders();
  void spawn_along(std::int32_t begin, std::int32_t end, float spacing, bool sliders);
  [[nodiscard]] std::optional<std::size_t> allocate();
  [[nodiscard]] std::optional<std::size_t> allocate_slider();
  [[nodiscard]] std::size_t route_for(std::size_t trajectory);

  [[nodiscard]] static std::size_t trajectory_list(std::size_t trajectory);
  [[nodiscard]] std::size_t segment_list(std::size_t segment) const;
  [[nodiscard]] std::size_t route_list(std::size_t route) const;
  [[nodiscard]] std::optional<std::size_t> list_head(std::size_t list) const;
  void move_to_list(std::size_t pedestrian, std::size_t list);
  void swap_in_list(std::size_t first, std::size_t second);

  [[nodiscard]] bool crossing_transition(
      std::size_t route, std::int32_t release, std::int32_t acquire);
  [[nodiscard]] std::int16_t step_crossing(std::size_t route, std::int32_t step) const;

  void advance_target(std::size_t index, float delta_units);
  /// Records `other` as the neighbour ahead and blocks when the candidate
  /// target would come within both radii of its target (0x00455230).
  [[nodiscard]] bool test_neighbour(
      std::size_t index, const App::Runtime::Vec3& candidate, std::size_t other);
  [[nodiscard]] bool scan_segments(std::size_t index,
      const App::Runtime::Vec3& candidate,
      std::size_t trajectory,
      std::int32_t segment);
  [[nodiscard]] std::int32_t change_segment(
      std::size_t index, std::size_t trajectory, std::uint32_t new_flags, std::int32_t segment);
  void walk(std::size_t index, float delta_units);
  void start_activity(Agent& pedestrian);
  void perform_activity(Agent& pedestrian, float delta_units);
  [[nodiscard]] bool play_in_place(Agent& pedestrian, float delta_units, App::Runtime::Vec3& delta);
  void advance_activity(Agent& pedestrian, std::size_t slot, Activity& activity);
  [[nodiscard]] static std::int32_t steer(Agent& agent,
      const App::Runtime::Vec3& to_target,
      float distance,
      float near_distance,
      float far_distance);
  void drive(std::size_t index, float delta_units);

  std::shared_ptr<const Omikron::TrajectoryOpt> m_network;
  std::shared_ptr<const Omikron::AnimationBankAni> m_animations;
  std::vector<Prototype> m_prototypes;
  std::size_t m_male_prototype_count{0};
  std::size_t m_passer_prototype_count{0};
  std::size_t m_slider_prototype_count{0};
  std::size_t m_slider_count{0};
  std::uint32_t m_generation{0};
  std::vector<Agent> m_agents;
  /// Occupancy lists, newest entrant first: trajectories, then segments, then
  /// routes. The agent after an entry is the one ahead of it.
  std::vector<std::vector<std::size_t>> m_lists;
  /// Live crossing lock counters (OPT crossing +0x03).
  std::vector<std::int8_t> m_crossing_locks;
  /// Live stop-point animation IDs (OPT stop point +0x10).
  std::vector<std::int16_t> m_stop_point_ids;
  /// Activity pool sized by the stop-point count (0x00539928).
  std::vector<std::optional<Activity>> m_activities;
  /// Effective route counts; Runtime rewrites a zero count to one.
  std::vector<std::int8_t> m_route_counts;
  std::uint32_t m_random_state{1};
  /// Runtime 0x004C8854 shared route/stop-point counter.
  std::uint32_t m_counter{0};
};

/// Loads ANIMS/<ani>.ANI, TRAJECTOIRES/<opt>.OPT and the AREA's enabled
/// passerby and vehicle models. Returns nullopt when the AREA has no
/// trajectory network.
[[nodiscard]] std::expected<std::optional<PopulationRequest>, std::string> load_area_population(
    const Omikron::IamAreaRecord& area,
    std::uint8_t street_activity,
    const Character::Runtime::ModelLoader& model_loader);

}  // namespace App::Crowd
