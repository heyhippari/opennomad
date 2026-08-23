#include "Core/Omikron/Model3DO.hpp"

#include <fmt/format.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <flat_map>
#include <flat_set>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"
#include "Core/Omikron/BinaryReader.hpp"
#include "Core/RuntimeMath.hpp"
#include "Core/Vertex.hpp"

namespace App::Omikron {

namespace {

constexpr std::size_t K_TRIANGLE_SIZE{28};
constexpr std::size_t K_RECTANGLE_SIZE{32};
constexpr std::uint32_t K_MAX_MESH_COUNT{65536};
constexpr std::uint32_t K_MAX_LIGHT_COUNT{65536};
constexpr std::size_t K_MAX_VERTEX_COUNT{static_cast<std::size_t>(4U) * 1024U * 1024U};

/// Bit 15 of a triangle vertex reference flags a parent-block index.
constexpr std::uint16_t K_PARENTED_FLAG{0x8000U};
/// Bits 0-9 contain the vertex index. Bits 10-14 are not interpreted by the
/// reference importer and must not become part of the index.
constexpr std::uint16_t K_TRIANGLE_VERTEX_INDEX_MASK{0x03FFU};

/// Reads count bytes into a fixed-size byte array.
template <std::size_t Size>
void read_raw_array(BinaryReader& reader, std::array<std::byte, Size>& target) {
  const std::span<const std::byte> bytes{reader.read_bytes(target.size())};
  for (std::size_t index{0}; index < bytes.size(); ++index) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    target.at(index) = bytes[index];
  }
}

}  // namespace

