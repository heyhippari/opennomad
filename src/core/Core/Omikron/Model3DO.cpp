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
      "3DO signature '{}', version {}.{}",
      std::string_view{model.header.signature.data(), model.header.signature.size()},
      model.header.version_major,
      model.header.version_minor);

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
    if (root != id_to_index.end()) {
      model.root_mesh_index = static_cast<std::int32_t>(root->second);
    } else {
      // Some synthetic/legacy fixtures predate recovery of root_mesh_id.
      // Keep a conservative fallback when there is exactly one parentless
      // object; real retail models with a valid +0xB4 take the path above.
      std::int32_t sole_parentless{-1};
      bool multiple_parentless{false};
      for (std::size_t index{0}; index < model.meshes.size(); ++index) {
        if (model.hierarchy_parent_index.at(index) != -1) {
          continue;
        }
        if (sole_parentless != -1) {
          multiple_parentless = true;
          break;
        }
        sole_parentless = static_cast<std::int32_t>(index);
      }

      if (sole_parentless == -1 || multiple_parentless) {
        return std::expected<Model3DOData, std::string>{std::unexpect,
            fmt::format("3DO root mesh ID {} does not resolve and no unique "
                        "parentless fallback exists",
                model.header.root_mesh_id)};
      }

      model.root_mesh_index = sole_parentless;
      App::Log::warn(LogCategory::Renderer,
          "3DO root mesh ID {} does not resolve; using sole parentless mesh "
          "'{}' (id {})",
          model.header.root_mesh_id,
          model.meshes.at(static_cast<std::size_t>(sole_parentless)).name,
          model.meshes.at(static_cast<std::size_t>(sole_parentless)).mesh_id);
    }
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

  // Recover Runtime's bind-pose object traversal. 0x0048D3B0 starts at the
  // model root and recursively follows first-child / next-sibling links.
  //
  // In the unanimated bind pose:
  //   root origin  = root.position
  //   child origin = parent origin + child.bone_position
  //
  // Rotation/animation matrices will later extend this same hierarchy;
  // keeping the derived origin separate from serialized fields avoids
  // baking presentation state back into the decoded data.
  model.hierarchy_reachable.assign(model.meshes.size(), std::uint8_t{0});
  model.bind_pose_world_origin.assign(model.meshes.size(), Vec3{});

  if (model.root_mesh_index != -1) {
    std::vector<std::uint8_t> visit_state(model.meshes.size(), std::uint8_t{0});
    std::function<std::expected<void, std::string>(std::size_t)> visit;
    visit = [&](const std::size_t index) -> std::expected<void, std::string> {
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
      model.hierarchy_reachable.at(index) = 1U;

      const MeshDescriptor& mesh{model.meshes.at(index)};
      Vec3& origin{model.bind_pose_world_origin.at(index)};

      if (std::cmp_equal(index, model.root_mesh_index)) {
        origin = mesh.position;
      } else {
        const std::int32_t parent_index{model.hierarchy_parent_index.at(index)};
        if (parent_index < 0) {
          return std::expected<void, std::string>{std::unexpect,
              fmt::format(
                  "reachable non-root mesh '{}' (id {}) has no parent", mesh.name, mesh.mesh_id)};
        }

        const Vec3& parent_origin{
            model.bind_pose_world_origin.at(static_cast<std::size_t>(parent_index))};
        origin = Vec3{.x = parent_origin.x + mesh.bone_position.x,
            .y = parent_origin.y + mesh.bone_position.y,
            .z = parent_origin.z + mesh.bone_position.z};
      }

      std::int32_t child_index{model.hierarchy_first_child_index.at(index)};
      std::size_t sibling_steps{0};
      while (child_index != -1) {
        // A malformed sibling loop must not hang model loading.
        ++sibling_steps;
        if (sibling_steps > model.meshes.size()) {
          return std::expected<void, std::string>{std::unexpect,
              fmt::format(
                  "cycle in child/sibling chain below mesh '{}' (id {})", mesh.name, mesh.mesh_id)};
        }

        const std::size_t child{static_cast<std::size_t>(child_index)};
        if (child >= model.meshes.size()) {
          return std::expected<void, std::string>{
              std::unexpect, "3DO child descriptor index is out of range"};
        }

        if (std::cmp_not_equal(model.hierarchy_parent_index.at(child), index)) {
          return std::expected<void, std::string>{std::unexpect,
              fmt::format("inconsistent 3DO hierarchy: '{}' (id {}) lists '{}' "
                          "(id {}) as a child, but that mesh names parent {}",
                  mesh.name,
                  mesh.mesh_id,
                  model.meshes.at(child).name,
                  model.meshes.at(child).mesh_id,
                  model.meshes.at(child).parent_id)};
        }

        if (auto result{visit(child)}; !result) {
          return result;
        }

        child_index = model.hierarchy_next_sibling_index.at(child);
      }

      visit_state.at(index) = 2U;
      return {};
    };

    if (auto result{visit(static_cast<std::size_t>(model.root_mesh_index))}; !result) {
      return std::expected<Model3DOData, std::string>{std::unexpect, std::move(result).error()};
    }

    std::size_t reachable_count{0};
    for (const std::uint8_t reachable : model.hierarchy_reachable) {
      reachable_count += reachable != 0U ? 1U : 0U;
    }

    App::Log::debug(LogCategory::Renderer,
        "3DO hierarchy — root={} index={} reachable={}/{}",
        model.header.root_mesh_id,
        model.root_mesh_index,
        reachable_count,
        model.meshes.size());
  }

  return model;
}

