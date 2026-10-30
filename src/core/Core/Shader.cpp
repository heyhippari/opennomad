#include "Shader.hpp"

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"

namespace App {

Shader::Shader(const GLuint program_id) : m_program_id(program_id) {}

Shader::Shader(Shader&& other) noexcept
    : m_program_id(std::exchange(other.m_program_id, 0)),
      m_uniform_cache(std::move(other.m_uniform_cache)),
      m_block_cache(std::move(other.m_block_cache)) {}

Shader& Shader::operator=(Shader&& other) noexcept {
  if (this != &other) {
    if (m_program_id != 0) {
      glDeleteProgram(m_program_id);
    }
    m_program_id = std::exchange(other.m_program_id, 0);
    m_uniform_cache = std::move(other.m_uniform_cache);
    m_block_cache = std::move(other.m_block_cache);
  }
  return *this;
}

std::expected<Shader, std::string> Shader::create(
    const std::string_view vertex_source, const std::string_view fragment_source) {
  APP_PROFILE_FUNCTION();

  const GLuint vs{compile_shader(GL_VERTEX_SHADER, vertex_source)};
  if (vs == 0) {
    return std::expected<Shader, std::string>{std::unexpect, "Vertex shader failed to compile"};
  }

  const GLuint fs{compile_shader(GL_FRAGMENT_SHADER, fragment_source)};
  if (fs == 0) {
    glDeleteShader(vs);
    return std::expected<Shader, std::string>{std::unexpect, "Fragment shader failed to compile"};
  }

  // glCreateProgram cannot run before the shaders are compiled, so it cannot
  // live in a member-initializer list.
  const GLuint program{glCreateProgram()};
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);

  GLint link_status{0};
  glGetProgramiv(program, GL_LINK_STATUS, &link_status);

  glDetachShader(program, vs);
  glDetachShader(program, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);

  if (link_status == 0) {
    GLint info_length{0};
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_length);
    std::string info_log(static_cast<std::size_t>(info_length), '\0');
    glGetProgramInfoLog(program, info_length, nullptr, info_log.data());
    App::Log::error(LogCategory::Renderer, "Shader program link error: {}", info_log);
    glDeleteProgram(program);
    return std::expected<Shader, std::string>{std::unexpect, "Shader program failed to link"};
  }

  return Shader{program};
}

Shader::~Shader() {
  APP_PROFILE_FUNCTION();

  if (m_program_id != 0) {
    glDeleteProgram(m_program_id);
  }
}

GLuint Shader::compile_shader(const GLenum type, const std::string_view source) {
  const GLuint id{glCreateShader(type)};
  const char* src{source.data()};
  const GLint len{static_cast<GLint>(source.size())};
  glShaderSource(id, 1, &src, &len);
  glCompileShader(id);

  GLint compile_status{0};
  glGetShaderiv(id, GL_COMPILE_STATUS, &compile_status);
  if (compile_status == 0) {
    GLint info_length{0};
    glGetShaderiv(id, GL_INFO_LOG_LENGTH, &info_length);
    std::string info_log(static_cast<std::size_t>(info_length), '\0');
    glGetShaderInfoLog(id, info_length, nullptr, info_log.data());
    App::Log::error(LogCategory::Renderer,
        "Shader compile error ({}): {}",
        (type == GL_VERTEX_SHADER ? "vertex" : "fragment"),
        info_log);
    glDeleteShader(id);
    return 0;
  }

  return id;
}

void Shader::bind() const {
  glUseProgram(m_program_id);
}

void Shader::unbind() {
  glUseProgram(0);
}

GLuint Shader::program_id() const {
  return m_program_id;
}

Shader::UniformSlot* Shader::uniform_slot(const std::string_view name) const {
  auto found{m_uniform_cache.find(name)};
  if (found == m_uniform_cache.end()) {
    const std::string key{name};
    const GLint location{glGetUniformLocation(m_program_id, key.c_str())};
    found = m_uniform_cache.emplace(key, UniformSlot{.location = location}).first;
  }
  return found->second.location == -1 ? nullptr : &found->second;
}

std::optional<GLint> Shader::changed_location(
    const std::string_view name, const std::span<const std::uint32_t> bits) const {
  return changed_location(uniform_slot(name), bits);
}

std::optional<GLint> Shader::changed_location(
    UniformSlot* const slot, const std::span<const std::uint32_t> bits) {
  if (slot == nullptr) {
    return std::nullopt;
  }
  const auto cached{std::span{slot->value}.first(bits.size())};
  if (slot->has_value && std::ranges::equal(cached, bits)) {
    return std::nullopt;
  }
  std::ranges::copy(bits, cached.begin());
  slot->has_value = true;
  return slot->location;
}

namespace {

template <std::size_t N>
[[nodiscard]] std::array<std::uint32_t, N> float_bits(const std::span<const GLfloat, N> value) {
  std::array<std::uint32_t, N> bits{};
  std::ranges::transform(value, bits.begin(), [](const GLfloat component) {
    return std::bit_cast<std::uint32_t>(component);
  });
  return bits;
}

}  // namespace

void Shader::set_uniform_mat4(
    const std::string_view name, const std::span<const GLfloat, 16> value) const {
  if (const UniformSlot* const slot{uniform_slot(name)}) {
    glUniformMatrix4fv(slot->location, 1, GL_FALSE, value.data());
  }
}

void Shader::set_uniform_mat3(
    const std::string_view name, const std::span<const GLfloat, 9> value) const {
  if (const UniformSlot* const slot{uniform_slot(name)}) {
    glUniformMatrix3fv(slot->location, 1, GL_FALSE, value.data());
  }
}