std::expected<Model3DOData, std::string> Model3DO::load(const std::span<const std::byte> data) {
  APP_PROFILE_FUNCTION();

  BinaryReader reader{data};
  Model3DOData model;

  read_header(reader, model.header);
  if (reader.has_error()) {
    return std::expected<Model3DOData, std::string>{std::unexpect, reader.error()};
  }

  App::Log::debug(LogCategory::Renderer,
      "3DO signature '{}', version {}, root offset {:#x}",
      std::string_view{model.header.signature.data(), model.header.signature.size()},
      model.header.version_major,
      model.header.root_offset);

  if (model.header.mesh_count > K_MAX_MESH_COUNT) {
    return std::expected<Model3DOData, std::string>{
        std::unexpect, fmt::format("implausible mesh count {}", model.header.mesh_count)};
  }

  // Materials (texture descriptors).
  reader.seek(model.header.materials_offset);
  model.materials.reserve(model.header.material_count);
  for (std::uint32_t index{0}; index < model.header.material_count; ++index) {
    model.materials.push_back(read_material(reader));
  }
  if (reader.has_error()) {
    return std::expected<Model3DOData, std::string>{
        std::unexpect, fmt::format("materials: {}", reader.error())};
  }

  // Mesh descriptors; compute absolute element offsets as we go.
  reader.seek(model.header.meshes_offset);
  model.meshes.reserve(model.header.mesh_count);
  std::size_t vertex_base{0};
  std::size_t triangle_byte_offset{0};
  std::size_t rectangle_byte_offset{0};
  for (std::uint32_t index{0}; index < model.header.mesh_count; ++index) {
    MeshDescriptor mesh{read_mesh_descriptor(reader)};
    mesh.vertex_base = vertex_base;
    mesh.triangle_byte_offset = triangle_byte_offset;
    mesh.rectangle_byte_offset = rectangle_byte_offset;
    vertex_base += mesh.vertex_count;
    triangle_byte_offset += static_cast<std::size_t>(mesh.triangle_count) * K_TRIANGLE_SIZE;
    rectangle_byte_offset += static_cast<std::size_t>(mesh.rectangle_count) * K_RECTANGLE_SIZE;
    model.meshes.push_back(std::move(mesh));
  }
  if (reader.has_error()) {
    return std::expected<Model3DOData, std::string>{
        std::unexpect, fmt::format("mesh descriptors: {}", reader.error())};
  }
  if (vertex_base > K_MAX_VERTEX_COUNT) {
    return std::expected<Model3DOData, std::string>{
        std::unexpect, fmt::format("implausible vertex count {}", vertex_base)};
  }

  // Vertices (one contiguous block for the whole file).
  reader.seek(model.header.vertices_offset);
  model.vertices.reserve(vertex_base);
  for (std::size_t index{0}; index < vertex_base; ++index) {
    model.vertices.push_back(read_raw_vertex(reader));
  }
  if (reader.has_error()) {
    return std::expected<Model3DOData, std::string>{
        std::unexpect, fmt::format("vertices: {}", reader.error())};
  }

  // Polygons of every mesh, in mesh order.
  model.polygons.reserve(model.meshes.size());
  for (const MeshDescriptor& mesh : model.meshes) {
    MeshPolygons polygons;
    reader.seek(
        static_cast<std::size_t>(model.header.triangles_offset) + mesh.triangle_byte_offset);
    for (std::uint32_t index{0}; index < mesh.triangle_count; ++index) {
      polygons.triangles.push_back(read_triangle(reader));
    }
    reader.seek(
        static_cast<std::size_t>(model.header.rectangles_offset) + mesh.rectangle_byte_offset);
    for (std::uint32_t index{0}; index < mesh.rectangle_count; ++index) {
      polygons.rectangles.push_back(read_rectangle(reader));
    }
    model.polygons.push_back(std::move(polygons));
  }
  if (reader.has_error()) {
    return std::expected<Model3DOData, std::string>{
        std::unexpect, fmt::format("polygons: {}", reader.error())};
  }

  // Explicit light records. Only the second count field (lights_unknown2)
  // has records; the first one (lights_unknown1, "mesh lights") has no
  // record section — that lighting is baked into the vertex colours.
  if (model.header.lights_unknown1 > 0U) {
    App::Log::debug(LogCategory::Renderer,
        "3DO header reports {} mesh lights (baked into vertex colours)",
        model.header.lights_unknown1);
  }
  if (model.header.lights_unknown2 > K_MAX_LIGHT_COUNT) {
    return std::expected<Model3DOData, std::string>{
        std::unexpect, fmt::format("implausible light count {}", model.header.lights_unknown2)};
  }
  if (model.header.lights_unknown2 > 0U && model.header.lights_offset == 0U) {
    return std::expected<Model3DOData, std::string>{std::unexpect,
        fmt::format("lights: {} records but the lights offset is 0", model.header.lights_unknown2)};
  }
  reader.seek(model.header.lights_offset);
  model.lights.reserve(model.header.lights_unknown2);
  for (std::uint32_t index{0}; index < model.header.lights_unknown2; ++index) {
    model.lights.push_back(read_light(reader));
  }
  if (reader.has_error()) {
    return std::expected<Model3DOData, std::string>{
        std::unexpect, fmt::format("lights: {}", reader.error())};
  }

  // Runtime converts the serialized mesh IDs into object pointers while
  // loading a 3DO. Keep the equivalent descriptor-index relationships here.
  std::flat_map<std::uint32_t, std::size_t> id_to_index;
  for (std::size_t index{0}; index < model.meshes.size(); ++index) {
    const MeshDescriptor& mesh{model.meshes.at(index)};
    const auto [unused, inserted]{id_to_index.try_emplace(mesh.mesh_id, index)};
    if (!inserted) {
      return std::expected<Model3DOData, std::string>{
          std::unexpect, fmt::format("duplicate mesh ID {} ('{}')", mesh.mesh_id, mesh.name)};
    }
  }

  const auto resolve_link = [&id_to_index](const std::int32_t id,
                                const std::string_view relation,
                                const MeshDescriptor& owner) -> std::int32_t {
    if (id == -1) {
      return -1;
    }

    const auto found{id_to_index.find(static_cast<std::uint32_t>(id))};
    if (found == id_to_index.end()) {
      App::Log::warn(LogCategory::Renderer,
          "mesh '{}' (id {}) references unknown {} mesh {}",
          owner.name,
          owner.mesh_id,
          relation,
          id);
      return -1;
    }
    return static_cast<std::int32_t>(found->second);
  };

  model.hierarchy_parent_index.reserve(model.meshes.size());
  model.hierarchy_first_child_index.reserve(model.meshes.size());
  model.hierarchy_next_sibling_index.reserve(model.meshes.size());
  for (const MeshDescriptor& mesh : model.meshes) {
    model.hierarchy_parent_index.push_back(resolve_link(mesh.parent_id, "parent", mesh));
    model.hierarchy_first_child_index.push_back(
        resolve_link(mesh.first_child_id, "first-child", mesh));
    model.hierarchy_next_sibling_index.push_back(
        resolve_link(mesh.next_sibling_id, "next-sibling", mesh));
  }

  // Runtime resolves Serialized3DORootV4+0xB4 to one runtime object and
  // begins model traversal from that object.
  if (!model.meshes.empty()) {
    const auto root{id_to_index.find(model.header.root_mesh_id)};
    if (root == id_to_index.end()) {
      return std::expected<Model3DOData, std::string>{std::unexpect,
          fmt::format("3DO root mesh ID {} does not resolve", model.header.root_mesh_id)};
    }
    model.root_mesh_index = static_cast<std::int32_t>(root->second);
  }

  // Skin parents skip joint-only meshes on the way up.
  model.skin_parent_index.reserve(model.meshes.size());
  for (std::size_t index{0}; index < model.meshes.size(); ++index) {
    std::int32_t skin_parent{-1};
    const std::int32_t hierarchy_parent{model.hierarchy_parent_index.at(index)};
    if (hierarchy_parent != -1) {
      std::int32_t candidate{hierarchy_parent};
      while (candidate != -1 && has_flag(model.meshes.at(static_cast<std::size_t>(candidate)).flags,
                                    MeshFlags::k_joint_only)) {
        candidate = model.hierarchy_parent_index.at(static_cast<std::size_t>(candidate));
      }
      skin_parent = candidate;
    }
    model.skin_parent_index.push_back(skin_parent);
  }

  // Initialize mutable Runtime object state without modifying serialized
  // descriptors. Runtime's serialized object stride is 0x8C; its expanded
  // runtime object is 0xB8 and begins with identity orientation/unit scale.
  model.hierarchy_reachable.assign(model.meshes.size(), std::uint8_t{0});
  model.runtime_objects.reserve(model.meshes.size());
  for (std::size_t index{0}; index < model.meshes.size(); ++index) {
    const MeshDescriptor& mesh{model.meshes.at(index)};
    const bool top_level{mesh.parent_id == -1};
    model.runtime_objects.push_back(Model3DOData::RuntimeObjectState{
        .local_offset = top_level ? mesh.position : mesh.bone_position,
        .local_matrix = Runtime::Matrix3::identity(),
        .animation_matrix = std::nullopt,
        .scale = {.x = 1.0F, .y = 1.0F, .z = 1.0F},
        .world_matrix = Runtime::Matrix3::identity(),
        .world_translation = {}});
  }

  if (auto resolved{resolve_runtime_transforms(model)}; !resolved) {
    return std::expected<Model3DOData, std::string>{std::unexpect, std::move(resolved).error()};
  }

  return model;
}

