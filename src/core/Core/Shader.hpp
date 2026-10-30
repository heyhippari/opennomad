#pragma once

#include <glad/glad.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <flat_map>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace App {

/// Compiles and links a GLSL vertex + fragment shader pair (OpenGL 4.1 core).
///
/// Construction can fail, so use Shader::create(), which reports compilation
/// and link errors through std::expected.
class Shader {
 private:
  struct UniformSlot;

 public:
  /// Pre-resolved uniform of one program for per-draw hot paths, skipping the
  /// name lookup. Valid for the lifetime of the Shader that produced it; a
  /// uniform the program does not have, or a handle passed to another Shader,
  /// makes writes no-ops.
  class UniformRef {
   public:
    UniformRef() = default;

   private:
    friend class Shader;
    UniformRef(UniformSlot* const slot, const GLuint program) : m_slot(slot), m_program(program) {}
    UniformSlot* m_slot{nullptr};
    GLuint m_program{0};
  };

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
  void set_uniform_uint(std::string_view name, GLuint value) const;

  /// Resolves `name` once for use with the UniformRef setters.
  [[nodiscard]] UniformRef uniform(std::string_view name) const;
  void set_uniform_mat4(UniformRef uniform, std::span<const GLfloat, 16> value) const;
  void set_uniform_vec3(UniformRef uniform, std::span<const GLfloat, 3> value) const;
  void set_uniform_vec2(UniformRef uniform, std::span<const GLfloat, 2> value) const;
  void set_uniform_float(UniformRef uniform, float value) const;
  void set_uniform_int(UniformRef uniform, int value) const;
  void set_uniform_uint(UniformRef uniform, GLuint value) const;

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

  /// Cached location plus the last scalar/vector value written through this
  /// object. Uniform values are program state, so a repeated write of the same
  /// value is skipped without a GL call.
  struct UniformSlot {
    GLint location{-1};
    std::array<std::uint32_t, 4> value{};
    bool has_value{false};
  };

  /// Returns the uniform slot, querying GL on first use. nullptr if the program
  /// has no uniform with that name. The lookup never allocates once cached.
  [[nodiscard]] UniformSlot* uniform_slot(std::string_view name) const;

  /// Returns the location to write when `bits` differs from the cached value,
  /// recording it; std::nullopt when the uniform is absent or unchanged.
  [[nodiscard]] std::optional<GLint> changed_location(
      std::string_view name, std::span<const std::uint32_t> bits) const;
  [[nodiscard]] static std::optional<GLint> changed_location(
      UniformSlot* slot, std::span<const std::uint32_t> bits);
  /// The slot of `uniform` when it was resolved by this Shader, else nullptr.
  [[nodiscard]] UniformSlot* owned_slot(UniformRef uniform) const;

  /// Transparent hash so string_view lookups never build a std::string.
  struct UniformNameHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(const std::string_view name) const noexcept {
      return std::hash<std::string_view>{}(name);
    }
  };

  GLuint m_program_id{0};
  /// Hashed rather than ordered: every per-draw uniform write goes through this
  /// lookup, and character lighting writes dozens per material group.
  mutable std::unordered_map<std::string, UniformSlot, UniformNameHash, std::equal_to<>>
      m_uniform_cache;
  mutable std::flat_map<std::string, GLuint, std::less<>> m_block_cache;
};

}  // namespace App
