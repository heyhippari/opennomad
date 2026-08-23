#include "Core/Sfx/SfxRuntime.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/SFX.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Sprite/SpriteInstance.hpp"
#include "Core/Sprite/SpriteRenderMode.hpp"

namespace App::Sfx {

namespace {

constexpr float K_DEGREES_TO_RADIANS{std::numbers::pi_v<float> / 180.0F};

App::Runtime::Vec3 add(const App::Runtime::Vec3& left, const App::Runtime::Vec3& right) {
  return App::Runtime::Vec3{.x = left.x + right.x, .y = left.y + right.y, .z = left.z + right.z};
}

App::Runtime::Vec3 multiply(const App::Runtime::Vec3& value, const float scalar) {
  return App::Runtime::Vec3{.x = value.x * scalar, .y = value.y * scalar, .z = value.z * scalar};
}

float length(const App::Runtime::Vec3& value) {
  return std::sqrt((value.x * value.x) + (value.y * value.y) + (value.z * value.z));
}

App::Runtime::Vec3 normalize(const App::Runtime::Vec3& value) {
  const float magnitude{length(value)};
  return magnitude > 0.0F ? multiply(value, 1.0F / magnitude) : App::Runtime::Vec3{};
}

App::Runtime::Vec3 cross(const App::Runtime::Vec3& left, const App::Runtime::Vec3& right) {
  return App::Runtime::Vec3{.x = (left.y * right.z) - (left.z * right.y),
      .y = (left.z * right.x) - (left.x * right.z),
      .z = (left.x * right.y) - (left.y * right.x)};
}

std::array<float, 3> normalized_rgb(const App::Runtime::Vec3& color) {
  constexpr float k_byte_to_float{1.0F / 255.0F};
  return {color.x * k_byte_to_float, color.y * k_byte_to_float, color.z * k_byte_to_float};
}

App::Runtime::Vec3 unpack_rgb(const std::uint32_t rgb) {
  return App::Runtime::Vec3{.x = static_cast<float>((rgb >> 16U) & 0xFFU),
      .y = static_cast<float>((rgb >> 8U) & 0xFFU),
      .z = static_cast<float>(rgb & 0xFFU)};
}

}  // namespace

struct Runtime::EmissionRequest {
  const Omikron::SfxDefinition* definition{nullptr};
  App::Runtime::Vec3 position{};
  float sound_countdown{0.0F};
  float emission_countdown{0.0F};
};

struct Runtime::Particle {
  Sprite::SpriteHandle sprite;
  float lifetime{0.0F};
  float elapsed{0.0F};
  App::Runtime::Vec3 velocity{};
  float acceleration_y{0.0F};
  float diffuse_alpha_delta{0.0F};
  App::Runtime::Vec3 color{};
  App::Runtime::Vec3 color_delta{};
  float rotation_velocity_radians{0.0F};
  float scale_velocity{0.0F};
  std::size_t frame_count{0};
};

Runtime::Runtime(const Omikron::SfxData& data, Host& host, Random01 random01)
    : m_data{data},
      m_host{host},
      m_injected_random{std::move(random01)},
      m_generator{static_cast<std::mt19937::result_type>(
          std::chrono::steady_clock::now().time_since_epoch().count())} {}

Runtime::~Runtime() {
  clear_particles();
}

std::expected<std::unique_ptr<Runtime>, std::string> Runtime::create(
    const Omikron::SfxData& data, Host& host, Random01 random01) {
  auto runtime{std::unique_ptr<Runtime>{new Runtime{data, host, std::move(random01)}}};
  if (auto linked{runtime->link()}; !linked) {
    return std::expected<std::unique_ptr<Runtime>, std::string>{
        std::unexpect, std::move(linked).error()};
  }
  static_cast<void>(runtime->trigger(1, -1));
  return runtime;
}

std::expected<void, std::string> Runtime::link() {
  std::unordered_map<std::int32_t, std::size_t> definition_ids;
  m_definition_sprite_resources.reserve(m_data.definitions.size());
  for (std::size_t index{0}; index < m_data.definitions.size(); ++index) {
    const Omikron::SfxDefinition& definition{m_data.definitions.at(index)};
    if (!definition_ids.emplace(definition.definition_id, index).second) {
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("duplicate SFX definition ID {}", definition.definition_id)};
    }
    if (definition.sprite_render_mode > 8U) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("SFX definition {} '{}' has invalid sprite render mode {}",
              definition.definition_id,
              definition.name,
              definition.sprite_render_mode)};
    }
    auto resource{m_host.resolve_sfx_sprite_id(definition.sprite_id())};
    if (!resource) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("SFX definition {} '{}' sprite ID {}: {}",
              definition.definition_id,
              definition.name,
              definition.sprite_id(),
              resource.error())};
    }
    m_definition_sprite_resources.push_back(resource.value());
  }

  std::unordered_map<std::int32_t, std::size_t> track_ids;
  for (std::size_t index{0}; index < m_data.tracks.size(); ++index) {
    if (!track_ids.emplace(m_data.tracks.at(index).track_id, index).second) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("duplicate SFX track ID {}", m_data.tracks.at(index).track_id)};
    }
  }
  std::unordered_map<std::int32_t, std::size_t> node_ids;
  for (std::size_t index{0}; index < m_data.nodes.size(); ++index) {
    if (!node_ids.emplace(m_data.nodes.at(index).node_id, index).second) {
      return std::expected<void, std::string>{
          std::unexpect, fmt::format("duplicate SFX node ID {}", m_data.nodes.at(index).node_id)};
    }
  }

  m_nodes.resize(m_data.nodes.size());
  m_definitions_by_node.resize(m_data.nodes.size());
  for (std::size_t index{0}; index < m_data.nodes.size(); ++index) {
    const Omikron::SfxNode& node{m_data.nodes.at(index)};
    if (!track_ids.contains(node.track_id)) {
      continue;  // Retail activation aborts unresolved/empty tracks.
    }
    if (node.fixed_definition_id > 0) {
      const auto found{definition_ids.find(node.fixed_definition_id)};
      if (found == definition_ids.end()) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format("SFX node {} fixed definition ID {} does not exist",
                node.node_id,
                node.fixed_definition_id)};
      }
      m_definitions_by_node.at(index) = &m_data.definitions.at(found->second);
    }
  }
  return {};
}