std::expected<void, std::string> Model3DO::resolve_runtime_transforms(Model3DOData& model) {
  auto resolved{resolve_runtime_transforms(model, std::span{model.runtime_objects})};
  if (!resolved) {
    return resolved;
  }

  model.hierarchy_reachable.assign(model.meshes.size(), std::uint8_t{0});
  if (model.root_mesh_index == -1) {
    return {};
  }
  std::vector<std::size_t> stack{static_cast<std::size_t>(model.root_mesh_index)};
  while (!stack.empty()) {
    const std::size_t index{stack.back()};
    stack.pop_back();
    if (model.hierarchy_reachable.at(index) != 0U) {
      continue;
    }
    model.hierarchy_reachable.at(index) = 1U;
    const std::int32_t sibling{model.hierarchy_next_sibling_index.at(index)};
    if (sibling != -1) {
      stack.push_back(static_cast<std::size_t>(sibling));
    }
    const std::int32_t child{model.hierarchy_first_child_index.at(index)};
    if (child != -1) {
      stack.push_back(static_cast<std::size_t>(child));
    }
  }
  return {};
}

std::expected<void, std::string> Model3DO::resolve_runtime_transforms(
    const Model3DOData& model, const std::span<Model3DOData::RuntimeObjectState> runtime_objects) {
  if (runtime_objects.size() != model.meshes.size()) {
    return std::expected<void, std::string>{std::unexpect,
        fmt::format("3DO runtime object count {} does not match mesh count {}",
            runtime_objects.size(),
            model.meshes.size())};
  }

  if (model.root_mesh_index != -1) {
    std::vector<std::uint8_t> visit_state(model.meshes.size(), std::uint8_t{0});
    std::function<std::expected<void, std::string>(std::size_t)> visit_object;
    std::function<std::expected<void, std::string>(std::int32_t, std::int32_t)> visit_siblings;

    visit_object = [&](const std::size_t index) -> std::expected<void, std::string> {
      if (index >= model.meshes.size()) {
        return std::expected<void, std::string>{
            std::unexpect, "3DO hierarchy index is outside the mesh table"};
      }

      if (visit_state.at(index) == 1U) {
        return std::expected<void, std::string>{std::unexpect,
            fmt::format("cycle in 3DO hierarchy at mesh '{}' (id {})",
                model.meshes.at(index).name,
                model.meshes.at(index).mesh_id)};
      }
      if (visit_state.at(index) == 2U) {
        return {};
      }

      visit_state.at(index) = 1U;
      // std::span has no bounds-checked at(); the size and traversal index are validated above.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      Model3DOData::RuntimeObjectState& object{runtime_objects[index]};
      Runtime::Matrix3 effective_local{object.local_matrix};
      if (object.animation_matrix.has_value()) {
        effective_local = Runtime::multiply(effective_local, object.animation_matrix.value());
      }

      const std::int32_t parent_index{model.hierarchy_parent_index.at(index)};
      if (parent_index < 0) {
        object.world_matrix = effective_local;
        object.world_translation = object.local_offset;
      } else {
        const Model3DOData::RuntimeObjectState& parent{
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
            runtime_objects[static_cast<std::size_t>(parent_index)]};
        const Runtime::Transform composed{
            Runtime::compose(Runtime::Transform{.matrix = effective_local,
                                 .translation = object.local_offset,
                                 .scale = object.scale},
                Runtime::Transform{.matrix = parent.world_matrix,
                    .translation = parent.world_translation,
                    .scale = parent.scale})};
        object.world_matrix = composed.matrix;
        object.world_translation = composed.translation;
      }

      if (auto result{visit_siblings(
              model.hierarchy_first_child_index.at(index), static_cast<std::int32_t>(index))};
          !result) {
        return result;
      }

      visit_state.at(index) = 2U;
      return {};
    };

    visit_siblings = [&](std::int32_t sibling_index,
                         const std::int32_t expected_parent) -> std::expected<void, std::string> {
      std::size_t sibling_steps{0};
      while (sibling_index != -1) {
        // A malformed sibling loop must not hang model loading.
        ++sibling_steps;
        if (sibling_steps > model.meshes.size()) {
          const std::string owner{
              expected_parent < 0
                  ? "top-level object chain"
                  : fmt::format("child chain below mesh '{}' (id {})",
                        model.meshes.at(static_cast<std::size_t>(expected_parent)).name,
                        model.meshes.at(static_cast<std::size_t>(expected_parent)).mesh_id)};
          return std::expected<void, std::string>{
              std::unexpect, fmt::format("cycle in 3DO sibling chain ({})", owner)};
        }

        const std::size_t sibling{static_cast<std::size_t>(sibling_index)};
        if (sibling >= model.meshes.size()) {
          return std::expected<void, std::string>{
              std::unexpect, "3DO sibling descriptor index is out of range"};
        }

        const MeshDescriptor& mesh{model.meshes.at(sibling)};
        const bool resolved_parent_matches{
            model.hierarchy_parent_index.at(sibling) == expected_parent};
        const bool serialized_top_level_matches{expected_parent != -1 || mesh.parent_id == -1};
        if (!resolved_parent_matches || !serialized_top_level_matches) {
          if (expected_parent == -1) {
            return std::expected<void, std::string>{std::unexpect,
                fmt::format("inconsistent 3DO hierarchy: top-level sibling '{}' (id {}) "
                            "names parent {}",
                    mesh.name,
                    mesh.mesh_id,
                    mesh.parent_id)};
          }
          const MeshDescriptor& parent{model.meshes.at(static_cast<std::size_t>(expected_parent))};
          return std::expected<void, std::string>{std::unexpect,
              fmt::format("inconsistent 3DO hierarchy: '{}' (id {}) lists '{}' "
                          "(id {}) as a child, but that mesh names parent {}",
                  parent.name,
                  parent.mesh_id,
                  mesh.name,
                  mesh.mesh_id,
                  mesh.parent_id)};
        }

        if (auto result{visit_object(sibling)}; !result) {
          return result;
        }

        sibling_index = model.hierarchy_next_sibling_index.at(sibling);
      }
      return {};
    };

    if (auto result{visit_siblings(model.root_mesh_index, -1)}; !result) {
      return std::expected<void, std::string>{std::unexpect, std::move(result).error()};
    }
  }

  return {};
}

