#include "Core/Crowd/CrowdRuntime.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Character/CharacterRuntime.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/GameDataLoader.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/Animation3DA.hpp"
#include "Core/Omikron/AnimationBankAni.hpp"
#include "Core/Omikron/IamArea.hpp"
#include "Core/Omikron/Model3DO.hpp"
#include "Core/Omikron/TrajectoryOpt.hpp"
#include "Core/RuntimeMath.hpp"

namespace App::Crowd {

namespace {

using App::Runtime::Matrix3;
using App::Runtime::Vec3;

/// Runtime's built-in passerby model tables (0x004C25B8 male, 0x004C2738
/// female, 12-byte entries, all with weight 1). AREA +0xA4/+0xA8 enable them.
constexpr std::array<std::string_view, 12> K_MALE_PASSER_MODELS{"PSH_FN",
    "CMH_FN",
    "KSH_FN",
    "PVH_FN",
    "AMH_FN",
    "CEM_FN",
    "KHO_FN",
    "TEH_FN",
    "YES_FN",
    "PSH1_FN",
    "KSH1_FN",
    "PVH1_FN"};
constexpr std::array<std::string_view, 9> K_FEMALE_PASSER_MODELS{
    "FSH_FN", "CWH_FN", "KWH_FN", "VFH_FN", "AN1_FN", "KIL2_FN", "FSH1_FN", "KWH1_FN", "VFH1_FN"};

/// Vehicle tables (0x004C2A38 `sli_fn` x2, 0x004C28B8 `moto`), weight 1.
constexpr std::array<std::string_view, 2> K_SLIDER_MODELS{"sli_fn", "sli_fn"};
constexpr std::array<std::string_view, 1> K_MOTO_MODELS{"moto"};

constexpr std::int32_t K_MALE_PASSER{1};
constexpr std::int32_t K_FEMALE_PASSER{2};
/// 0x00453E80 distributes this many allocations across a sex's variants.
constexpr std::int32_t K_PROTOTYPE_SHARES{100};
/// Spawn spacing unit: one metre in inches (0x00453B40 multiplies by 39).
constexpr float K_SPACING_UNIT{39.0F};
/// A new walker starts this far behind its leading point (0x004BC570).
constexpr float K_SPAWN_LEAD{117.0F};
/// Segment lengths and speeds are stored x256 (0x004BC59C).
constexpr float K_FIXED_SCALE{256.0F};
/// Steering bands of 0x00455D10 (0x004C8880): walker near/far, then the
/// slider bands at 0x004C8888.
constexpr float K_STEER_NEAR{19.5F};
constexpr float K_STEER_FAR{58.5F};
constexpr float K_SLIDER_STEER_NEAR{195.0F};
constexpr float K_SLIDER_STEER_FAR{390.0F};
/// Slider top speed (slot +0x0C), braking per tick when blocked (0x004BC5D8)
/// and the hull's height above its lane (0x004BC5A0).
constexpr float K_SLIDER_MAX_SPEED{5120.0F};
constexpr float K_SLIDER_BRAKING{768.0F};
constexpr float K_SLIDER_HOVER{30.75F};
/// `sli_fn` objects are flagged environment-mapped (0x00453A70 flag 0x400 ->
/// object flag 0x04000000 via 0x00437220).
constexpr std::uint32_t K_SLIDER_MESH_FLAGS{0x04000000U};
/// Leading point closer than this stops steering (0x004BC5B8, x256).
constexpr float K_STEER_MINIMUM{512.0F};
/// Passing distance of the overtaking side-step (0x004BC598).
constexpr float K_SIDE_STEP_DISTANCE{39.0F};
constexpr float K_CROWD_DENSITY_LEVELS{5.0F};
constexpr std::uint32_t K_FLAG_OCCUPANCY_IGNORED{0x280};
constexpr std::uint32_t K_COUNTER_MASK{0x7FFFFFFFU};
constexpr float K_DEGREES_TO_RADIANS{0.01745329251994329577F};

/// `base + offset` as a table index; callers only pass non-negative sums.
[[nodiscard]] std::size_t table_index(const std::int32_t base, const std::int32_t offset) {
  const std::int32_t index{base + offset};
  return static_cast<std::size_t>(index);
}

[[nodiscard]] float length(const Vec3& vector) {
  return std::sqrt((vector.x * vector.x) + (vector.y * vector.y) + (vector.z * vector.z));
}

[[nodiscard]] Vec3 add(const Vec3& first, const Vec3& second) {
  return Vec3{.x = first.x + second.x, .y = first.y + second.y, .z = first.z + second.z};
}

[[nodiscard]] Vec3 scaled(const Vec3& vector, const float factor) {
  return Vec3{.x = vector.x * factor, .y = vector.y * factor, .z = vector.z * factor};
}

[[nodiscard]] float distance_squared(const Vec3& first, const Vec3& second) {
  const float dx{first.x - second.x};
  const float dy{first.y - second.y};
  const float dz{first.z - second.z};
  return (dx * dx) + (dy * dy) + (dz * dz);
}

/// Runtime 0x00453330: a basis whose model -Z axis faces (x, z), pitched by
/// y over `distance` when y is nonzero. Runtime divides the last element by
/// the pitch cosine (an `fdiv`, so not exactly orthonormal); it is kept.
[[nodiscard]] Matrix3 facing_toward(
    const float x, const float y, const float z, const float distance) {
  float horizontal{0.0F};
  float along_x{0.0F};
  float along_z{1.0F};
  if (x != 0.0F || z != 0.0F) {
    horizontal = std::sqrt((x * x) + (z * z));
    along_x = x / horizontal;
    along_z = -(z / horizontal);
  }
  float pitch_sine{0.0F};
  float pitch_cosine{1.0F};
  if (y != 0.0F && distance != 0.0F) {
    pitch_sine = y / distance;
    pitch_cosine = horizontal / distance;
  }
  const float last{pitch_cosine != 0.0F ? along_z / pitch_cosine : along_z};
  return Matrix3{{along_z,
      0.0F,
      along_x,
      -(along_x * pitch_sine),
      pitch_cosine,
      along_z * pitch_sine,
      -(along_x * pitch_cosine),
      -pitch_sine,
      last}};
}

/// Yaw-only form used by walkers.
[[nodiscard]] Matrix3 facing_toward(const float x, const float z) {
  return facing_toward(x, 0.0F, z, 0.0F);
}

/// Runtime 0x00453A70: up to four parentless objects, selection-sorted by
/// descending own polygon count with Runtime's swap order.
[[nodiscard]] std::vector<std::size_t> level_of_detail_roots(const Omikron::Model3DOData& model) {
  std::vector<std::size_t> roots{Omikron::Model3DO::parentless_objects(model)};
  if (roots.size() > K_MAX_LEVELS_OF_DETAIL) {
    roots.resize(K_MAX_LEVELS_OF_DETAIL);
  }
  const auto polygons = [&model](const std::size_t index) {
    const Omikron::MeshDescriptor& mesh{model.meshes.at(index)};
    return mesh.triangle_count + mesh.rectangle_count;
  };
  for (std::size_t first{0}; first + 1U < roots.size(); ++first) {
    std::uint32_t best{polygons(roots.at(first))};
    for (std::size_t other{first + 1U}; other < roots.size(); ++other) {
      const std::uint32_t candidate{polygons(roots.at(other))};
      if (candidate > best) {
        std::swap(roots.at(first), roots.at(other));
        best = candidate;
      }
    }
  }
  return roots;
}

struct PrototypeShape {
  AgentKind kind{AgentKind::k_passer};
  std::int32_t character_type{0};
  std::size_t first_level{0};
  std::array<float, K_MAX_LEVELS_OF_DETAIL> switch_depths{K_PASSER_LOD_DEPTHS};
  std::uint32_t extra_mesh_flags{0};
};

[[nodiscard]] std::expected<Prototype, std::string> build_prototype(
    std::shared_ptr<const Character::ModelResource> resource, const PrototypeShape& shape) {
  const Omikron::Model3DOData& model{resource->model};
  Prototype prototype;
  prototype.kind = shape.kind;
  prototype.character_type = shape.character_type;
  prototype.switch_depths = shape.switch_depths;
  prototype.level_roots = level_of_detail_roots(model);
  if (prototype.level_roots.empty()) {
    return std::expected<Prototype, std::string>{
        std::unexpect, fmt::format("passerby model '{}' has no root object", resource->name)};
  }
  for (const std::size_t root : prototype.level_roots) {
    std::vector<std::size_t> objects{Omikron::Model3DO::subtree_preorder(model, root)};
    auto layout{
        Omikron::Model3DO::build_posed_layout(model, Omikron::Model3DO::subtree_mask(model, root))};
    if (objects.empty() || !layout) {
      return std::expected<Prototype, std::string>{std::unexpect,
          fmt::format("passerby model '{}' level {}: {}",
              resource->name,
              prototype.level_objects.size(),
              layout ? std::string{"invalid hierarchy"} : layout.error())};
    }
    for (Omikron::MaterialGroup& group : layout->groups) {
      group.flags |= shape.extra_mesh_flags;
    }
    prototype.level_objects.push_back(std::move(objects));
    prototype.level_layouts.push_back(std::move(layout).value());
  }
  prototype.first_level = std::min(shape.first_level, prototype.level_roots.size() - 1U);
  prototype.root_radius = model.meshes.at(prototype.level_roots.front()).bounding_radius;
  prototype.resource = std::move(resource);
  return prototype;
}

}  // namespace

std::size_t select_level_of_detail(const std::size_t level_count,
    const std::size_t first_level,
    const std::span<const float> switch_depths,
    const std::uint8_t detail_level,
    const float camera_depth) {
  if (first_level >= level_count) {
    return level_count == 0U ? 0U : level_count - 1U;
  }
  const std::size_t chain_length{level_count - first_level};
  std::size_t record{0};
  for (std::int32_t skip{1 - static_cast<std::int32_t>(detail_level)};
      skip > 0 && record + 1U < chain_length;
      --skip) {
    ++record;
  }
  while (record + 1U < chain_length && record < switch_depths.size() &&
         switch_depths[record] <= camera_depth) {
    ++record;
  }
  return first_level + record;
}

std::expected<void, std::string> Runtime::populate(PopulationRequest request) {
  APP_PROFILE_FUNCTION();
  clear();
  if (request.network == nullptr || request.animations == nullptr) {
    return std::expected<void, std::string>{
        std::unexpect, "crowd population needs OPT and ANI data"};
  }
  m_network = std::move(request.network);
  m_animations = std::move(request.animations);

  // Runtime 0x00453450 builds a sex's prototypes only when the ANI entry for
  // that passerby type has a walk animation (0x00453D50).
  const auto has_walk = [this](const std::int32_t character_type) {
    const Omikron::AniCharacter* const character{m_animations->character(character_type)};
    return character != nullptr &&
           !Omikron::ani_nodes_of_kind(*character, Omikron::AniAnimationKind::k_walk).empty();
  };
  const auto add_prototypes =
      [this](std::vector<std::shared_ptr<const Character::ModelResource>>& models,
          const PrototypeShape& shape,
          const bool equal_shares) -> std::expected<std::size_t, std::string> {
    std::vector<Prototype> built;
    for (std::shared_ptr<const Character::ModelResource>& model : models) {
      auto prototype{build_prototype(std::move(model), shape)};
      if (!prototype) {
        return std::expected<std::size_t, std::string>{std::unexpect, prototype.error()};
      }
      built.push_back(std::move(prototype).value());
    }
    // 0x00453E80 gives passersby an integer share of 100 per variant;
    // vehicles keep their table weight of 1.
    std::int32_t share{1};
    if (equal_shares) {
      share = built.empty() ? 0 : K_PROTOTYPE_SHARES / static_cast<std::int32_t>(built.size());
    }
    for (Prototype& prototype : built) {
      prototype.remaining_share = share;
      m_prototypes.push_back(std::move(prototype));
    }
    return built.size();
  };
  const auto fail = [this](std::string error) {
    clear();
    return std::expected<void, std::string>{std::unexpect, std::move(error)};
  };

  const Omikron::TrajectoryOpt& network{*m_network};
  const bool has_passers{network.pedestrian_begin < network.pedestrian_end};
  const bool has_sliders{network.pedestrian_end < network.trajectory_end};
  if (has_passers && has_walk(K_MALE_PASSER)) {
    auto added{add_prototypes(request.male_models,
        PrototypeShape{.kind = AgentKind::k_passer, .character_type = K_MALE_PASSER},
        true)};
    if (!added) {
      return fail(added.error());
    }
    m_male_prototype_count = added.value();
  }
  if (has_passers && has_walk(K_FEMALE_PASSER)) {
    if (auto added{add_prototypes(request.female_models,
            PrototypeShape{.kind = AgentKind::k_passer, .character_type = K_FEMALE_PASSER},
            true)};
        !added) {
      return fail(added.error());
    }
  }
  m_passer_prototype_count = m_prototypes.size();
  if (has_sliders) {
    // 0x00453450: the slider chain starts at level 1; `moto` at level 0.
    auto sliders{add_prototypes(request.slider_models,
        PrototypeShape{.kind = AgentKind::k_slider,
            .first_level = 1U,
            .switch_depths = K_SLIDER_LOD_DEPTHS,
            .extra_mesh_flags = K_SLIDER_MESH_FLAGS},
        false)};
    if (!sliders) {
      return fail(sliders.error());
    }
    m_slider_prototype_count = sliders.value();
    if (auto motos{add_prototypes(request.moto_models,
            PrototypeShape{.kind = AgentKind::k_slider, .switch_depths = K_SLIDER_LOD_DEPTHS},
            false)};
        !motos) {
      return fail(motos.error());
    }
  }
  if (m_prototypes.empty()) {
    return {};
  }

  m_lists.assign(network.trajectories.size() + network.segments.size() + network.routes.size(), {});
  m_crossing_locks.assign(network.crossings.size(), 0);
  m_stop_point_ids.clear();
  for (const Omikron::TrajectoryStopPoint& point : network.stop_points) {
    m_stop_point_ids.push_back(point.animation_id);
  }
  m_activities.assign(network.stop_points.size(), std::nullopt);
  m_route_counts.clear();
  for (const Omikron::TrajectoryRecord& trajectory : network.trajectories) {
    m_route_counts.push_back(trajectory.route_count);
  }
  if (m_passer_prototype_count != 0U) {
    spawn(std::min<std::uint8_t>(request.street_activity, 4U));
  }
  if (m_prototypes.size() != m_passer_prototype_count) {
    spawn_sliders();
  }
  return {};
}

void Runtime::clear() {
  m_network.reset();
  m_animations.reset();
  m_prototypes.clear();
  m_male_prototype_count = 0;
  m_passer_prototype_count = 0;
  m_slider_prototype_count = 0;
  m_slider_count = 0;
  ++m_generation;
  m_agents.clear();
  m_lists.clear();
  m_crossing_locks.clear();
  m_stop_point_ids.clear();
  m_activities.clear();
  m_route_counts.clear();
  m_random_state = 1U;
  m_counter = 0U;
}

const Omikron::Animation3DA* Runtime::animation(const Agent& pedestrian) const {
  if (m_animations == nullptr || pedestrian.animation == nullptr ||
      pedestrian.animation->animation_index >= m_animations->animations.size()) {
    return nullptr;
  }
  return &m_animations->animations.at(pedestrian.animation->animation_index);
}

std::int32_t Runtime::next_random() {
  // MSVC CRT rand(), which Runtime links.
  m_random_state = (m_random_state * 214013U) + 2531011U;
  return static_cast<std::int32_t>((m_random_state >> 16U) & 0x7FFFU);
}

std::uint32_t Runtime::next_counter() {
  m_counter = (m_counter + 1U) & K_COUNTER_MASK;
  return m_counter;
}

const Omikron::AniCharacter* Runtime::ani_character(const std::int32_t character_type) const {
  return m_animations == nullptr ? nullptr : m_animations->character(character_type);
}

const Omikron::AniAnimation* Runtime::random_animation(
    const std::int32_t character_type, const Omikron::AniAnimationKind kind) {
  const Omikron::AniCharacter* const character{ani_character(character_type)};
  if (character == nullptr) {
    return nullptr;
  }
  const std::vector<const Omikron::AniAnimation*> candidates{
      Omikron::ani_nodes_of_kind(*character, kind)};
  if (candidates.empty()) {
    return nullptr;
  }
  return candidates.at(static_cast<std::size_t>(next_random()) % candidates.size());
}

void Runtime::select_animation(Agent& pedestrian, const Omikron::AniAnimation* animation) const {
  // Runtime 0x00455E20.
  if (animation == nullptr) {
    return;
  }
  pedestrian.animation = animation;
  pedestrian.progress = 1.0F;
  const Omikron::Animation3DA* const payload{this->animation(pedestrian)};
  pedestrian.frame_count = payload == nullptr ? 1.0F : static_cast<float>(payload->max_frame_index);
  pedestrian.bob = 0.0F;
}

Vec3 Runtime::root_motion(
    const Agent& pedestrian, const float previous, const float current) const {
  // Runtime 0x00437FE0 -> 0x004711D0: the first channel with a position
  // stream, rotated by the instance basis.
  const Omikron::Animation3DA* const payload{animation(pedestrian)};
  if (payload == nullptr) {
    return {};
  }
  for (const Omikron::Animation3DAChannel& channel : payload->channels) {
    if (channel.translations.empty()) {
      continue;
    }
    const std::optional<Vec3> motion{channel.integrate_translation(previous, current)};
    return motion.has_value() ? App::Runtime::transform_vector(motion.value(), pedestrian.facing)
                              : Vec3{};
  }
  return {};
}

float Runtime::root_speed(const Agent& pedestrian) const {
  // Runtime 0x00453D80.
  if (pedestrian.frame_count <= 0.0F) {
    return 0.0F;
  }
  const Vec3 motion{root_motion(pedestrian, 1.0F, pedestrian.frame_count - 1.0F)};
  return std::sqrt((motion.z * motion.z) + (motion.x * motion.x)) * K_FIXED_SCALE /
         pedestrian.frame_count;
}

std::size_t Runtime::trajectory_list(const std::size_t trajectory) {
  return trajectory;
}

std::size_t Runtime::segment_list(const std::size_t segment) const {
  return m_network->trajectories.size() + segment;
}

std::size_t Runtime::route_list(const std::size_t route) const {
  return m_network->trajectories.size() + m_network->segments.size() + route;
}

std::optional<std::size_t> Runtime::list_head(const std::size_t list) const {
  const std::vector<std::size_t>& members{m_lists.at(list)};
  return members.empty() ? std::nullopt : std::optional<std::size_t>{members.front()};
}

std::optional<std::size_t> Runtime::list_ahead(const std::size_t pedestrian) const {
  const std::vector<std::size_t>& members{m_lists.at(m_agents.at(pedestrian).list)};
  const auto found{std::ranges::find(members, pedestrian)};
  if (found == members.end() || std::next(found) == members.end()) {
    return std::nullopt;
  }
  return *std::next(found);
}

void Runtime::move_to_list(const std::size_t pedestrian, const std::size_t list) {
  Agent& moving{m_agents.at(pedestrian)};
  std::erase(m_lists.at(moving.list), pedestrian);
  m_lists.at(list).insert(m_lists.at(list).begin(), pedestrian);
  moving.list = list;
}

void Runtime::swap_in_list(const std::size_t first, const std::size_t second) {
  // 0x00455830 exchanges the two link records, so each takes the other's
  // place (and list) without moving any other entry.
  Agent& first_pedestrian{m_agents.at(first)};
  Agent& second_pedestrian{m_agents.at(second)};
  std::vector<std::size_t>& first_list{m_lists.at(first_pedestrian.list)};
  std::vector<std::size_t>& second_list{m_lists.at(second_pedestrian.list)};
  const auto first_entry{std::ranges::find(first_list, first)};
  const auto second_entry{std::ranges::find(second_list, second)};
  if (first_entry == first_list.end() || second_entry == second_list.end()) {
    return;
  }
  *first_entry = second;
  *second_entry = first;
  std::swap(first_pedestrian.list, second_pedestrian.list);
}

std::size_t Runtime::route_for(const std::size_t trajectory) {
  std::int8_t& count{m_route_counts.at(trajectory)};
  if (count <= 0) {
    count = 1;
  }
  return static_cast<std::size_t>(m_network->trajectories.at(trajectory).first_route) +
         (next_counter() % static_cast<std::uint32_t>(count));
}

std::int16_t Runtime::step_crossing(const std::size_t route, const std::int32_t step) const {
  const Omikron::TrajectoryRoute& record{m_network->routes.at(route)};
  if (step == 0) {
    return record.entry_crossing;
  }
  const std::int32_t index{record.first_step + step - 1};
  if (index < 0 || std::cmp_greater_equal(index, m_network->route_steps.size())) {
    return -1;
  }
  return m_network->route_steps.at(static_cast<std::size_t>(index)).crossing;
}

bool Runtime::crossing_transition(
    const std::size_t route, const std::int32_t release, const std::int32_t acquire) {
  // Runtime 0x00453230: acquiring a step's crossing fails while any holder
  // has locked it; holding it locks every conflicting crossing.
  const auto conflicts = [this](const std::int16_t crossing) {
    const Omikron::TrajectoryCrossing& record{
        m_network->crossings.at(static_cast<std::size_t>(crossing))};
    return std::span<const std::int16_t>{m_network->crossing_conflicts}.subspan(
        static_cast<std::size_t>(record.first_conflict),
        static_cast<std::size_t>(record.conflict_count));
  };
  if (acquire >= 0) {
    if (const std::int16_t crossing{step_crossing(route, acquire)}; crossing != -1) {
      if (m_crossing_locks.at(static_cast<std::size_t>(crossing)) != 0) {
        return true;
      }
      for (const std::int16_t conflict : conflicts(crossing)) {
        ++m_crossing_locks.at(static_cast<std::size_t>(conflict));
      }
    }
  }
  if (release != -1) {
    if (const std::int16_t crossing{step_crossing(route, release)}; crossing != -1) {
      for (const std::int16_t conflict : conflicts(crossing)) {
        --m_crossing_locks.at(static_cast<std::size_t>(conflict));
      }
    }
  }
  return false;
}

std::optional<std::size_t> Runtime::allocate() {
  // Runtime 0x00453ED0.
  if (m_agents.size() >= K_MAX_PEDESTRIANS) {
    return std::nullopt;
  }
  const std::size_t female_count{m_passer_prototype_count - m_male_prototype_count};
  std::int32_t sex{next_random() & 1};
  if (m_male_prototype_count == 0U) {
    sex = 1;
  }
  if (female_count == 0U) {
    sex = 0;
  }
  const std::size_t first{sex == 0 ? 0U : m_male_prototype_count};
  const std::size_t count{sex == 0 ? m_male_prototype_count : female_count};
  std::size_t prototype_index{first};
  for (std::size_t index{first}; index < first + count; ++index) {
    if (m_prototypes.at(index).remaining_share != 0) {
      --m_prototypes.at(index).remaining_share;
      prototype_index = index;
      break;
    }
  }
  const Prototype& prototype{m_prototypes.at(prototype_index)};

  Agent pedestrian;
  pedestrian.prototype = prototype_index;
  pedestrian.character_type = prototype.character_type;
  select_animation(
      pedestrian, random_animation(pedestrian.character_type, Omikron::AniAnimationKind::k_walk));
  pedestrian.ground_offset = prototype.root_radius;
  pedestrian.radius = prototype.root_radius * 0.5F;
  m_agents.push_back(pedestrian);
  return m_agents.size() - 1U;
}

std::optional<std::size_t> Runtime::allocate_slider() {
  // Runtime 0x004544B0: slot 0 is the boardable slider built from the first
  // `sli_fn` prototype; later slots pick `moto` or another `sli_fn` variant.
  if (m_slider_count >= K_MAX_SLIDERS) {
    return std::nullopt;
  }
  const std::size_t slider_first{m_passer_prototype_count};
  const std::size_t moto_first{slider_first + m_slider_prototype_count};
  const std::size_t moto_count{m_prototypes.size() - moto_first};
  std::size_t prototype_index{slider_first};
  const bool boardable{m_slider_count == 0U && m_slider_prototype_count != 0U};
  if (!boardable) {
    std::int32_t choice{next_random() & 1};
    if (moto_count == 0U) {
      choice = 1;
    }
    if (m_slider_prototype_count < 2U) {
      choice = 0;
      if (moto_count == 0U) {
        return std::nullopt;
      }
    }
    const std::size_t first{choice == 0 ? moto_first : slider_first + 1U};
    const std::size_t count{choice == 0 ? moto_count : m_slider_prototype_count - 1U};
    prototype_index = first;
    for (std::size_t index{first}; index < first + count; ++index) {
      if (m_prototypes.at(index).remaining_share != 0) {
        --m_prototypes.at(index).remaining_share;
        prototype_index = index;
        break;
      }
    }
  }
  const Prototype& prototype{m_prototypes.at(prototype_index)};

  Agent slider;
  slider.kind = AgentKind::k_slider;
  slider.prototype = prototype_index;
  slider.boardable = boardable;
  slider.max_speed = K_SLIDER_MAX_SPEED;
  slider.nominal_speed = K_FIXED_SCALE;
  slider.speed = K_FIXED_SCALE;
  // Instance radius of the hull's level-0 root, doubled (0x00438040).
  slider.radius = prototype.root_radius * 2.0F;
  slider.ground_offset = K_SLIDER_HOVER;
  m_agents.push_back(slider);
  ++m_slider_count;
  return m_agents.size() - 1U;
}

void Runtime::spawn_sliders() {
  // Runtime 0x004543F0: the same placement walk over the slider lanes, with
  // the OPT slider spacing unscaled by Crowd Density.
  const Omikron::TrajectoryOpt& network{*m_network};
  const std::size_t first_slider{m_agents.size()};
  spawn_along(network.pedestrian_end,
      network.trajectory_end,
      static_cast<float>(network.slider_spacing) * K_SPACING_UNIT,
      true);
  for (std::size_t index{first_slider}; index < m_agents.size(); ++index) {
    Agent& slider{m_agents.at(index)};
    slider.facing = facing_toward(
        slider.direction.x, slider.direction.y, slider.direction.z, length(slider.direction));
    slider.instance = Vec3{.x = slider.position.x,
        .y = slider.position.y - slider.ground_offset,
        .z = slider.position.z};
  }
  App::Log::info(LogCategory::Scenario,
      "CrowdSlidersPopulated — sliders={} prototypes={} lanes=[{}, {})",
      m_slider_count,
      m_prototypes.size() - m_passer_prototype_count,
      network.pedestrian_end,
      network.trajectory_end);
}

void Runtime::spawn(const std::uint8_t street_activity) {
  const Omikron::TrajectoryOpt& network{*m_network};
  const float spacing{static_cast<float>(network.pedestrian_spacing) *
                      (K_CROWD_DENSITY_LEVELS - static_cast<float>(street_activity)) *
                      K_SPACING_UNIT};
  spawn_along(network.pedestrian_begin, network.pedestrian_end, spacing, false);

  // 0x00453450 post-pass: face along the trajectory and jitter each walker's
  // playback rate by +-25%, matching the leading point's speed to the walk.
  for (Agent& pedestrian : m_agents) {
    pedestrian.facing = facing_toward(pedestrian.direction.x, pedestrian.direction.z);
    const float speed{root_speed(pedestrian)};
    const float rate{(static_cast<float>(5 - (next_random() % 10)) * 0.05F) + 1.0F};
    pedestrian.nominal_speed = rate * speed;
    pedestrian.speed = pedestrian.nominal_speed;
    pedestrian.playback_rate = rate;
    pedestrian.instance = Vec3{.x = pedestrian.position.x,
        .y = pedestrian.position.y - pedestrian.ground_offset,
        .z = pedestrian.position.z};
  }
  App::Log::info(LogCategory::Scenario,
      "CrowdPopulated — passersby={} prototypes={} trajectories=[{}, {}) spacing={:.1f}",
      m_agents.size(),
      m_passer_prototype_count,
      network.pedestrian_begin,
      network.pedestrian_end,
      spacing);
}

void Runtime::spawn_along(
    const std::int32_t begin, const std::int32_t end, const float spacing, const bool sliders) {
  // Runtime 0x00453B40: walk each trajectory and place an agent at a segment
  // start once the distance since the previous one exceeds the spacing. A
  // failed allocation ends the whole walk.
  const Omikron::TrajectoryOpt& network{*m_network};
  for (std::int32_t trajectory_index{begin}; trajectory_index < end; ++trajectory_index) {
    const auto trajectory_slot{static_cast<std::size_t>(trajectory_index)};
    const Omikron::TrajectoryRecord& trajectory{network.trajectories.at(trajectory_slot)};
    std::size_t list{trajectory_list(trajectory_slot)};
    Vec3 position{trajectory.start};
    float accumulated{0.0F};
    for (std::size_t segment{0}; segment < trajectory.segment_count; ++segment) {
      const std::size_t segment_index{static_cast<std::size_t>(trajectory.first_segment) + segment};
      const Vec3& vector{network.segments.at(segment_index).vector};
      const float segment_length{length(vector)};
      if (accumulated > spacing) {
        const std::optional<std::size_t> allocated{sliders ? allocate_slider() : allocate()};
        if (!allocated.has_value()) {
          return;
        }
        accumulated = 0.0F;
        Agent& agent{m_agents.at(allocated.value())};
        agent.target = position;
        agent.segment_start = position;
        agent.direction = scaled(vector, 1.0F / segment_length);
        agent.position = Vec3{.x = position.x - (agent.direction.x * K_SPAWN_LEAD),
            .y = position.y,
            .z = position.z - (agent.direction.z * K_SPAWN_LEAD)};
        agent.remaining = segment_length * K_FIXED_SCALE;
        agent.trajectory = trajectory_slot;
        agent.segment = static_cast<std::int32_t>(segment) + 1;
        agent.list = list;
        m_lists.at(list).insert(m_lists.at(list).begin(), allocated.value());
        agent.route = route_for(trajectory_slot);
        agent.flags |= K_FLAG_ON_NETWORK;
      }
      accumulated += segment_length;
      position = add(position, vector);
      list = segment_list(segment_index);
    }
  }
}

bool Runtime::test_neighbour(
    const std::size_t index, const Vec3& candidate, const std::size_t other) {
  Agent& pedestrian{m_agents.at(index)};
  const Agent& ahead{m_agents.at(other)};
  const float clearance{ahead.radius + pedestrian.radius};
  pedestrian.neighbour = other;
  if (distance_squared(candidate, ahead.target) < clearance * clearance) {
    pedestrian.flags |= K_FLAG_BLOCKED;
    return true;
  }
  return false;
}

bool Runtime::scan_segments(const std::size_t index,
    const Vec3& candidate,
    const std::size_t trajectory,
    std::int32_t segment) {
  // Runtime 0x00455340: the newest entrant of the first occupied list ahead.
  const Omikron::TrajectoryRecord& record{m_network->trajectories.at(trajectory)};
  const auto usable = [this](const std::size_t head) {
    return (m_agents.at(head).flags & K_FLAG_OCCUPANCY_IGNORED) == 0U;
  };
  if (segment == 1) {
    if (const std::optional<std::size_t> head{list_head(trajectory_list(trajectory))};
        head.has_value() && usable(*head)) {
      static_cast<void>(test_neighbour(index, candidate, *head));
      return true;
    }
  } else {
    segment -= 2;
  }
  while (std::cmp_less(segment, record.segment_count)) {
    const std::int32_t segment_index{record.first_segment + segment};
    if (segment_index >= 0 && std::cmp_less(segment_index, m_network->segments.size())) {
      const std::optional<std::size_t> head{
          list_head(segment_list(static_cast<std::size_t>(segment_index)))};
      if (head.has_value() && usable(*head)) {
        static_cast<void>(test_neighbour(index, candidate, *head));
        return true;
      }
    }
    ++segment;
  }
  return false;
}

std::int32_t Runtime::change_segment(const std::size_t index,
    const std::size_t trajectory,
    const std::uint32_t new_flags,
    const std::int32_t segment) {
  // Runtime 0x00455680: moves the passerby between occupancy lists and
  // acquires/releases junction crossings.
  Agent& pedestrian{m_agents.at(index)};
  const Omikron::TrajectoryRecord& record{m_network->trajectories.at(trajectory)};
  const auto first_segment{static_cast<std::int32_t>(record.first_segment)};
  std::size_t new_list{0};
  std::int32_t result{0};
  if (((new_flags ^ pedestrian.flags) & K_FLAG_IN_JUNCTION) == 0U) {
    if ((new_flags & K_FLAG_IN_JUNCTION) != 0U) {
      if (crossing_transition(pedestrian.route, segment, segment + 1)) {
        pedestrian.flags |= K_FLAG_BLOCKED;
        return -1;
      }
      return segment + 1;
    }
    new_list = segment_list(table_index(first_segment, segment - 1));
    result = segment + 1;
  } else if ((new_flags & K_FLAG_IN_JUNCTION) == 0U) {
    static_cast<void>(crossing_transition(pedestrian.route, segment, -1));
    const std::size_t route{pedestrian.route};
    pedestrian.trajectory = static_cast<std::size_t>(m_network->routes.at(route).next_trajectory);
    new_list = trajectory_list(pedestrian.trajectory);
    pedestrian.route = route_for(pedestrian.trajectory);
    result = 1;
  } else {
    if (crossing_transition(pedestrian.route, -1, 0)) {
      pedestrian.flags |= K_FLAG_BLOCKED;
      return -1;
    }
    new_list = route_list(pedestrian.route);
    result = 0;
  }
  move_to_list(index, new_list);
  return result;
}

void Runtime::advance_target(const std::size_t index, const float delta_units) {
  // Runtime 0x00454F40: advance the leading point along its segment, switch
  // segment/junction step when it runs out, then test the occupant ahead.
  Agent& pedestrian{m_agents.at(index)};
  const Omikron::TrajectoryOpt& network{*m_network};
  const float step{pedestrian.speed * delta_units};
  const std::uint32_t entry_flags{pedestrian.flags};
  std::uint32_t flags{entry_flags & ~K_FLAG_BLOCKED};
  pedestrian.flags = flags;

  Vec3 candidate{pedestrian.target};
  Vec3 direction{pedestrian.direction};
  Vec3 segment_start{pedestrian.segment_start};
  float remaining{pedestrian.remaining - step};
  std::size_t trajectory{pedestrian.trajectory};
  std::int32_t segment{pedestrian.segment};
  float advance{step};
  bool changed_segment{false};

  if (remaining < 0.0F) {
    if ((entry_flags & K_FLAG_ACTIVITY) != 0U) {
      return;
    }
    candidate = pedestrian.segment_start;
    advance = -remaining;
    const Omikron::TrajectoryRoute& route{network.routes.at(pedestrian.route)};
    if ((entry_flags & K_FLAG_IN_JUNCTION) == 0U) {
      const Omikron::TrajectoryRecord& record{network.trajectories.at(trajectory)};
      const auto segment_index{table_index(record.first_segment, segment - 1)};
      candidate = add(candidate, network.segments.at(segment_index).vector);
      if (std::cmp_equal(segment, record.segment_count)) {
        flags |= K_FLAG_IN_JUNCTION;
        segment = -1;
      }
    } else if (segment == static_cast<std::int32_t>(route.step_count)) {
      flags &= ~(K_FLAG_IN_JUNCTION | K_FLAG_BLOCKED);
      trajectory = static_cast<std::size_t>(route.next_trajectory);
      candidate = network.trajectories.at(trajectory).start;
      segment = 0;
    } else {
      candidate =
          add(candidate, network.route_steps.at(table_index(route.first_step, segment)).vector);
    }

    Vec3 vector{};
    if ((flags & K_FLAG_IN_JUNCTION) == 0U) {
      // 0x004555C0: the next segment, possibly with an idle activity.
      const Omikron::TrajectoryRecord& record{network.trajectories.at(trajectory)};
      const Omikron::TrajectorySegment& next{
          network.segments.at(table_index(record.first_segment, segment))};
      vector = next.vector;
      if (next.stop_point != -1 &&
          m_stop_point_ids.at(static_cast<std::size_t>(next.stop_point)) != 0 &&
          (next_counter() & 1U) != 0U) {
        flags |= K_FLAG_ACTIVITY;
        pedestrian.stop_point = static_cast<std::size_t>(next.stop_point);
      }
    } else {
      // 0x004554B0: the next junction step, or the leg to the next trajectory.
      const std::int32_t next_step{segment + 1};
      if (next_step == static_cast<std::int32_t>(route.step_count)) {
        const Vec3& start{
            network.trajectories.at(static_cast<std::size_t>(route.next_trajectory)).start};
        vector = Vec3{
            .x = start.x - candidate.x, .y = start.y - candidate.y, .z = start.z - candidate.z};
      } else {
        vector = network.route_steps.at(table_index(route.first_step, next_step)).vector;
      }
    }
    const float vector_length{length(vector)};
    direction = vector_length > 0.0F ? scaled(vector, 1.0F / vector_length) : Vec3{};
    remaining = vector_length * K_FIXED_SCALE;
    segment_start = candidate;
    changed_segment = true;
  }
  candidate = add(candidate, scaled(direction, advance / K_FIXED_SCALE));

  pedestrian.neighbour.reset();
  const std::optional<std::size_t> ahead{list_ahead(index)};
  if (!ahead.has_value() || (m_agents.at(ahead.value()).flags & K_FLAG_OCCUPANCY_IGNORED) != 0U) {
    if ((pedestrian.flags & K_FLAG_IN_JUNCTION) == 0U) {
      const Omikron::TrajectoryRecord& record{network.trajectories.at(pedestrian.trajectory)};
      if (std::cmp_less(record.segment_count, pedestrian.segment) ||
          !scan_segments(index, candidate, pedestrian.trajectory, pedestrian.segment + 1)) {
        // 0x004552B0: the newest passerby inside this trajectory's route.
        if (const std::optional<std::size_t> head{list_head(route_list(pedestrian.route))};
            head.has_value() &&
            (m_agents.at(head.value()).flags & K_FLAG_OCCUPANCY_IGNORED) == 0U) {
          static_cast<void>(test_neighbour(index, candidate, head.value()));
        }
      }
    } else {
      static_cast<void>(scan_segments(index,
          candidate,
          static_cast<std::size_t>(network.routes.at(pedestrian.route).next_trajectory),
          1));
    }
  } else {
    static_cast<void>(test_neighbour(index, candidate, ahead.value()));
  }

  if ((pedestrian.flags & K_FLAG_BLOCKED) != 0U) {
    return;
  }
  std::int32_t committed_segment{pedestrian.segment};
  if (changed_segment) {
    committed_segment = change_segment(index, pedestrian.trajectory, flags, pedestrian.segment);
    if (committed_segment == -1) {
      return;
    }
  }
  pedestrian.target = candidate;
  pedestrian.direction = direction;
  pedestrian.remaining = remaining;
  pedestrian.segment_start = segment_start;
  pedestrian.flags = flags;
  pedestrian.segment = committed_segment;
}

std::int32_t Runtime::steer(Agent& agent,
    const Vec3& to_target,
    const float distance,
    const float near_distance,
    const float far_distance) {
  // Runtime 0x00455D10: face the leading point and pace it to the agent.
  const float scaled_distance{distance * K_FIXED_SCALE};
  if (scaled_distance >= K_STEER_MINIMUM) {
    agent.facing = facing_toward(to_target.x, to_target.y, to_target.z, distance);
    const bool blocked{(agent.flags & K_FLAG_BLOCKED) != 0U};
    if (near_distance * K_FIXED_SCALE <= scaled_distance) {
      agent.speed = far_distance * K_FIXED_SCALE < scaled_distance ? 0.0F : agent.nominal_speed;
      return blocked ? 2 : 1;
    }
    if (!blocked) {
      agent.speed = agent.nominal_speed + agent.nominal_speed;
      return 1;
    }
  } else if (agent.speed == 0.0F) {
    agent.speed = K_FIXED_SCALE;
  }
  return 0;
}

void Runtime::drive(const std::size_t index, const float delta_units) {
  // Runtime 0x00456C70, ordinary traffic state 0: the slider flies straight
  // at its leading point, accelerating to its top speed and braking while the
  // vehicle ahead blocks it. Its nominal speed (+0x34) is its live velocity.
  advance_target(index, delta_units);
  Agent& slider{m_agents.at(index)};
  const Vec3 to_target{.x = slider.target.x - slider.position.x,
      .y = slider.target.y - slider.position.y,
      .z = slider.target.z - slider.position.z};
  const float distance{length(to_target)};
  const std::int32_t steering{
      steer(slider, to_target, distance, K_SLIDER_STEER_NEAR, K_SLIDER_STEER_FAR)};
  if (steering == 0) {
    slider.flags |= K_FLAG_WAITING;
    return;
  }
  if ((slider.flags & K_FLAG_WAITING) != 0U) {
    slider.nominal_speed = K_FIXED_SCALE;
    slider.flags &= ~K_FLAG_WAITING;
  }
  float velocity{0.0F};
  if (steering == 2) {
    velocity = std::max(slider.nominal_speed - (delta_units * K_SLIDER_BRAKING), 0.0F);
  } else {
    velocity = std::min((delta_units * K_FIXED_SCALE) + slider.nominal_speed, slider.max_speed);
  }
  slider.nominal_speed = velocity;
  const float step{(velocity * delta_units) / K_FIXED_SCALE / distance};
  slider.position = add(slider.position, scaled(to_target, step));
  slider.instance = Vec3{.x = slider.position.x,
      .y = slider.position.y - slider.ground_offset,
      .z = slider.position.z};
}

void Runtime::walk(const std::size_t index, const float delta_units) {
  // Runtime 0x00455830.
  advance_target(index, delta_units);

  Agent& pedestrian{m_agents.at(index)};
  std::uint32_t flags{pedestrian.flags};
  if (pedestrian.neighbour.has_value()) {
    const std::size_t other{pedestrian.neighbour.value()};
    const Agent& ahead{m_agents.at(other)};
    if (ahead.nominal_speed < pedestrian.nominal_speed && (ahead.flags & K_FLAG_BLOCKED) == 0U &&
        ((ahead.flags ^ flags) & K_FLAG_IN_JUNCTION) == 0U &&
        ((flags & K_FLAG_IN_JUNCTION) != 0U || pedestrian.segment == ahead.segment)) {
      // Overtake a slower walker by side-stepping past it.
      const float dx{ahead.position.x - pedestrian.position.x};
      const float dz{ahead.position.z - pedestrian.position.z};
      const float clearance{ahead.radius + pedestrian.radius};
      const float horizontal{std::sqrt((dz * dz) + (dx * dx))};
      flags &= ~K_FLAG_BLOCKED;
      if (horizontal <= K_SIDE_STEP_DISTANCE && horizontal > 0.0F) {
        const float along_x{dx / horizontal};
        const float along_z{dz / horizontal};
        swap_in_list(index, other);
        pedestrian.side_step = Vec3{.x = ((along_x - along_z) * clearance) + dx,
            .y = 0.0F,
            .z = ((along_z + along_x) * clearance) + dz};
        pedestrian.side_step_remaining =
            std::sqrt((pedestrian.side_step.z * pedestrian.side_step.z) +
                      (pedestrian.side_step.x * pedestrian.side_step.x));
        flags |= K_FLAG_SIDE_STEP;
      }
    }
  }
  pedestrian.flags = flags;
  if ((flags & K_FLAG_ACTIVITY) != 0U) {
    start_activity(pedestrian);
  }

  const Vec3 start{pedestrian.position};
  const float before_x{pedestrian.target.x - start.x};
  const float before_z{pedestrian.target.z - start.z};
  float previous{pedestrian.progress};
  float current{(pedestrian.playback_rate * delta_units) + pedestrian.progress};
  if (pedestrian.frame_count <= current) {
    current = (current + 1.0F) - pedestrian.frame_count;
    previous = 0.0F;
    pedestrian.instance = Vec3{.x = start.x, .y = start.y - pedestrian.ground_offset, .z = start.z};
    pedestrian.bob = 0.0F;
  }
  const Vec3 motion{root_motion(pedestrian, previous, current)};
  const float x{start.x + motion.x};
  const float z{start.z + motion.z};
  pedestrian.progress = current;
  pedestrian.bob += motion.y;

  const Vec3 to_target{.x = pedestrian.target.x - x,
      .y = pedestrian.target.y - start.y,
      .z = pedestrian.target.z - z};
  float distance{length(to_target)};
  const float step_length{std::sqrt(((to_target.z - before_z) * (to_target.z - before_z)) +
                                    ((to_target.x - before_x) * (to_target.x - before_x)))};
  bool waiting{false};
  if ((pedestrian.flags & K_FLAG_SIDE_STEP) == 0U) {
    waiting = steer(pedestrian,
                  Vec3{.x = to_target.x, .y = 0.0F, .z = to_target.z},
                  distance,
                  K_STEER_NEAR,
                  K_STEER_FAR) == 0;
  } else {
    pedestrian.side_step_remaining -= step_length;
    if (pedestrian.side_step_remaining <= 0.0F) {
      pedestrian.flags &= ~K_FLAG_SIDE_STEP;
    }
    pedestrian.facing = facing_toward(pedestrian.side_step.x, pedestrian.side_step.z);
  }
  if (waiting != ((pedestrian.flags & K_FLAG_WAITING) != 0U)) {
    if (waiting) {
      select_animation(pedestrian,
          random_animation(pedestrian.character_type, Omikron::AniAnimationKind::k_blocked));
      pedestrian.flags |= K_FLAG_WAITING;
    } else {
      select_animation(pedestrian,
          random_animation(pedestrian.character_type, Omikron::AniAnimationKind::k_walk));
      pedestrian.flags &= ~K_FLAG_WAITING;
    }
  }

  if (distance == 0.0F) {
    distance = 1.0F;
  }
  const float y{((step_length * to_target.y) / distance) + start.y};
  pedestrian.position = Vec3{.x = x, .y = y, .z = z};
  pedestrian.instance = Vec3{.x = x, .y = (pedestrian.bob - pedestrian.ground_offset) + y, .z = z};
}

void Runtime::start_activity(Agent& pedestrian) {
  // Runtime 0x00455830 activity block: claim a free activity record for the
  // stop point and resolve its begin/loop/end animations by group name.
  const auto free_slot{
      std::ranges::find_if(m_activities, [](const std::optional<Activity>& activity) {
        return !activity.has_value();
      })};
  const Omikron::AniCharacter* const character{ani_character(pedestrian.character_type)};
  if (free_slot == m_activities.end() || !pedestrian.stop_point.has_value() ||
      character == nullptr) {
    pedestrian.flags &= ~K_FLAG_ACTIVITY;
    return;
  }
  const std::size_t stop_index{pedestrian.stop_point.value()};
  const Omikron::TrajectoryStopPoint& stop{m_network->stop_points.at(stop_index)};
  const std::int16_t animation_id{m_stop_point_ids.at(stop_index)};
  const Omikron::AniAnimation* const loop{Omikron::ani_node_by_id(*character, animation_id)};
  if (loop == nullptr) {
    pedestrian.flags &= ~K_FLAG_ACTIVITY;
    return;
  }
  *free_slot = Activity{.stop_point = stop_index,
      .target = add(pedestrian.segment_start, stop.offset),
      .yaw_degrees = stop.yaw_degrees,
      .begin = Omikron::ani_group_node(
          *character, loop->group, Omikron::AniAnimationKind::k_activity_begin),
      .loop = loop,
      .end = Omikron::ani_group_node(
          *character, loop->group, Omikron::AniAnimationKind::k_activity_end),
      .state = 0,
      .loops_remaining = stop.loop_count,
      .saved_animation_id = animation_id};
  m_stop_point_ids.at(stop_index) = 0;
  pedestrian.activity = static_cast<std::size_t>(std::distance(m_activities.begin(), free_slot));
}

bool Runtime::play_in_place(Agent& pedestrian, const float delta_units, Vec3& delta) {
  // Runtime 0x004561B0.
  const float previous{pedestrian.progress};
  float current{(pedestrian.playback_rate * delta_units) + previous};
  const bool playing{current < pedestrian.frame_count};
  if (!playing) {
    current = (current + 1.0F) - pedestrian.frame_count;
    pedestrian.instance = Vec3{.x = pedestrian.position.x,
        .y = pedestrian.position.y - pedestrian.ground_offset,
        .z = pedestrian.position.z};
    pedestrian.bob = 0.0F;
  }
  delta = root_motion(pedestrian, previous, current);
  pedestrian.progress = current;
  return playing;
}

void Runtime::advance_activity(Agent& pedestrian, const std::size_t slot, Activity& activity) {
  // Runtime 0x00456250: begin -> loop (repeated) -> end -> walk.
  const Omikron::AniAnimation* next{nullptr};
  bool finished{false};
  if (activity.state == 1) {
    activity.state = 2;
    next = activity.loop;
  } else if (activity.state == 2) {
    --activity.loops_remaining;
    if (activity.loops_remaining >= 0) {
      return;
    }
    if (activity.end == nullptr) {
      finished = true;
    } else {
      activity.state = 3;
      next = activity.end;
    }
  } else {
    finished = true;
  }
  if (!finished) {
    select_animation(pedestrian, next);
    return;
  }
  select_animation(
      pedestrian, random_animation(pedestrian.character_type, Omikron::AniAnimationKind::k_walk));
  m_stop_point_ids.at(activity.stop_point) = activity.saved_animation_id;
  m_activities.at(slot).reset();
  pedestrian.activity.reset();
  pedestrian.flags &= ~K_FLAG_ACTIVITY;
}

void Runtime::perform_activity(Agent& pedestrian, const float delta_units) {
  // Runtime 0x00455E90.
  if (!pedestrian.activity.has_value()) {
    pedestrian.flags &= ~K_FLAG_ACTIVITY;
    return;
  }
  const std::size_t slot{*pedestrian.activity};
  std::optional<Activity>& record{m_activities.at(slot)};
  if (!record.has_value()) {
    pedestrian.flags &= ~K_FLAG_ACTIVITY;
    return;
  }
  Activity& activity{*record};
  if (activity.state == 0) {
    const Vec3 start{pedestrian.position};
    float previous{pedestrian.progress};
    float current{(pedestrian.playback_rate * delta_units) + pedestrian.progress};
    if (pedestrian.frame_count <= current) {
      current = (current + 1.0F) - pedestrian.frame_count;
      previous = 0.0F;
      pedestrian.instance =
          Vec3{.x = start.x, .y = start.y - pedestrian.ground_offset, .z = start.z};
      pedestrian.bob = 0.0F;
    }
    const Vec3 motion{root_motion(pedestrian, previous, current)};
    pedestrian.progress = current;
    const float step_length{std::sqrt((motion.z * motion.z) + (motion.x * motion.x))};
    const float x{start.x + motion.x};
    const float y{start.y + motion.y};
    const float z{start.z + motion.z};
    pedestrian.position.x = x;
    const Vec3 to_target{
        .x = activity.target.x - x, .y = activity.target.y - y, .z = activity.target.z - z};
    const float horizontal{std::sqrt((to_target.z * to_target.z) + (to_target.x * to_target.x))};
    pedestrian.facing = facing_toward(to_target.x, to_target.z);
    pedestrian.bob += motion.y;
    if (horizontal < pedestrian.radius) {
      pedestrian.instance = Vec3{.x = activity.target.x,
          .y = activity.target.y - pedestrian.ground_offset,
          .z = activity.target.z};
      pedestrian.position = activity.target;
      activity.state = 1;
      const Omikron::AniAnimation* animation{activity.begin};
      if (animation == nullptr) {
        activity.state = 2;
        animation = activity.loop;
      }
      pedestrian.facing = App::Runtime::rotation_y(activity.yaw_degrees * K_DEGREES_TO_RADIANS);
      select_animation(pedestrian, animation);
      return;
    }
    const float ground{((to_target.y * step_length) / horizontal) + y};
    pedestrian.position = Vec3{.x = x, .y = ground, .z = z};
    pedestrian.instance =
        Vec3{.x = x, .y = (pedestrian.bob - pedestrian.ground_offset) + ground, .z = z};
    return;
  }
  if (activity.state < 1 || activity.state > 3) {
    return;
  }
  Vec3 delta{};
  if (play_in_place(pedestrian, delta_units, delta)) {
    pedestrian.instance = add(pedestrian.instance, delta);
    pedestrian.position = add(pedestrian.position, delta);
    return;
  }
  advance_activity(pedestrian, slot, activity);
  static_cast<void>(play_in_place(pedestrian, delta_units, delta));
  pedestrian.instance = add(pedestrian.instance, delta);
}

void Runtime::tick(const float delta_units) {
  APP_PROFILE_FUNCTION();
  if (m_network == nullptr || delta_units <= 0.0F) {
    return;
  }
  for (std::size_t index{0}; index < m_agents.size(); ++index) {
    const std::uint32_t flags{m_agents.at(index).flags};
    if ((flags & K_FLAG_ON_NETWORK) == 0U) {
      continue;
    }
    // Passersby precede sliders in the agent list, matching Runtime's two
    // service loops (0x00454BB0, then 0x00456530 per slider slot).
    if (m_agents.at(index).kind == AgentKind::k_slider) {
      drive(index, delta_units);
    } else if ((flags & K_FLAG_ACTIVITY) == 0U) {
      walk(index, delta_units);
    } else {
      advance_target(index, delta_units);
      perform_activity(m_agents.at(index), delta_units);
    }
  }
}

std::expected<std::optional<PopulationRequest>, std::string> load_area_population(
    const Omikron::IamAreaRecord& area,
    const std::uint8_t street_activity,
    const Character::Runtime::ModelLoader& model_loader) {
  using Result = std::expected<std::optional<PopulationRequest>, std::string>;
  APP_PROFILE_FUNCTION();
  const std::string network_name{area.options_opt_name()};
  const std::string animation_name{area.animation_ani_name()};
  if (network_name.empty() || animation_name.empty()) {
    return std::optional<PopulationRequest>{};
  }

  auto network_file{
      load_game_file(std::filesystem::path{"TRAJECTOIRES"} / (network_name + ".OPT"))};
  if (!network_file) {
    return Result{
        std::unexpect, fmt::format("trajectories '{}': {}", network_name, network_file.error())};
  }
  auto network{Omikron::TrajectoryOpt::load(network_file->bytes)};
  if (!network) {
    return Result{
        std::unexpect, fmt::format("trajectories '{}': {}", network_name, network.error())};
  }
  auto animation_file{load_game_file(std::filesystem::path{"ANIMS"} / (animation_name + ".ANI"))};
  if (!animation_file) {
    return Result{
        std::unexpect, fmt::format("animations '{}': {}", animation_name, animation_file.error())};
  }
  auto animations{Omikron::AnimationBankAni::load(animation_file->bytes)};
  if (!animations) {
    return Result{
        std::unexpect, fmt::format("animations '{}': {}", animation_name, animations.error())};
  }

  PopulationRequest request{
      .network = std::make_shared<const Omikron::TrajectoryOpt>(std::move(network).value()),
      .animations =
          std::make_shared<const Omikron::AnimationBankAni>(std::move(animations).value()),
      .male_models = {},
      .female_models = {},
      .slider_models = {},
      .moto_models = {},
      .street_activity = street_activity};
  const auto load_variants =
      [&model_loader](const std::span<const std::string_view> names,
          const std::uint32_t mask,
          std::vector<std::shared_ptr<const Character::ModelResource>>& models)
      -> std::expected<void, std::string> {
    for (std::size_t index{0}; index < names.size(); ++index) {
      if ((mask & (1U << index)) == 0U) {
        continue;
      }
      auto model{model_loader(names[index])};
      if (!model) {
        return std::expected<void, std::string>{std::unexpect, model.error()};
      }
      models.push_back(std::move(model).value());
    }
    return {};
  };
  if (auto loaded{
          load_variants(K_MALE_PASSER_MODELS, area.male_passer_variants(), request.male_models)};
      !loaded) {
    return Result{std::unexpect, loaded.error()};
  }
  if (auto loaded{load_variants(
          K_FEMALE_PASSER_MODELS, area.female_passer_variants(), request.female_models)};
      !loaded) {
    return Result{std::unexpect, loaded.error()};
  }
  // 0x00453450 loads vehicles only when the OPT has slider lanes; 0x0040EA10
  // forces the first `sli_fn` entry on.
  if (request.network->pedestrian_end < request.network->trajectory_end) {
    if (auto loaded{
            load_variants(K_SLIDER_MODELS, area.slider_variants() | 1U, request.slider_models)};
        !loaded) {
      return Result{std::unexpect, loaded.error()};
    }
    if (auto loaded{load_variants(K_MOTO_MODELS, area.moto_variants(), request.moto_models)};
        !loaded) {
      return Result{std::unexpect, loaded.error()};
    }
  }
  return std::optional<PopulationRequest>{std::move(request)};
}

}  // namespace App::Crowd