std::span<const NodeState> Runtime::nodes() const {
  return m_nodes;
}

Diagnostics Runtime::diagnostics() const {
  const std::size_t active_nodes{
      static_cast<std::size_t>(std::ranges::count_if(m_nodes, [](const NodeState& node) {
        return node.active();
      }))};
  return Diagnostics{.loaded = true,
      .definition_count = m_data.definitions.size(),
      .node_count = m_data.nodes.size(),
      .track_count = m_data.tracks.size(),
      .active_node_count = active_nodes,
      .queued_request_count = m_requests.size(),
      .active_particle_count = m_particles.size(),
      .attached_sprite_count = m_particles.size()};
}

float Runtime::random01() {
  if (m_injected_random) {
    return std::clamp(m_injected_random(), 0.0F, 1.0F);
  }
  return static_cast<float>(m_runtime_rand(m_generator)) * (1.0F / 32767.0F);
}

float Runtime::traversal_duration(const Omikron::SfxTrack& track) {
  if (track.points.empty()) {
    return 0.0F;
  }
  if (track.points.size() == 1U) {
    return track.points.front().segment_duration;
  }
  float duration{0.0F};
  for (std::size_t index{0}; index + 1U < track.points.size(); ++index) {
    duration += track.points.at(index).segment_duration;
  }
  return duration;
}

std::size_t Runtime::trigger(const std::int32_t type, const std::int32_t id) {
  std::size_t activated{0};
  for (std::size_t index{0}; index < m_data.nodes.size(); ++index) {
    const Omikron::SfxNode& node{m_data.nodes.at(index)};
    if (node.trigger_type != type || node.trigger_id != id) {
      continue;
    }
    std::vector<bool> activation_stack(m_nodes.size(), false);
    if (activate_node(index, activation_stack)) {
      ++activated;
    }
  }
  return activated;
}