void Shader::set_uniform_vec4(
    const std::string_view name, const std::span<const GLfloat, 4> value) const {
  if (const auto loc{changed_location(name, float_bits(value))}) {
    glUniform4fv(*loc, 1, value.data());
  }
}

void Shader::set_uniform_vec3(
    const std::string_view name, const std::span<const GLfloat, 3> value) const {
  if (const auto loc{changed_location(name, float_bits(value))}) {
    glUniform3fv(*loc, 1, value.data());
  }
}

void Shader::set_uniform_vec2(
    const std::string_view name, const std::span<const GLfloat, 2> value) const {
  if (const auto loc{changed_location(name, float_bits(value))}) {
    glUniform2fv(*loc, 1, value.data());
  }
}

void Shader::set_uniform_float(const std::string_view name, const float value) const {
  const std::array<std::uint32_t, 1> bits{std::bit_cast<std::uint32_t>(value)};
  if (const auto loc{changed_location(name, bits)}) {
    glUniform1f(*loc, value);
  }
}

void Shader::set_uniform_int(const std::string_view name, const int value) const {
  const std::array<std::uint32_t, 1> bits{std::bit_cast<std::uint32_t>(value)};
  if (const auto loc{changed_location(name, bits)}) {
    glUniform1i(*loc, value);
  }
}

void Shader::set_uniform_uint(const std::string_view name, const GLuint value) const {
  const std::array<std::uint32_t, 1> bits{value};
  if (const auto loc{changed_location(name, bits)}) {
    glUniform1ui(*loc, value);
  }
}

Shader::UniformRef Shader::uniform(const std::string_view name) const {
  return UniformRef{uniform_slot(name), m_program_id};
}

Shader::UniformSlot* Shader::owned_slot(const UniformRef uniform) const {
  return uniform.m_program == m_program_id ? uniform.m_slot : nullptr;
}

void Shader::set_uniform_mat4(
    const UniformRef uniform, const std::span<const GLfloat, 16> value) const {
  if (const UniformSlot* const slot{owned_slot(uniform)}) {
    glUniformMatrix4fv(slot->location, 1, GL_FALSE, value.data());
  }
}

void Shader::set_uniform_vec3(
    const UniformRef uniform, const std::span<const GLfloat, 3> value) const {
  if (const auto loc{changed_location(owned_slot(uniform), float_bits(value))}) {
    glUniform3fv(*loc, 1, value.data());
  }
}

void Shader::set_uniform_vec2(
    const UniformRef uniform, const std::span<const GLfloat, 2> value) const {
  if (const auto loc{changed_location(owned_slot(uniform), float_bits(value))}) {
    glUniform2fv(*loc, 1, value.data());
  }
}

void Shader::set_uniform_float(const UniformRef uniform, const float value) const {
  const std::array<std::uint32_t, 1> bits{std::bit_cast<std::uint32_t>(value)};
  if (const auto loc{changed_location(owned_slot(uniform), bits)}) {
    glUniform1f(*loc, value);
  }
}

void Shader::set_uniform_int(const UniformRef uniform, const int value) const {
  const std::array<std::uint32_t, 1> bits{std::bit_cast<std::uint32_t>(value)};
  if (const auto loc{changed_location(owned_slot(uniform), bits)}) {
    glUniform1i(*loc, value);
  }
}

void Shader::set_uniform_uint(const UniformRef uniform, const GLuint value) const {
  const std::array<std::uint32_t, 1> bits{value};
  if (const auto loc{changed_location(owned_slot(uniform), bits)}) {
    glUniform1ui(*loc, value);
  }
}

void Shader::set_uniform_block_binding(
    const std::string_view name, const GLuint binding_point) const {
  GLuint block_index{GL_INVALID_INDEX};
  if (const auto found{m_block_cache.find(name)}; found != m_block_cache.end()) {
    block_index = found->second;
  } else {
    const std::string key{name};
    block_index = glGetUniformBlockIndex(m_program_id, key.c_str());
    m_block_cache.emplace(key, block_index);
  }
  if (block_index == GL_INVALID_INDEX) {
    App::Log::warn(LogCategory::Renderer, "Shader has no uniform block '{}'", name);
    return;
  }
  glUniformBlockBinding(m_program_id, block_index, binding_point);
}

Shader Shader::create_default() {
  // clang-format off
  static constexpr std::string_view k_vertex_source = R"glsl(
#version 410 core

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv;

uniform mat4 u_mvp;

out vec2 v_uv;

void main() {
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)glsl";

  static constexpr std::string_view k_fragment_source = R"glsl(
#version 410 core

in vec2 v_uv;

uniform sampler2D u_texture0;
uniform vec4 u_tint;

out vec4 frag_colour;

float linear_to_srgb(float linear) {
    if (linear <= 0.0031308) {
        return 12.92 * linear;
    }
    return (1.055 * pow(linear, 1.0 / 2.4)) - 0.055;
}

void main() {
    vec4 linear = texture(u_texture0, v_uv) * u_tint;
    frag_colour = vec4(linear_to_srgb(linear.r),
                       linear_to_srgb(linear.g),
                       linear_to_srgb(linear.b),
                       linear.a);
}
)glsl";
  // clang-format on

  auto shader{create(k_vertex_source, k_fragment_source)};
  if (!shader) {
    throw std::runtime_error{"Failed to build default shader: " + shader.error()};
  }
  return std::move(shader).value();
}

}  // namespace App