std::expected<std::vector<MaterialGroup>, std::string> Model3DO::build_static_geometry(
    const Model3DOData& model) {
  return build_posed_geometry(model, std::span<const Model3DOData::RuntimeObjectState>{
                                         model.runtime_objects});
}

std::expected<std::vector<MaterialGroup>, std::string> Model3DO::build_posed_geometry(
    const Model3DOData& model,
    const std::span<const Model3DOData::RuntimeObjectState> runtime_objects) {
  return build_posed_geometry(model, runtime_objects, std::span<const RawVertex>{model.vertices});
}

std::expected<std::vector<MaterialGroup>, std::string> Model3DO::build_posed_geometry(
    const Model3DOData& model,
    const std::span<const Model3DOData::RuntimeObjectState> runtime_objects,
    const std::span<const RawVertex> source_vertices) {
  APP_PROFILE_FUNCTION();

  if (model.meshes.empty()) {
    return std::expected<std::vector<MaterialGroup>, std::string>{
        std::unexpect, "model contains no meshes"};
  }
  if (model.materials.empty()) {
    return std::expected<std::vector<MaterialGroup>, std::string>{
        std::unexpect, "model contains no material descriptors"};
  }
  if (runtime_objects.size() != model.meshes.size()) {
    return std::expected<std::vector<MaterialGroup>, std::string>{
        std::unexpect, "posed Runtime object count does not match the model hierarchy"};
  }
  if (source_vertices.size() != model.vertices.size()) {
    return std::expected<std::vector<MaterialGroup>, std::string>{
        std::unexpect, "posed source vertex count does not match the model"};
  }

  std::vector<MaterialGroup> groups;
  using MaterialKey = std::pair<std::int32_t, std::uint32_t>;
  std::flat_map<MaterialKey, std::size_t> group_by_material;
  std::flat_set<std::int32_t> clamped_materials;

  const auto resolve_material = [&](const std::int32_t material_id) -> std::int32_t {
    if (material_id >= 0 && static_cast<std::size_t>(material_id) < model.materials.size()) {
      return material_id;
    }
    if (clamped_materials.insert(material_id).second) {
      App::Log::warn(LogCategory::Renderer,
          "3DO face references invalid material {} ({} materials); using material 0",
          material_id,
          model.materials.size());
    }
    return 0;
  };

  const auto group_for_material = [&](const std::int32_t material_id,
                                      const std::uint32_t render_flags) -> MaterialGroup& {
    const MaterialKey key{material_id, render_flags};
    const auto found{group_by_material.find(key)};
    if (found != group_by_material.end()) {
      return groups.at(found->second);
    }
    group_by_material.emplace(key, groups.size());
    groups.push_back(MaterialGroup{
        .material_id = material_id, .flags = render_flags, .vertices = {}, .indices = {}});
    return groups.at(groups.size() - 1U);
  };

  const auto emit_corner = [&](MaterialGroup& group,
                               const Material& material,
                               const std::size_t vertex_owner_index,
                               const std::size_t vertex_index,
                               const std::uint8_t texture_u,
                               const std::uint8_t texture_v) -> std::expected<void, std::string> {
    const MeshDescriptor& vertex_owner{model.meshes.at(vertex_owner_index)};

    if (vertex_index >= vertex_owner.vertex_count) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format("vertex index {} out of range for mesh '{}' ({} vertices)",
              vertex_index,
              vertex_owner.name,
              vertex_owner.vertex_count)};
    }

    const std::size_t global_index{vertex_owner.vertex_base + vertex_index};
    if (global_index >= source_vertices.size()) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format(
              "vertex index {} out of range ({} vertices)", global_index, source_vertices.size())};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) -- checked above; span has no at().
    const RawVertex& raw{source_vertices[global_index]};
    if (vertex_owner_index >= runtime_objects.size()) {
      return std::expected<void, std::string>{
          std::unexpect, "3DO vertex owner has no Runtime object transform"};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const Model3DOData::RuntimeObjectState& object{runtime_objects[vertex_owner_index]};
    const Runtime::Transform transform{.matrix = object.world_matrix,
        .translation = object.world_translation,
        .scale = object.scale};
    const Vec3 position{Runtime::transform_point(raw.position, transform)};
    const Vec3 normal{Runtime::transform_vector(raw.normal, object.world_matrix)};

    Vertex vertex{};
    vertex.position = {position.x, position.y, position.z};
    vertex.normal = {normal.x, normal.y, normal.z};
    constexpr float byte_to_float{1.0F / 255.0F};
    vertex.color = {
        static_cast<float>(raw.color_bgra.at(2)) * byte_to_float,
        static_cast<float>(raw.color_bgra.at(1)) * byte_to_float,
        static_cast<float>(raw.color_bgra.at(0)) * byte_to_float,
        static_cast<float>(raw.color_bgra.at(3)) * byte_to_float,
    };
    if (material.width != 0U && material.height != 0U) {
      vertex.uv = {static_cast<float>(texture_u) / static_cast<float>(material.width),
          static_cast<float>(texture_v) / static_cast<float>(material.height)};
    }
    group.vertices.push_back(vertex);
    group.indices.push_back(static_cast<std::uint32_t>(group.vertices.size() - 1U));
    return {};
  };

  for (std::size_t mesh_index{0}; mesh_index < model.meshes.size(); ++mesh_index) {
    const MeshDescriptor& mesh{model.meshes.at(mesh_index)};

    // Runtime traverses the object hierarchy beginning at root_mesh_id;
    // disconnected serialized descriptors are not submitted.
    if (mesh_index >= model.hierarchy_reachable.size() ||
        model.hierarchy_reachable.at(mesh_index) == 0U) {
      continue;
    }

    if (has_flag(mesh.flags, MeshFlags::k_invisible) ||
        has_flag(mesh.flags, MeshFlags::k_joint_only)) {
      continue;
    }
    const MeshPolygons& polygons{model.polygons.at(mesh_index)};

    // Triangles of skinned meshes may reference the nearest non-joint
    // parent's vertex block (the bind pose).
    std::optional<std::size_t> skin_parent_mesh_index;

    if (const std::int32_t skin_parent{model.skin_parent_index.at(mesh_index)}; skin_parent >= 0) {
      skin_parent_mesh_index = static_cast<std::size_t>(skin_parent);
    }

    for (const Triangle& triangle : polygons.triangles) {
      const std::int32_t material_id{resolve_material(triangle.material_id)};
      const Material& material{model.materials.at(static_cast<std::size_t>(material_id))};
      MaterialGroup& group{group_for_material(material_id, static_cast<std::uint32_t>(mesh.flags))};
      for (std::size_t corner{0}; corner < triangle.vertices.size(); ++corner) {
        const TriangleVertexRef& reference{triangle.vertices.at(corner)};
        if (reference.parented && !skin_parent_mesh_index.has_value()) {
          return std::expected<std::vector<MaterialGroup>, std::string>{std::unexpect,
              fmt::format("mesh '{}' contains a parented triangle vertex but has no skin parent",
                  mesh.name)};
        }
        const std::size_t vertex_owner_index{
            reference.parented ? skin_parent_mesh_index.value() : mesh_index};
        const auto result{emit_corner(group,
            material,
            vertex_owner_index,
            reference.index,
            triangle.uv.at(corner * 2U),
            triangle.uv.at((corner * 2U) + 1U))};
        if (!result) {
          return std::expected<std::vector<MaterialGroup>, std::string>{
              std::unexpect, result.error()};
        }
      }
    }

    for (const Rectangle& rectangle : polygons.rectangles) {
      const std::int32_t material_id{resolve_material(rectangle.material_id)};
      const Material& material{model.materials.at(static_cast<std::size_t>(material_id))};
      MaterialGroup& group{group_for_material(material_id, static_cast<std::uint32_t>(mesh.flags))};

      // Each quad becomes two triangles: (0, 1, 2) and (0, 2, 3).
      const auto emit = [&](const std::uint16_t vertex_index,
                            const std::uint8_t texture_u,
                            const std::uint8_t texture_v) -> std::expected<void, std::string> {
        return emit_corner(group, material, mesh_index, vertex_index, texture_u, texture_v);
      };
      const auto first{emit(rectangle.vertices.at(0), rectangle.uv.at(0), rectangle.uv.at(1))};
      if (!first) {
        return std::expected<std::vector<MaterialGroup>, std::string>{std::unexpect, first.error()};
      }
      const auto second{emit(rectangle.vertices.at(1), rectangle.uv.at(2), rectangle.uv.at(3))};
      if (!second) {
        return std::expected<std::vector<MaterialGroup>, std::string>{
            std::unexpect, second.error()};
      }
      const auto third{emit(rectangle.vertices.at(2), rectangle.uv.at(4), rectangle.uv.at(5))};
      if (!third) {
        return std::expected<std::vector<MaterialGroup>, std::string>{std::unexpect, third.error()};
      }
      const auto fourth{emit(rectangle.vertices.at(0), rectangle.uv.at(0), rectangle.uv.at(1))};
      if (!fourth) {
        return std::expected<std::vector<MaterialGroup>, std::string>{
            std::unexpect, fourth.error()};
      }
      const auto fifth{emit(rectangle.vertices.at(2), rectangle.uv.at(4), rectangle.uv.at(5))};
      if (!fifth) {
        return std::expected<std::vector<MaterialGroup>, std::string>{std::unexpect, fifth.error()};
      }
      const auto sixth{emit(rectangle.vertices.at(3), rectangle.uv.at(6), rectangle.uv.at(7))};
      if (!sixth) {
        return std::expected<std::vector<MaterialGroup>, std::string>{std::unexpect, sixth.error()};
      }
    }
  }

  return groups;
}