// Retail activation recursively follows node references; the stack guard
// makes authored cycles terminate safely.
// NOLINTNEXTLINE(misc-no-recursion)
bool Runtime::activate_node(const std::size_t node_index, std::vector<bool>& activation_stack) {
  if (node_index >= m_data.nodes.size() || activation_stack.at(node_index)) {
    return false;
  }
  const Omikron::SfxNode& source{m_data.nodes.at(node_index)};
  const auto track_it{
      std::ranges::find(m_data.tracks, source.track_id, &Omikron::SfxTrack::track_id)};
  if (track_it == m_data.tracks.end() || track_it->points.empty()) {
    return false;
  }

  activation_stack.at(node_index) = true;
  // NOLINTNEXTLINE(misc-no-recursion)
  const auto activate_reference = [this, &activation_stack](
                                      const std::int32_t type, const std::int32_t id) {
    if (type != 1) {
      return;
    }
    const auto found{std::ranges::find(m_data.nodes, id, &Omikron::SfxNode::node_id)};
    if (found == m_data.nodes.end()) {
      return;
    }
    const std::size_t reference_index{
        static_cast<std::size_t>(std::distance(m_data.nodes.begin(), found))};
    if (!m_nodes.at(reference_index).active()) {
      static_cast<void>(activate_node(reference_index, activation_stack));
    }
  };
  activate_reference(source.anchor_reference_type, source.anchor_reference_id);
  for (const Omikron::SfxTrackPoint& point : track_it->points) {
    activate_reference(point.reference_type, point.reference_id);
  }

  NodeState& node{m_nodes.at(node_index)};
  node.flags = source.flags | 0x01U;
  if ((source.flags & 0x04U) != 0U) {
    node.flags |= 0x08U;
  } else {
    node.flags &= ~0x08U;
  }
  if (source.startup_delay != 0.0F) {
    node.flags |= 0x20U;
  } else {
    node.flags &= ~0x20U;
  }
  node.elapsed = 0.0F;
  node.repeat_index = 1;
  node.track_duration = traversal_duration(*track_it);
  node.current_point_index =
      node.current_reverse() && track_it->points.size() > 1U ? track_it->points.size() - 2U : 0U;
  evaluate_node_position(node_index);
  activation_stack.at(node_index) = false;
  return true;
}

App::Runtime::Vec3 Runtime::evaluate_point(
    const Omikron::SfxNode& node, const Omikron::SfxTrackPoint& point) const {
  std::int32_t reference_type{point.reference_type};
  std::int32_t reference_id{point.reference_id};
  if (node.anchor_reference_type != 0 && point.reference_type != 0) {
    reference_type = node.anchor_reference_type;
    reference_id = node.anchor_reference_id;
  }
  if (reference_type == 0) {
    return point.position;
  }

  App::Runtime::Transform anchor{};
  if (reference_type == 1) {
    const auto found{std::ranges::find(m_data.nodes, reference_id, &Omikron::SfxNode::node_id)};
    if (found == m_data.nodes.end()) {
      return point.position;
    }
    const std::size_t index{static_cast<std::size_t>(std::distance(m_data.nodes.begin(), found))};
    anchor.translation = m_nodes.at(index).current_position;
  } else if (reference_type == 2) {
    const std::optional<App::Runtime::Transform> character{
        m_host.resolve_sfx_character_anchor(reference_id)};
    if (!character.has_value()) {
      return point.position;
    }
    anchor = character.value();
  } else if (reference_type == 3) {
    if (reference_id == -1) {
      return point.position;
    }
  } else {
    return point.position;
  }
  return add(App::Runtime::transform_vector(point.position, anchor.matrix), anchor.translation);
}

