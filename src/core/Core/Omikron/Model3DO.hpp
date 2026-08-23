#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Core/RuntimeMath.hpp"
#include "Core/Vertex.hpp"

namespace App::Omikron {

class BinaryReader;

/// Bit flags carried by mesh descriptors.
enum MeshFlags : std::uint32_t {
  k_joint_only = 1U << 0,  ///< Joint-only mesh: no displayed geometry.
  k_vertex_lit = 1U << 2,  ///< Pre-baked vertex lighting.
  k_has_parent = 1U << 4,
  k_has_children = 1U << 5,
  k_alpha_testing = 1U << 11,
  k_alpha_blending = 1U << 12,
  k_additive = 1U << 13,
  k_subtractive = 1U << 14,
  k_mirror = 1U << 20,
  k_fps_arm = 1U << 21,
  k_face_morph = 1U << 22,
  k_invisible = 1U << 23,
  /// Runtime polygon submission adds the global cyclic U phase
  /// (Runtime.exe 0x00907304) to every polygon U coordinate when this bit is
  /// set. Confirmed in the triangle and quad submission paths at 0x004955A9,
  /// 0x00495A33 and 0x0049749C.
  k_uv_scroll_u = 1U << 24,

  /// Runtime polygon submission adds the global cyclic V phase
  /// (Runtime.exe 0x00907300) to every polygon V coordinate when this bit is
  /// set. This is independent of the U-scroll bit.
  k_uv_scroll_v = 1U << 25,