void Model3DO::read_header(BinaryReader& reader, Header& header) {
  const std::span<const std::byte> signature{reader.read_bytes(header.signature.size())};
  for (std::size_t index{0}; index < signature.size(); ++index) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    header.signature.at(index) = static_cast<char>(signature[index]);
  }

  header.version_major = reader.read_u32();
  header.root_offset = reader.read_u32();
  header.materials_offset = reader.read_u32();
  header.vertices_offset = reader.read_u32();
  header.triangles_offset = reader.read_u32();
  header.rectangles_offset = reader.read_u32();
  header.meshes_offset = reader.read_u32();
  header.doors_offset = reader.read_u32();
  header.cameras_offset = reader.read_u32();
  header.lights_offset = reader.read_u32();

  // Runtime treats the 0x2C core directory and the 0x148-byte root as
  // separate structures. The old parser followed rootOffset and then still
  // consumed the former 0x174-byte combined directory+root layout.
  const std::size_t root{header.root_offset};

  reader.seek(root);
  read_raw_array(reader, header.reserved_a);  // +0x00..+0x47
  header.frame_count = reader.read_u32();     // +0x48
  read_raw_array(reader, header.reserved_b);  // +0x4C..+0xB3
  header.root_mesh_id = reader.read_u32();    // +0xB4

  // +0xB8 is a Runtime scalar whose higher-level semantic is still unknown.
  static_cast<void>(reader.read_f32());
  header.triangle_count = reader.read_u32();   // +0xBC
  header.rectangle_count = reader.read_u32();  // +0xC0
  header.vertex_count = reader.read_u32();     // +0xC4
  header.reserved2 = reader.read_u64();        // +0xC8
  header.material_count = reader.read_u32();   // +0xD0
  header.unknown3 = reader.read_u32();          // +0xD4
  header.reserved3 = reader.read_u32();         // +0xD8
  header.camera_count = reader.read_u32();      // +0xDC
  header.object_count = reader.read_u32();      // +0xE0
  header.unknown2 = reader.read_u32();           // +0xE4 relationship count
  header.light_count = reader.read_u32();       // +0xE8 serialized light count
  header.lights_unknown1 = reader.read_u32();   // +0xEC
  header.lights_unknown2 = reader.read_u32();   // +0xF0 processed light count
  read_raw_array(reader, header.unknown4);       // +0xF4..+0x147

  // Compatibility aliases retained until Header is cleaned up separately.
  // They are not additional serialized fields.
  header.mesh_count = header.object_count;
  header.texture_count = 0;
  header.door_count = 0;
}