std::expected<std::vector<MaterialGroup>, std::string> Model3DO::build_static_geometry(
    const Model3DOData& model) {
  APP_PROFILE_FUNCTION();

  if (model.meshes.empty()) {
    return std::expected<std::vector<MaterialGroup>, std::string>{
        std::unexpect, "model contains no meshes"};
  }
  if (model.materials.empty()) {
    return std::expected<std::vector<MaterialGroup>, std::string>{
        std::unexpect, "model contains no material descriptors"};
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
    if (global_index >= model.vertices.size()) {
      return std::expected<void, std::string>{std::unexpect,
          fmt::format(
              "vertex index {} out of range ({} vertices)", global_index, model.vertices.size())};
    }
    const RawVertex& raw{model.vertices.at(global_index)};
    const Vec3 bind_origin{vertex_owner_index < model.bind_pose_world_origin.size()
                               ? model.bind_pose_world_origin.at(vertex_owner_index)
                               : Vec3{}};

    Vertex vertex{};
    vertex.position = {raw.position.x + bind_origin.x,
        raw.position.y + bind_origin.y,
        raw.position.z + bind_origin.z};
    vertex.normal = {raw.normal.x, raw.normal.y, raw.normal.z};
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
  header.version_minor = reader.read_u32();
  header.materials_offset = reader.read_u32();
  header.vertices_offset = reader.read_u32();
  header.triangles_offset = reader.read_u32();
  header.rectangles_offset = reader.read_u32();
  header.meshes_offset = reader.read_u32();
  header.doors_offset = reader.read_u32();
  header.cameras_offset = reader.read_u32();
  header.lights_offset = reader.read_u32();

  read_raw_array(reader, header.reserved_a);
  header.frame_count = reader.read_u32();
  read_raw_array(reader, header.reserved_b);
  header.root_mesh_id = reader.read_u32();
  read_raw_array(reader, header.reserved_b2);
  header.texture_count = reader.read_u32();
  read_raw_array(reader, header.reserved_c);

  header.object_count = reader.read_u32();
  header.unknown2 = reader.read_u32();
  header.triangle_count = reader.read_u32();
  header.rectangle_count = reader.read_u32();
  header.vertex_count = reader.read_u32();
  header.reserved2 = reader.read_u64();
  header.material_count = reader.read_u32();
  header.unknown3 = reader.read_u32();
  header.reserved3 = reader.read_u32();
  header.camera_count = reader.read_u32();
  header.mesh_count = reader.read_u32();
  header.door_count = reader.read_u32();
  header.light_count = reader.read_u32();
  header.lights_unknown1 = reader.read_u32();
  header.lights_unknown2 = reader.read_u32();

  read_raw_array(reader, header.unknown4);
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
  mesh.position = read_vec3(reader, k_scale_factor);
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
  mesh.box_extent_neg = read_vec3(reader, k_scale_factor);
  mesh.box_extent_pos = read_vec3(reader, k_scale_factor);
  mesh.unknown18 = reader.read_f32();
  mesh.unknown19 = reader.read_f32();
  mesh.unknown20 = reader.read_f32();
  mesh.bone_position = read_vec3(reader, k_scale_factor);
  return mesh;
}

RawVertex Model3DO::read_raw_vertex(BinaryReader& reader) {
  RawVertex vertex;
  vertex.position = read_vec3(reader, k_scale_factor);
  vertex.normal = read_vec3(reader, 1.0F);
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
  light.attenuation_end = reader.read_f32() * k_scale_factor;
  light.attenuation_start = reader.read_f32() * k_scale_factor;
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
    light.points.at(slot) = read_vec3(reader, k_scale_factor);
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

Vec3 Model3DO::read_vec3(BinaryReader& reader, const float scale) {
  // The file stores (x, z, y) in a left-handed frame (the second float is
  // the file's Z axis and the third is its Y axis). The reference importer
  // converts this to right-handed Z-up game space as (x, y, -z); turning
  // that into the renderer's right-handed Y-up frame via (x, z, -y) yields
  // the combined mapping (x, -z, -y).
  const float file_x{reader.read_f32()};
  const float file_z{reader.read_f32()};
  const float file_y{reader.read_f32()};
  return Vec3{.x = file_x * scale, .y = -(file_z * scale), .z = -(file_y * scale)};
}

}  // namespace App::Omikron
