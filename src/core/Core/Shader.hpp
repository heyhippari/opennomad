#pragma once

#include <glad/glad.h>

#include <expected>
#include <flat_map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace App {

/// Compiles and links a GLSL vertex + fragment shader pair (OpenGL 4.1 core).
///
/// Construction can fail, so use Shader::create(), which reports compilation
/// and link errors through std::expected.
class Shader {
 public:
  /// Compiles and links the given sources. On failure the error describes
  /// which stage failed (the compiler/linker info log is logged separately).
  static std::expected<Shader, std::string> create(
      std::string_view vertex_source, std::string_view fragment_source);

  Shader(Shader&& other) noexcept;
  Shader& operator=(Shader&& other) noexcept;
  ~Shader();

  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;

  void bind() const;
  static void unbind();

  [[nodiscard]] GLuint program_id() const;

  // --- Uniform setters ---
  void set_uniform_mat4(std::string_view name, std::span<const GLfloat, 16> value) const;
  void set_uniform_mat3(std::string_view name, std::span<const GLfloat, 9> value) const;
  void set_uniform_vec4(std::string_view name, std::span<const GLfloat, 4> value) const;
  void set_uniform_vec3(std::string_view name, std::span<const GLfloat, 3> value) const;
  void set_uniform_vec2(std::string_view name, std::span<const GLfloat, 2> value) const;
  void set_uniform_float(std::string_view name, float value) const;
  void set_uniform_int(std::string_view name, int value) const;

  /// Binds a named uniform block to a binding point (glUniformBlockBinding).
  /// No-op when the program has no such block (e.g. it was optimised out).
  void set_uniform_block_binding(std::string_view name, GLuint binding_point) const;

  /// Returns a simple default shader (MVP transform + textured + linear tint
  /// + explicit sRGB display encoding). Used by the startup splash.
  ///
  /// Throws std::runtime_error if the hardcoded sources fail to build — that
  /// is a programmer error, not a recoverable runtime condition.
  static Shader create_default();

 private:
  /// Assumes ownership of an already linked program.
  explicit Shader(GLuint program_id);

  static GLuint compile_shader(GLenum type, std::string_view source);

  /// Returns the uniform location, querying GL on first use. std::nullopt if
  /// the program has no uniform with that name.
  [[nodiscard]] std::optional<GLint> uniform_location(std::string_view name) const;

  GLuint m_program_id{0};
  mutable std::flat_map<std::string, GLint> m_uniform_cache;
  mutable std::flat_map<std::string, GLuint> m_block_cache;
};

}  // namespace App