Material Model3DO::read_material(BinaryReader& reader) {
  Material material;
  material.name = read_fixed_string(reader, 20);
  material.texture_name = read_fixed_string(reader, 20);
  material.palette_name = read_fixed_string(reader, 20);
  material.data_size = reader.read_u32();
  material.texture_page_index = reader.read_u16();
  material.texture_slot_index = reader.read_u16();
  material.palette_page_index = reader.read_u16();
  material.palette_slot_index = reader.read_u16();
  material.bits_per_pixel = reader.read_u16();
  material.atlas_u_offset = reader.read_u8();
  material.atlas_v_offset = reader.read_u8();
  material.width = reader.read_u16();
  material.height = reader.read_u16();
  return material;
}

MeshDescriptor Model3DO::read_mesh_descriptor(BinaryReader& reader) {
  MeshDescriptor mesh;
  mesh.flags = reader.read_u32();
  mesh.mover_flags = reader.read_u32();
  mesh.mesh_id = reader.read_u32();
  mesh.script_id = reader.read_u32();
  mesh.name = read_fixed_string(reader, 20);
  mesh.position = read_vec3(reader);
  mesh.parent_id = reader.read_i32();
  mesh.first_child_id = reader.read_i32();
  mesh.next_sibling_id = reader.read_i32();
  mesh.unknown07_count1 = reader.read_u32();
  mesh.vertex_count = reader.read_u32();
  mesh.triangle_count = reader.read_u32();
  mesh.rectangle_count = reader.read_u32();
  mesh.unknown08 = reader.read_f32();
  mesh.unknown09 = reader.read_f32();
  mesh.unknown10 = reader.read_f32();
  mesh.unknown11 = reader.read_f32();
  mesh.box_extent_neg = read_vec3(reader);
  mesh.box_extent_pos = read_vec3(reader);
  mesh.unknown18 = reader.read_f32();
  mesh.unknown19 = reader.read_f32();
  mesh.unknown20 = reader.read_f32();
  mesh.bone_position = read_vec3(reader);
  return mesh;
}