  // Importer-derived name; Runtime behavior still requires independent
  // verification before treating this as a modern environment-map shader
  // selector.
  k_environment_mapped = 1U << 26,
  k_underwater = 1U << 27,
  k_water_surface = 1U << 29,
  k_water_unknown = 1U << 30,
};

/// Tests a mesh flag word for a given flag.
[[nodiscard]] constexpr bool has_flag(const std::uint32_t flags, const MeshFlags flag) {
  return (flags & static_cast<std::uint32_t>(flag)) != 0U;
}

/// Per-draw Runtime UV translation selected by a mesh's independent U/V bits.
[[nodiscard]] constexpr std::array<float, 2> uv_scroll_offset(
    const std::uint32_t flags, const float u_phase, const float v_phase) {
  return {has_flag(flags, MeshFlags::k_uv_scroll_u) ? u_phase : 0.0F,
      has_flag(flags, MeshFlags::k_uv_scroll_v) ? v_phase : 0.0F};
}

/// How a mesh combines with the framebuffer, derived from its flags.
///
/// Mirrors the reference importer's shader selection: alpha blending wins
/// over alpha testing, and the additive/subtractive modifiers only take
/// effect together with alpha blending.
enum class BlendMode : std::uint8_t {
  k_opaque,       ///< Depth-tested, writes depth, no blending.
  k_alpha_test,   ///< Cutout: fragments below the alpha threshold are discarded.
  k_alpha_blend,  ///< Standard source-alpha blending.
  k_additive,     ///< Adds the source colour to the framebuffer.
  k_subtractive,  ///< Subtracts the source colour from the framebuffer.
};

/// Derives the blend mode a mesh's flag word maps to.
[[nodiscard]] constexpr BlendMode blend_mode(const std::uint32_t flags) {
  if (has_flag(flags, MeshFlags::k_alpha_blending)) {
    if (has_flag(flags, MeshFlags::k_subtractive)) {
      return BlendMode::k_subtractive;
    }
    if (has_flag(flags, MeshFlags::k_additive)) {
      return BlendMode::k_additive;
    }
    return BlendMode::k_alpha_blend;
  }
  if (has_flag(flags, MeshFlags::k_alpha_testing)) {
    return BlendMode::k_alpha_test;
  }
  return BlendMode::k_opaque;
}

/// Native Runtime XYZ vector. 3DO values are preserved exactly as serialized.
using Vec3 = Runtime::Vec3;

/// Parsed .3DO file header. Section offsets and counts are kept for future
/// expansion (doors and cameras are not decoded yet).
struct Header {
  std::array<char, 4> signature{};
  std::uint32_t version_major{0};
  /// Offset of Serialized3DORootV4 from the beginning of the OD3X core.
  std::uint32_t root_offset{0};
  std::uint32_t materials_offset{0};
  std::uint32_t vertices_offset{0};
  std::uint32_t triangles_offset{0};
  std::uint32_t rectangles_offset{0};
  std::uint32_t meshes_offset{0};
  std::uint32_t doors_offset{0};
  std::uint32_t cameras_offset{0};
  std::uint32_t lights_offset{0};
  /// Unparsed bytes Serialized3DORootV4+0x00..0x47.
  std::array<std::byte, 72> reserved_a{};
  /// Animation frame count (Serialized3DORootV4+0x48); 0 in all observed
  /// files. The frame-descriptor table has not been located yet, so the
  /// count is preserved for documentation only.
  std::uint32_t frame_count{0};
  /// Unparsed bytes 0x4C..0xB3.
  std::array<std::byte, 104> reserved_b{};
  /// Root runtime object ID (Serialized3DORootV4+0xB4).
  std::uint32_t root_mesh_id{0};
  /// Unparsed bytes 0xB8..0xCF.
  std::array<std::byte, 24> reserved_b2{};
  /// Texture count used by the original runtime (Serialized3DORootV4+0xD0);
  /// serialized as 0 in observed files. Preserved for documentation only.
  std::uint32_t texture_count{0};
  /// Unparsed bytes 0xD4..0xDF.
  std::array<std::byte, 12> reserved_c{};
  /// Object count (Serialized3DORootV4+0xE0). Not equal to mesh_count in
  /// observed files (Anekbah: 0 vs 20); mesh records remain driven by
  /// mesh_count below.
  std::uint32_t object_count{0};
  std::uint32_t unknown2{0};
  std::uint32_t triangle_count{0};
  std::uint32_t rectangle_count{0};
  std::uint32_t vertex_count{0};
  std::uint64_t reserved2{0};
  std::uint32_t material_count{0};
  std::uint32_t unknown3{0};
  std::uint32_t reserved3{0};
  std::uint32_t camera_count{0};
  std::uint32_t mesh_count{0};
  std::uint32_t door_count{0};
  std::uint32_t light_count{0};
  std::uint32_t lights_unknown1{0};
  std::uint32_t lights_unknown2{0};
  std::array<std::byte, 84> unknown4{};
};

/// One texture/material slot; the pixel data itself lives in the .3DT sidecar.
///
/// Field names and offsets follow the original runtime's Runtime3DOTexture
/// (0x50 bytes). The page/slot indices and atlas offsets are original-runtime
/// texture-page allocation state (the runtime packs textures into shared
/// 256x256 pages) and are parsed but not used by OpenNomad, which keeps one
/// independent GPU texture per material.
struct Material {
  /// First 20-byte region (unknown_00 in the runtime). Observed files store
  /// the material name here, without extension.
  std::string name;
  /// Original runtime textureName (+0x14): texture/cache identifier, e.g.
  /// "SKIN.BMP".
  std::string texture_name;
  /// Original runtime paletteName (+0x28): palette identifier, e.g.
  /// "SKIN.TGA".
  std::string palette_name;
  /// Payload size in the .3DT (+0x3C). The payload is stored raw exactly
  /// when this equals width * height.
  std::uint32_t data_size{0};
  std::uint16_t texture_page_index{0};  ///< Runtime texture page (+0x40); 0xFFFF in files.
  std::uint16_t texture_slot_index{0};  ///< Runtime texture slot (+0x42); 0xFFFF in files.
  std::uint16_t palette_page_index{0};  ///< Runtime palette page (+0x44); 0xFFFF in files.
  std::uint16_t palette_slot_index{0};  ///< Runtime palette slot (+0x46); 0xFFFF in files.
  std::uint16_t bits_per_pixel{0};      ///< Palette depth (+0x48): 2^bpp entries.
  std::uint8_t atlas_u_offset{0};       ///< Runtime atlas placement (+0x4A); unused.
  std::uint8_t atlas_v_offset{0};       ///< Runtime atlas placement (+0x4B); unused.
  std::uint16_t width{0};               ///< Texture width (+0x4C).
  std::uint16_t height{0};              ///< Texture height (+0x4E).
};

/// Mesh block descriptor (a bone or a visible part of a model).
struct MeshDescriptor {
  std::uint32_t flags{0};
  std::uint32_t mover_flags{0};
  std::uint32_t mesh_id{0};
  std::uint32_t script_id{0};
  std::string name;
  Vec3 position{};  ///< Serialized native Runtime XYZ position, in inches.
  std::int32_t parent_id{-1};
  std::int32_t first_child_id{-1};
  std::int32_t next_sibling_id{-1};
  std::uint32_t unknown07_count1{0};
  std::uint32_t vertex_count{0};
  std::uint32_t triangle_count{0};
  std::uint32_t rectangle_count{0};
  float unknown08{0.0F};
  float unknown09{0.0F};
  float unknown10{0.0F};
  float unknown11{0.0F};
  Vec3 box_extent_neg{};
  Vec3 box_extent_pos{};
  float unknown18{0.0F};
  float unknown19{0.0F};
  float unknown20{0.0F};
  Vec3 bone_position{};