void Runtime::evaluate_node_position(const std::size_t node_index) {
  const Omikron::SfxNode& source{m_data.nodes.at(node_index)};
  NodeState& node{m_nodes.at(node_index)};
  const auto track_it{
      std::ranges::find(m_data.tracks, source.track_id, &Omikron::SfxTrack::track_id)};
  if (track_it == m_data.tracks.end() || track_it->points.empty()) {
    return;
  }
  if (track_it->points.size() == 1U) {
    node.current_point_index = 0U;
    node.current_position = evaluate_point(source, track_it->points.front());
    return;
  }

  float cumulative{0.0F};
  std::size_t segment{node.current_reverse() ? track_it->points.size() - 2U : 0U};
  if (node.current_reverse()) {
    for (std::size_t reverse_index{track_it->points.size() - 1U}; reverse_index > 0U;
        --reverse_index) {
      const std::size_t candidate{reverse_index - 1U};
      const float duration{track_it->points.at(candidate).segment_duration};
      segment = candidate;
      if (node.elapsed < cumulative + duration || candidate == 0U) {
        break;
      }
      cumulative += duration;
    }
  } else {
    for (std::size_t candidate{0}; candidate + 1U < track_it->points.size(); ++candidate) {
      const float duration{track_it->points.at(candidate).segment_duration};
      segment = candidate;
      if (node.elapsed < cumulative + duration || candidate + 2U == track_it->points.size()) {
        break;
      }
      cumulative += duration;
    }
  }

  const float duration{track_it->points.at(segment).segment_duration};
  const float amount{duration > 0.0F ? (node.elapsed - cumulative) / duration : 0.0F};
  const App::Runtime::Vec3 from{
      evaluate_point(source, track_it->points.at(node.current_reverse() ? segment + 1U : segment))};
  const App::Runtime::Vec3 to{
      evaluate_point(source, track_it->points.at(node.current_reverse() ? segment : segment + 1U))};
  node.current_point_index = segment;
  node.current_position = App::Runtime::Vec3{.x = from.x + ((to.x - from.x) * amount),
      .y = from.y + ((to.y - from.y) * amount),
      .z = from.z + ((to.z - from.z) * amount)};
}

void Runtime::service_node(const std::size_t node_index) {
  NodeState& node{m_nodes.at(node_index)};
  if (!node.active()) {
    return;
  }
  const Omikron::SfxNode& source{m_data.nodes.at(node_index)};
  const auto track_it{
      std::ranges::find(m_data.tracks, source.track_id, &Omikron::SfxTrack::track_id)};
  if (track_it == m_data.tracks.end() || track_it->points.empty()) {
    node.flags &= ~0x01U;
    return;
  }

  if (node.delaying()) {
    node.elapsed += 1.0F;
    if (node.elapsed < source.startup_delay) {
      return;
    }
    node.flags &= ~0x20U;
    node.elapsed = 0.0F;
    return;
  }

  evaluate_node_position(node_index);
  const Omikron::SfxDefinition* definition{m_definitions_by_node.at(node_index)};
  if (definition == nullptr) {
    const std::int32_t dynamic_id{track_it->points.at(node.current_point_index).definition_id};
    const auto found{
        std::ranges::find(m_data.definitions, dynamic_id, &Omikron::SfxDefinition::definition_id)};
    if (found != m_data.definitions.end()) {
      definition = &*found;
    }
  }
  if (definition != nullptr) {
    enqueue_request(*definition, node.current_position);
  }

  node.elapsed += 1.0F;
  if (node.elapsed < node.track_duration) {
    return;
  }
  if (node.repeat_index >= source.repeat_limit && source.repeat_limit != 999) {
    node.flags &= ~0x01U;
    return;
  }
  if ((node.flags & 0x10U) != 0U) {
    node.flags ^= 0x08U;
  }
  node.track_duration = traversal_duration(*track_it);
  node.elapsed = 0.0F;
  node.current_point_index =
      node.current_reverse() && track_it->points.size() > 1U ? track_it->points.size() - 2U : 0U;
  if (source.repeat_limit != 999) {
    ++node.repeat_index;
  }
}

void Runtime::enqueue_request(
    const Omikron::SfxDefinition& definition, const App::Runtime::Vec3 position) {
  if (m_requests.size() >= k_request_capacity) {
    if (!m_request_capacity_warned) {
      m_request_capacity_warned = true;
      App::Log::warn(LogCategory::Scenario, "SFX request capacity {} reached", k_request_capacity);
    }
    return;
  }
  m_requests.push_back(EmissionRequest{.definition = &definition,
      .position = position,
      .sound_countdown = definition.sound_delay,
      .emission_countdown = definition.emission_delay});
}

void Runtime::service_requests() {
  for (std::size_t index{0}; index < m_requests.size();) {
    EmissionRequest& request{m_requests.at(index)};
    if (request.sound_countdown >= 0.0F) {
      request.sound_countdown -= 1.0F;
      if (request.sound_countdown < 0.0F && request.definition->sound_id != 0x0000FFFF &&
          request.definition->sound_id != -1 && !m_sound_unsupported_warned) {
        m_sound_unsupported_warned = true;
        App::Log::warn(LogCategory::Audio,
            "SFX sound ID {} is preserved but playback is unsupported",
            request.definition->sound_id);
      }
    }
    if (request.emission_countdown >= 0.0F) {
      request.emission_countdown -= 1.0F;
      if (request.emission_countdown < 0.0F) {
        emit_burst(*request.definition, request.position);
      }
    }
    if (request.sound_countdown < 0.0F && request.emission_countdown < 0.0F) {
      m_requests.erase(m_requests.begin() + static_cast<std::ptrdiff_t>(index));
    } else {
      ++index;
    }
  }
}