RawVertex Model3DO::read_raw_vertex(BinaryReader& reader) {
  RawVertex vertex;
  vertex.position = read_vec3(reader);
  vertex.normal = read_vec3(reader);
  vertex.unknown_t1 = reader.read_u32();
  // Colour is stored B, G, R, A (kept in file order; renderers convert).
  for (std::size_t channel{0}; channel < vertex.color_bgra.size(); ++channel) {
    vertex.color_bgra.at(channel) = reader.read_u8();
  }
  return vertex;
}

Triangle Model3DO::read_triangle(BinaryReader& reader) {
  Triangle triangle;
  for (std::size_t corner{0}; corner < triangle.vertices.size(); ++corner) {
    const std::uint16_t reference{reader.read_u16()};
    // Bit 15 flags that the index belongs to the skin parent's vertex block.
    triangle.vertices.at(corner).parented = (reference & K_PARENTED_FLAG) != 0U;
    triangle.vertices.at(corner).index =
        static_cast<std::uint16_t>(reference & K_TRIANGLE_VERTEX_INDEX_MASK);
  }
  for (std::size_t channel{0}; channel < triangle.uv.size(); ++channel) {
    triangle.uv.at(channel) = reader.read_u8();
  }
  triangle.material_id = reader.read_i32();
  for (std::size_t value{0}; value < triangle.unknown_ints.size(); ++value) {
    triangle.unknown_ints.at(value) = reader.read_i32();
  }
  return triangle;
}