  // Computed while parsing (element offsets, not raw file offsets):
  std::size_t vertex_base{0};            ///< First vertex in the global list.
  std::size_t triangle_byte_offset{0};   ///< Bytes from triangles_offset.
  std::size_t rectangle_byte_offset{0};  ///< Bytes from rectangles_offset.
};

/// Raw vertex exactly as stored in the file (native XYZ and BGRA byte order).
struct RawVertex {
  Vec3 position{};
  Vec3 normal{};
  std::uint32_t unknown_t1{0};
  std::array<std::uint8_t, 4> color_bgra{};  ///< File order: B, G, R, A.
};

/// Corner reference of a triangle. When parented is set, the index refers to
/// the skin parent's vertex block instead of the mesh's own block.
struct TriangleVertexRef {
  std::uint16_t index{0};
  bool parented{false};
};

/// Triangle face. UVs are integer pixel coordinates (0-255) resolved against
/// the material's texture size.
struct Triangle {
  std::array<TriangleVertexRef, 3> vertices{};
  std::array<std::uint8_t, 6> uv{};
  std::int32_t material_id{-1};
  std::array<std::int32_t, 3> unknown_ints{};  ///< s2, s3, s4.
};

/// Quad face; split into two triangles when rendering.
struct Rectangle {
  std::array<std::uint16_t, 4> vertices{};
  std::array<std::uint8_t, 8> uv{};
  std::int32_t material_id{-1};
  std::array<std::int32_t, 3> unknown_ints{};
};

/// All polygons of one mesh block.
struct MeshPolygons {
  std::vector<Triangle> triangles;
  std::vector<Rectangle> rectangles;
};

/// One 304-byte explicit light record from the .3DO light section.
///
/// Only the lights counted in the header's second light count field
/// (lights_unknown2) have records here; the first field (lights_unknown1,
/// "mesh lights") has no record section — that lighting is baked into the
/// vertex colours.
///
/// Semantics follow the reference importer: points[0] is the light position
/// and points[1] its target (the spot direction); the spot cone spans a 40
/// degree full hotspot and a 120 degree full falloff. attenuation_end and
/// attenuation_start bound the linear falloff. The two 16-bit flag words and
/// points 2-5 (cone/frustum shape data) are not interpreted.
struct Light {
  std::uint32_t flags{0};  ///< Two 16-bit flag words; meaning unresolved.
  std::string name;
  float attenuation_end{0.0F};               ///< Native far-attenuation end, inches.
  float attenuation_start{0.0F};             ///< Native far-attenuation start, inches.
  float intensity{0.0F};                     ///< Raw intensity multiplier.
  float unknown4{0.0F};                      ///< Unresolved secondary value.
  float unknown5{0.0F};                      ///< Unresolved secondary value.
  std::array<std::uint8_t, 4> color_bgra{};  ///< File order: B, G, R, A.
  std::array<Vec3, 6> points{};              ///< Slot 0 = position, 1 = target.