void Runtime::emit_burst(
    const Omikron::SfxDefinition& definition, const App::Runtime::Vec3 position) {
  const std::int32_t count{std::max<std::int32_t>(definition.spawn_count, 0)};
  for (std::int32_t index{0}; index < count; ++index) {
    if (m_particles.size() >= k_particle_capacity) {
      if (!m_particle_capacity_warned) {
        m_particle_capacity_warned = true;
        App::Log::warn(
            LogCategory::Scenario, "SFX particle capacity {} reached", k_particle_capacity);
      }
      return;
    }
    create_particle(definition, position);
  }
}

void Runtime::create_particle(
    const Omikron::SfxDefinition& definition, const App::Runtime::Vec3 position) {
  const auto definition_it{std::ranges::find(
      m_data.definitions, definition.definition_id, &Omikron::SfxDefinition::definition_id)};
  if (definition_it == m_data.definitions.end()) {
    return;
  }
  const std::size_t definition_index{
      static_cast<std::size_t>(std::distance(m_data.definitions.begin(), definition_it))};
  auto spawned{
      m_host.spawn_sfx_sprite(m_definition_sprite_resources.at(definition_index), position)};
  if (!spawned) {
    App::Log::warn(LogCategory::Scenario,
        "SFX definition {} '{}' particle spawn failed: {}",
        definition.definition_id,
        definition.name,
        spawned.error());
    return;
  }
  Sprite::SpriteInstance* sprite{m_host.find_sfx_sprite(spawned->handle)};
  if (sprite == nullptr) {
    m_host.destroy_sfx_sprite(spawned->handle);
    return;
  }

  float lifetime{definition.lifetime};
  if ((definition.flags & 0x0100U) != 0U) {
    lifetime += random01() * definition.lifetime * 0.1F;
  }
  if (lifetime <= 0.0F) {
    m_host.destroy_sfx_sprite(spawned->handle);
    return;
  }

  sprite->frame_index = 0U;
  sprite->position = {position.x, position.y, position.z};
  sprite->scale_x = definition.initial_scale;
  sprite->scale_y = definition.initial_scale;
  sprite->render_mode = static_cast<Sprite::SpriteRenderMode>(definition.sprite_render_mode);
  sprite->type = definition.sprite_render_mode;
  const App::Runtime::Vec3 start_color{unpack_rgb(definition.start_color_rgb)};
  const App::Runtime::Vec3 end_color{unpack_rgb(definition.end_color_rgb)};
  sprite->tint = normalized_rgb(start_color);
  sprite->rotation =
      (definition.flags & 0x0010U) != 0U ? random01() * 360.0F * K_DEGREES_TO_RADIANS : 0.0F;
  sprite->diffuse_alpha =
      (definition.flags & 0x0002U) == 0U ? 0.5F : sprite->diffuse_alpha;

  float scale_velocity{0.0F};
  if ((definition.flags & 0x0004U) != 0U) {
    scale_velocity = definition.initial_scale / lifetime;
  } else if ((definition.flags & 0x2000U) != 0U) {
    scale_velocity = -definition.initial_scale / lifetime;
  }

  float acceleration_y{0.0F};
  const std::uint32_t acceleration_mode{definition.flags & 0x0600U};
  if (acceleration_mode == 0U) {
    acceleration_y = -definition.vertical_acceleration;
  } else if (acceleration_mode == 0x0200U) {
    acceleration_y = definition.vertical_acceleration;
  } else if (!m_acceleration_mode_warned) {
    m_acceleration_mode_warned = true;
    App::Log::warn(LogCategory::Scenario,
        "SFX acceleration flag mode {:#06x} is preserved but unsupported",
        acceleration_mode);
  }

  App::Runtime::Vec3 direction{definition.direction};
  if ((definition.flags & 0x0040U) != 0U) {
    direction.x += 2.0F * random01();
    direction.y += 2.0F * random01();
    direction.z += 2.0F * random01();
  }
  const float speed{length(direction)};
  App::Runtime::Vec3 velocity{};
  if (speed > 0.0F) {
    float theta_degrees{definition.cone_angle_degrees};
    if ((definition.flags & 0x1000U) != 0U) {
      theta_degrees += random01() * definition.cone_angle_degrees;
    }
    const float theta{theta_degrees * K_DEGREES_TO_RADIANS};
    const float phi{random01() * 360.0F * K_DEGREES_TO_RADIANS};
    const App::Runtime::Vec3 local{.x = speed * std::sin(theta) * std::cos(phi),
        .y = -speed * std::cos(theta),
        .z = speed * std::sin(theta) * std::sin(phi)};
    const App::Runtime::Vec3 axis{normalize(direction)};
    const App::Runtime::Vec3 helper{std::abs(axis.y) < 0.999F
                                        ? App::Runtime::Vec3{.x = 0.0F, .y = 1.0F, .z = 0.0F}
                                        : App::Runtime::Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F}};
    const App::Runtime::Vec3 basis_x{normalize(cross(helper, axis))};
    const App::Runtime::Vec3 basis_y{multiply(axis, -1.0F)};
    const App::Runtime::Vec3 basis_z{normalize(cross(basis_y, basis_x))};
    velocity = add(
        add(multiply(basis_x, local.x), multiply(basis_y, local.y)), multiply(basis_z, local.z));
  }

  m_particles.push_back(Particle{.sprite = spawned->handle,
      .lifetime = lifetime,
      .elapsed = 0.0F,
      .velocity = velocity,
      .acceleration_y = acceleration_y,
      .diffuse_alpha_delta = 0.0F,
      .color = start_color,
      .color_delta = App::Runtime::Vec3{.x = (end_color.x - start_color.x) / lifetime,
          .y = (end_color.y - start_color.y) / lifetime,
          .z = (end_color.z - start_color.z) / lifetime},
      .rotation_velocity_radians = definition.angular_velocity_degrees * K_DEGREES_TO_RADIANS,
      .scale_velocity = scale_velocity,
      .frame_count = spawned->frame_count});
}