Rectangle Model3DO::read_rectangle(BinaryReader& reader) {
  Rectangle rectangle;
  for (std::size_t corner{0}; corner < rectangle.vertices.size(); ++corner) {
    rectangle.vertices.at(corner) = reader.read_u16();
  }
  for (std::size_t channel{0}; channel < rectangle.uv.size(); ++channel) {
    rectangle.uv.at(channel) = reader.read_u8();
  }
  rectangle.material_id = reader.read_i32();
  for (std::size_t value{0}; value < rectangle.unknown_ints.size(); ++value) {
    rectangle.unknown_ints.at(value) = reader.read_i32();
  }
  return rectangle;
}

Light Model3DO::read_light(BinaryReader& reader) {
  Light light;
  light.flags = reader.read_u32();
  light.name = read_fixed_string(reader, 20);
  light.attenuation_end = reader.read_f32();
  light.attenuation_start = reader.read_f32();
  light.intensity = reader.read_f32();
  light.unknown4 = reader.read_f32();
  light.unknown5 = reader.read_f32();
  // Colour is stored B, G, R, A (kept in file order; renderers convert).
  for (std::size_t channel{0}; channel < light.color_bgra.size(); ++channel) {
    light.color_bgra.at(channel) = reader.read_u8();
  }
  // Six point slots: position, target, then four cone/frustum-shape points.
  // The 20 bytes after each point and the 64 trailing bytes are unresolved
  // (zero in practice) and are skipped.
  for (std::size_t slot{0}; slot < light.points.size(); ++slot) {
    light.points.at(slot) = read_vec3(reader);
    reader.skip(20);
  }
  reader.skip(64);
  return light;
}

Vec3 Light::direction() const {
  const Vec3& position{points.at(0)};
  const Vec3& target{points.at(1)};
  const float delta_x{target.x - position.x};
  const float delta_y{target.y - position.y};
  const float delta_z{target.z - position.z};
  const float length{std::sqrt((delta_x * delta_x) + (delta_y * delta_y) + (delta_z * delta_z))};
  if (length <= 0.0F) {
    return Vec3{};
  }
  return Vec3{.x = delta_x / length, .y = delta_y / length, .z = delta_z / length};
}

std::array<float, 4> Light::color_rgba() const {
  constexpr float byte_to_float{1.0F / 255.0F};
  return {static_cast<float>(color_bgra.at(2)) * byte_to_float,
      static_cast<float>(color_bgra.at(1)) * byte_to_float,
      static_cast<float>(color_bgra.at(0)) * byte_to_float,
      static_cast<float>(color_bgra.at(3)) * byte_to_float};
}

std::string Model3DO::read_fixed_string(BinaryReader& reader, const std::size_t length) {
  const std::span<const std::byte> bytes{reader.read_bytes(length)};
  std::string result;
  result.reserve(length);
  for (const std::byte byte : bytes) {
    const char character{static_cast<char>(byte)};
    if (character == '\0') {
      break;  // Strings are NUL-padded to their fixed width.
    }
    result.push_back(character);
  }
  return result;
}

Vec3 Model3DO::read_vec3(BinaryReader& reader) {
  // Runtime consumes serialized 3DO vectors as ordinary native XYZ floats.
  // Presentation basis conversion belongs exclusively at the renderer edge.
  return Vec3{.x = reader.read_f32(), .y = reader.read_f32(), .z = reader.read_f32()};
}

}  // namespace App::Omikron