  /// Normalised spot direction from the position to the target; zero when
  /// the two coincide (the light degrades to a point light).
  [[nodiscard]] Vec3 direction() const;
  /// Linear RGBA colour, converted from the stored BGRA byte order.
  [[nodiscard]] std::array<float, 4> color_rgba() const;
};

/// Fully parsed .3DO file.
struct Model3DOData {
  Header header;
  std::vector<Material> materials;
  std::vector<MeshDescriptor> meshes;
  std::vector<MeshPolygons> polygons;  ///< Parallel to meshes.
  std::vector<RawVertex> vertices;     ///< Global vertex list.
  std::vector<Light> lights;           ///< Explicit light records.

  /// Descriptor selected by Serialized3DORootV4+0xB4, or -1 for a model
  /// without meshes. Runtime begins object traversal from this object.
  std::int32_t root_mesh_index{-1};

  /// Descriptor index of each mesh's hierarchy parent, or -1.
  std::vector<std::int32_t> hierarchy_parent_index;

  /// Descriptor index of each mesh's first child, or -1.
  std::vector<std::int32_t> hierarchy_first_child_index;
  /// Descriptor index of each mesh's next sibling, or -1.
  std::vector<std::int32_t> hierarchy_next_sibling_index;

  /// Whether the descriptor is reachable through the top-level sibling chain
  /// headed by root_mesh_index and its child/sibling subtrees. Parallel to meshes.
  std::vector<std::uint8_t> hierarchy_reachable;

  /// Mutable Runtime object state derived from immutable serialized mesh
  /// descriptors. Parallel to meshes and ready for later animation matrices.
  struct RuntimeObjectState {
    Vec3 local_offset{};
    Runtime::Matrix3 local_matrix{};
    std::optional<Runtime::Matrix3> animation_matrix;
    Vec3 scale{1.0F, 1.0F, 1.0F};
    Runtime::Matrix3 world_matrix{};
    Vec3 world_translation{};
  };
  std::vector<RuntimeObjectState> runtime_objects;

  /// Descriptor index of each mesh's nearest non-joint parent, or -1.
  std::vector<std::int32_t> skin_parent_index;
};

/// Render-ready geometry for a single material and mesh flag combination
/// (one draw call).
struct MaterialGroup {
  std::int32_t material_id{0};
  std::uint32_t flags{0};  ///< Flags of the source mesh (vertex-lit, ...).
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
};

/// Decoder for Omikron: The Nomad Soul .3DO model files.
///
/// Runtime.exe behavior is authoritative. The reference Blender importer is
/// used only as a format hint where Runtime behavior remains unresolved.
class Model3DO {
 public:
  /// Parses a complete .3DO file from memory.
  [[nodiscard]] static std::expected<Model3DOData, std::string> load(
      std::span<const std::byte> data);

  /// Builds render-ready static geometry in the bind pose. Skinned triangle
  /// corners that reference their parent mesh resolve against the nearest
  /// non-joint parent's vertex block.
  [[nodiscard]] static std::expected<std::vector<MaterialGroup>, std::string> build_static_geometry(
      const Model3DOData& model);

  /// Builds geometry from instance-local Runtime object transforms without
  /// mutating the shared parsed model resource.
  [[nodiscard]] static std::expected<std::vector<MaterialGroup>, std::string> build_posed_geometry(
      const Model3DOData& model,
      std::span<const Model3DOData::RuntimeObjectState> runtime_objects);

  /// Re-resolves Runtime object transforms from the current local/animation
  /// matrices while preserving serialized descriptor data.
  [[nodiscard]] static std::expected<void, std::string> resolve_runtime_transforms(
      Model3DOData& model);

  /// Resolves an instance-local transform array against an immutable model
  /// hierarchy.
  [[nodiscard]] static std::expected<void, std::string> resolve_runtime_transforms(
      const Model3DOData& model,
      std::span<Model3DOData::RuntimeObjectState> runtime_objects);

 private:
  static void read_header(BinaryReader& reader, Header& header);
  static Material read_material(BinaryReader& reader);
  static MeshDescriptor read_mesh_descriptor(BinaryReader& reader);
  static RawVertex read_raw_vertex(BinaryReader& reader);
  static Triangle read_triangle(BinaryReader& reader);
  static Rectangle read_rectangle(BinaryReader& reader);
  static Light read_light(BinaryReader& reader);
  static std::string read_fixed_string(BinaryReader& reader, std::size_t length);
  static Vec3 read_vec3(BinaryReader& reader);
};

}  // namespace App::Omikron