void Runtime::service_particles() {
  for (std::size_t index{0}; index < m_particles.size();) {
    Particle& particle{m_particles.at(index)};
    if (particle.elapsed >= particle.lifetime) {
      m_host.destroy_sfx_sprite(particle.sprite);
      m_particles.erase(m_particles.begin() + static_cast<std::ptrdiff_t>(index));
      continue;
    }
    Sprite::SpriteInstance* sprite{m_host.find_sfx_sprite(particle.sprite)};
    if (sprite == nullptr) {
      m_particles.erase(m_particles.begin() + static_cast<std::ptrdiff_t>(index));
      continue;
    }
    sprite->position.at(0) += particle.velocity.x;
    sprite->position.at(1) += particle.velocity.y;
    sprite->position.at(2) += particle.velocity.z;
    sprite->rotation += particle.rotation_velocity_radians;
    sprite->scale_x += particle.scale_velocity;
    sprite->scale_y += particle.scale_velocity;
    particle.color = add(particle.color, particle.color_delta);
    sprite->tint = normalized_rgb(particle.color);
    particle.velocity.y += particle.acceleration_y;
    if (particle.frame_count > 0U) {
      const float frame_value{
          static_cast<float>(particle.frame_count - 1U) * particle.elapsed / particle.lifetime};
      sprite->frame_index = static_cast<std::uint16_t>(std::trunc(frame_value));
    }
    particle.elapsed += 1.0F;
    ++index;
  }
}

void Runtime::step() {
  for (std::size_t index{0}; index < m_nodes.size(); ++index) {
    service_node(index);
  }
  service_requests();
  service_particles();
}

void Runtime::tick(const float real_delta_seconds) {
  if (real_delta_seconds <= 0.0F) {
    return;
  }
  m_accumulator += real_delta_seconds;
  while (m_accumulator >= k_fixed_step_seconds) {
    step();
    m_accumulator -= k_fixed_step_seconds;
  }
}

void Runtime::clear_particles() {
  for (const Particle& particle : m_particles) {
    m_host.destroy_sfx_sprite(particle.sprite);
  }
  m_particles.clear();
  m_requests.clear();
  m_nodes.clear();
}

}  // namespace App::Sfx
