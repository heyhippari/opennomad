#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace App {

/// Shared vertex stage for textured, vertex-coloured Runtime 3DO geometry.
/// `u_model_view` is the GL eye-space transform the fog stage measures in.
///
/// `u_lighting_mode` selects Runtime's vertex colour path:
///   0  pass the serialized colour through (non-world consumers);
///   1  baked colour clamped as one packed 0x00RRGGBB integer between the
///      context ambient floor and ceiling (0x004947F0);
///   2  lit actor: the ambient floor plus saturating Lambert terms of the
///      explicit decor lights (0x00494E80 + 0x00493E40). Light directions
///      are in the same space as `a_normal`. With `PER_PIXEL_LIGHTING` the
///      vertex keeps only the floor and forwards the normal; the fragment
///      stage adds the same terms per pixel.
///
/// With `CUBIC_VERTEX_COLOR` the outputs, renamed `vertex_*` and joined by the
/// VertexColorSlopes stream, feed K_WORLD_CUBIC_COLOR_GEOMETRY_SHADER, which
/// re-emits them under their usual names.
inline constexpr std::string_view K_WORLD_VERTEX_SHADER{R"glsl(
#version 410 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;

uniform mat4 u_mvp;
uniform mat4 u_model_view;
uniform vec2 u_uv_offset;

uniform int u_lighting_mode;
uniform uint u_ambient_floor;
uniform uint u_ambient_ceiling;
uniform int u_light_count;
uniform vec3 u_light_direction[16];
uniform float u_light_scale[16];
uniform vec3 u_light_color[16];

#ifdef CUBIC_VERTEX_COLOR
layout(location = 4) in vec3 a_color_slope_next;
layout(location = 5) in vec3 a_color_slope_previous;
out vec2 vertex_uv;
out vec4 vertex_color;
out vec3 vertex_view_position;
out vec3 vertex_color_slope_next;
out vec3 vertex_color_slope_previous;
#else
out vec2 v_uv;
out vec4 v_color;
out vec3 v_view_position;
#endif
#ifdef PER_PIXEL_LIGHTING
out vec3 v_normal;
out float v_normal_length;
#endif

uvec3 unpack_rgb(uint packed_color) {
    return uvec3((packed_color >> 16u) & 0xFFu, (packed_color >> 8u) & 0xFFu, packed_color & 0xFFu);
}

vec4 runtime_vertex_color() {
    if (u_lighting_mode == 1) {
        uvec3 bytes = uvec3(round(clamp(a_color.rgb, 0.0, 1.0) * 255.0));
        uint packed_color = (bytes.r << 16u) | (bytes.g << 8u) | bytes.b;
        if (packed_color < u_ambient_floor) {
            packed_color = u_ambient_floor;
        }
        if (packed_color > u_ambient_ceiling) {
            packed_color = u_ambient_ceiling;
        }
        return vec4(vec3(unpack_rgb(packed_color)) / 255.0, a_color.a);
    }
    if (u_lighting_mode == 2) {
        ivec3 color = ivec3(unpack_rgb(u_ambient_floor));
#ifndef PER_PIXEL_LIGHTING
        for (int index = 0; index < u_light_count; ++index) {
            float lambert = -dot(a_normal, u_light_direction[index]) * u_light_scale[index];
            if (lambert > 0.0) {
                int k = int(lambert);
                color = min(color + ((k * ivec3(u_light_color[index])) >> 8), ivec3(255));
            }
        }
#endif
        return vec4(vec3(color) / 255.0, a_color.a);
    }
    return a_color;
}

void main() {
#ifdef CUBIC_VERTEX_COLOR
    vertex_uv = a_uv + u_uv_offset;
    vertex_color = runtime_vertex_color();
    vertex_view_position = (u_model_view * vec4(a_position, 1.0)).xyz;
    vertex_color_slope_next = a_color_slope_next;
    vertex_color_slope_previous = a_color_slope_previous;
#else
    v_uv = a_uv + u_uv_offset;
    v_color = runtime_vertex_color();
    v_view_position = (u_model_view * vec4(a_position, 1.0)).xyz;
#endif
#ifdef PER_PIXEL_LIGHTING
    v_normal = a_normal;
    v_normal_length = length(a_normal);
#endif
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)glsl"};

/// Geometry stage of the cubic vertex colour enhancement (OpenNomad).
///
/// Builds a cubic Bezier triangle from the Runtime colours the vertex stage
/// produced (already clamped between the ambient floor and ceiling). The
/// control next to corner i on edge ij is `c_i + s_ij (c_j - c_i) / 3`, where
/// s_ij in [0, 3] is the corner's slope ratio (CubicVertexColor.hpp), so the
/// colour along an edge is a monotone cubic that depends only on that edge's
/// two corners: triangles sharing an edge agree on it. Slope 1 everywhere
/// reproduces Gouraud. The centre control is the PN-triangle choice
/// `E + (E - V) / 2`, limited to the range of the other nine controls so the
/// patch never leaves its corners' range. Control order: corners 0, 1, 2,
/// then edge controls 01, 10, 12, 21, 20, 02, then the centre.
inline constexpr std::string_view K_WORLD_CUBIC_COLOR_GEOMETRY_SHADER{R"glsl(
#version 410 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in vec2 vertex_uv[];
in vec4 vertex_color[];
in vec3 vertex_view_position[];
in vec3 vertex_color_slope_next[];
in vec3 vertex_color_slope_previous[];

out vec2 v_uv;
out vec4 v_color;
out vec3 v_view_position;
out vec3 v_barycentric;
flat out vec3 v_color_controls[10];

void main() {
    vec3 corner[3] = vec3[3](vertex_color[0].rgb, vertex_color[1].rgb, vertex_color[2].rgb);
    vec3 toward_next[3];
    vec3 toward_previous[3];
    for (int k = 0; k < 3; ++k) {
        toward_next[k] = corner[k] + vertex_color_slope_next[k] * (corner[(k + 1) % 3] - corner[k]) / 3.0;
        toward_previous[k] =
            corner[k] + vertex_color_slope_previous[k] * (corner[(k + 2) % 3] - corner[k]) / 3.0;
    }
    vec3 controls[10];
    controls[0] = corner[0];
    controls[1] = corner[1];
    controls[2] = corner[2];
    controls[3] = toward_next[0];
    controls[4] = toward_previous[1];
    controls[5] = toward_next[1];
    controls[6] = toward_previous[2];
    controls[7] = toward_next[2];
    controls[8] = toward_previous[0];
    vec3 edge_mean = vec3(0.0);
    vec3 lowest = controls[0];
    vec3 highest = controls[0];
    for (int index = 0; index < 9; ++index) {
        if (index >= 3) {
            edge_mean += controls[index];
        }
        lowest = min(lowest, controls[index]);
        highest = max(highest, controls[index]);
    }
    edge_mean /= 6.0;
    vec3 corner_mean = (corner[0] + corner[1] + corner[2]) / 3.0;
    controls[9] = clamp(edge_mean + (edge_mean - corner_mean) * 0.5, lowest, highest);

    for (int k = 0; k < 3; ++k) {
        gl_Position = gl_in[k].gl_Position;
        v_uv = vertex_uv[k];
        v_color = vertex_color[k];
        v_view_position = vertex_view_position[k];
        v_barycentric = vec3(k == 0 ? 1.0 : 0.0, k == 1 ? 1.0 : 0.0, k == 2 ? 1.0 : 0.0);
        for (int index = 0; index < 10; ++index) {
            v_color_controls[index] = controls[index];
        }
        EmitVertex();
    }
    EndPrimitive();
}
)glsl"};

/// Runtime linear table fog (D3D FOGTABLEMODE = LINEAR, start/end from the
/// render context), evaluated per pixel. `u_fog_radial` selects the Euclidean
/// eye distance, which stays stable while the camera turns; zero reproduces
/// Runtime's planar eye depth. The blend happens on encoded colour, as the
/// Direct3D fog unit did, and returns scene-linear colour.
inline constexpr std::string_view K_WORLD_FOG_GLSL{R"glsl(
in vec3 v_view_position;

uniform float u_fog_enabled;
uniform float u_fog_radial;
uniform float u_fog_start;
uniform float u_fog_end;
uniform float u_fog_depth_scale;
uniform vec3 u_fog_color;

float fog_srgb_to_linear(float encoded) {
    return encoded <= 0.04045 ? encoded / 12.92
                              : pow((encoded + 0.055) / 1.055, 2.4);
}

float fog_linear_to_srgb(float linear_value) {
    return linear_value <= 0.0031308 ? linear_value * 12.92
                                     : 1.055 * pow(linear_value, 1.0 / 2.4) - 0.055;
}

vec3 apply_world_fog(vec3 linear_color) {
    if (u_fog_enabled < 0.5) {
        return linear_color;
    }
    float fog_distance = u_fog_radial > 0.5 ? length(v_view_position) : -v_view_position.z;
    fog_distance *= u_fog_depth_scale;
    float visibility = clamp((u_fog_end - fog_distance) / (u_fog_end - u_fog_start), 0.0, 1.0);
    vec3 color = clamp(linear_color, 0.0, 1.0);
    vec3 encoded = vec3(fog_linear_to_srgb(color.r),
        fog_linear_to_srgb(color.g),
        fog_linear_to_srgb(color.b));
    encoded = mix(u_fog_color, encoded, visibility);
    return vec3(fog_srgb_to_linear(encoded.r),
        fog_srgb_to_linear(encoded.g),
        fog_srgb_to_linear(encoded.b));
}
)glsl"};

/// Runtime far alpha fade (context +0x14C = 0.95 * distance to +0x154): the
/// diffuse alpha is `clamp((end - depth) / (end - start), 0, 1)`. It only has a
/// visible effect on alpha-blended surfaces, which are never fogged.
inline constexpr std::string_view K_WORLD_FAR_FADE_GLSL{R"glsl(
uniform float u_far_fade_enabled;
uniform float u_far_fade_radial;
uniform float u_far_fade_start;
uniform float u_far_fade_end;
uniform float u_far_fade_depth_scale;

float runtime_far_fade() {
    if (u_far_fade_enabled < 0.5) {
        return 1.0;
    }
    float fade_distance = u_far_fade_radial > 0.5 ? length(v_view_position) : -v_view_position.z;
    fade_distance *= u_far_fade_depth_scale;
    return clamp((u_far_fade_end - fade_distance) / (u_far_fade_end - u_far_fade_start), 0.0, 1.0);
}
)glsl"};

/// Builds a fragment shader whose body may call `runtime_far_fade()`.
[[nodiscard]] inline std::string faded_fragment_shader(const std::string_view body) {
  std::string source{"#version 410 core\nin vec3 v_view_position;\n"};
  source += K_WORLD_FAR_FADE_GLSL;
  source += body;
  return source;
}

/// Builds a fragment shader whose body may call `apply_world_fog()`.
[[nodiscard]] inline std::string fogged_fragment_shader(const std::string_view body) {
  std::string source{"#version 410 core\n"};
  source += K_WORLD_FOG_GLSL;
  source += body;
  return source;
}

/// Opaque/cutout world fragment body writing scene-linear colour.
inline constexpr std::string_view K_MODERN_WORLD_FRAGMENT_BODY{R"glsl(
in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_texture0;
uniform float u_alpha_test;

out vec4 frag_colour;

float srgb_to_linear(float encoded) {
    return encoded <= 0.04045 ? encoded / 12.92
                              : pow((encoded + 0.055) / 1.055, 2.4);
}

#ifdef PER_PIXEL_LIGHTING
in vec3 v_normal;
in float v_normal_length;

uniform int u_lighting_mode;
uniform int u_light_count;
uniform vec3 u_light_direction[16];
uniform float u_light_scale[16];
uniform vec3 u_light_color[16];

// The vertex path's saturating terms (0x00493E40) evaluated with the
// interpolated normal, restored to its authored length so non-unit normals
// keep Runtime's intensity. Equal to the vertex path at every vertex.
vec3 per_pixel_actor_color(vec3 floor_color) {
    ivec3 color = ivec3(round(floor_color * 255.0));
    float normal_length_squared = dot(v_normal, v_normal);
    if (normal_length_squared > 0.0) {
        vec3 normal = v_normal * (v_normal_length * inversesqrt(normal_length_squared));
        for (int index = 0; index < u_light_count; ++index) {
            float lambert = -dot(normal, u_light_direction[index]) * u_light_scale[index];
            if (lambert > 0.0) {
                int k = int(lambert);
                color = min(color + ((k * ivec3(u_light_color[index])) >> 8), ivec3(255));
            }
        }
    }
    return vec3(color) / 255.0;
}
#endif

#ifdef CUBIC_VERTEX_COLOR
in vec3 v_barycentric;
flat in vec3 v_color_controls[10];

// The geometry stage's Bezier triangle; equal to the Runtime colour at every
// vertex and within the triangle's corner range everywhere.
vec3 cubic_vertex_color() {
    vec3 b = max(v_barycentric, vec3(0.0));
    b /= max(b.x + b.y + b.z, 1e-6);
    float u = b.x;
    float v = b.y;
    float w = b.z;
    return v_color_controls[0] * (u * u * u) + v_color_controls[1] * (v * v * v) +
           v_color_controls[2] * (w * w * w) +
           3.0 * (v_color_controls[3] * (u * u * v) + v_color_controls[4] * (u * v * v) +
                  v_color_controls[5] * (v * v * w) + v_color_controls[6] * (v * w * w) +
                  v_color_controls[7] * (w * w * u) + v_color_controls[8] * (w * u * u)) +
           6.0 * v_color_controls[9] * (u * v * w);
}
#endif

void main() {
    vec4 texel = texture(u_texture0, v_uv);
    if (u_alpha_test > 0.5 && texel.a < 0.5) {
        discard;
    }
    vec3 vertex_color = v_color.rgb;
#ifdef CUBIC_VERTEX_COLOR
    vertex_color = cubic_vertex_color();
#endif
#ifdef PER_PIXEL_LIGHTING
    if (u_lighting_mode == 2) {
        vertex_color = per_pixel_actor_color(vertex_color);
    }
#endif
    vec3 runtime_vertex = vec3(
        srgb_to_linear(vertex_color.r),
        srgb_to_linear(vertex_color.g),
        srgb_to_linear(vertex_color.b));
    frag_colour = vec4(apply_world_fog(texel.rgb * runtime_vertex), texel.a);
}
)glsl"};

/// Compile-time world shader variants. The default options build the
/// original Runtime sources; every enhancement is a preprocessor define, so the
/// original program never pays for or depends on enhanced code.
struct WorldShaderOptions {
  /// Lit actors evaluate their explicit lights per pixel instead of per vertex.
  bool per_pixel_lighting{false};
  /// Baked colours are interpolated by a cubic Bezier triangle; the program
  /// then needs K_WORLD_CUBIC_COLOR_GEOMETRY_SHADER and the
  /// VertexColorSlopes stream.
  bool cubic_vertex_color{false};

  [[nodiscard]] bool operator==(const WorldShaderOptions&) const = default;
};

/// Inserts the option defines directly after the leading `#version` line.
[[nodiscard]] inline std::string with_world_shader_defines(
    const std::string_view source, const WorldShaderOptions& options) {
  std::string defines;
  if (options.per_pixel_lighting) {
    defines += "#define PER_PIXEL_LIGHTING\n";
  }
  if (options.cubic_vertex_color) {
    defines += "#define CUBIC_VERTEX_COLOR\n";
  }
  std::string result{source};
  if (defines.empty()) {
    return result;
  }
  constexpr std::string_view k_version{"#version 410 core\n"};
  const std::size_t version{result.find(k_version)};
  result.insert(version == std::string::npos ? 0U : version + k_version.size(), defines);
  return result;
}

/// Vertex stage of the opaque/cutout and blended world programs.
[[nodiscard]] inline std::string world_vertex_shader(const WorldShaderOptions& options) {
  return with_world_shader_defines(K_WORLD_VERTEX_SHADER, options);
}

/// Fogged opaque/cutout world fragment stage.
[[nodiscard]] inline std::string modern_world_fragment_shader(const WorldShaderOptions& options) {
  return with_world_shader_defines(fogged_fragment_shader(K_MODERN_WORLD_FRAGMENT_BODY), options);
}

}  // namespace App
