#pragma once

#include <string>
#include <string_view>

namespace App::Input {

/// Minimal UTF-8 text-input capture, the modern equivalent of the original
/// engine's WM_CHAR capture into a single character variable (see
/// docs/ReverseEngineering.md).
///
/// The original truncates characters to a signed byte; OpenNomad keeps
/// SDL's UTF-8 representation intact — truncation would only belong in a
/// format-facing subsystem. No text-entry consumer exists yet: this is the
/// hook a future save menu or text prompt can use.
///
/// Pure logic: fed by the application from SDL text events, no SDL calls.
class TextInputState {
 public:
  /// Enables or disables capture; input is only accepted while enabled.
  void set_enabled(const bool enabled) {
    m_enabled = enabled;
  }

  [[nodiscard]] bool is_enabled() const {
    return m_enabled;
  }

  /// Appends a UTF-8 text chunk. Ignored while capture is disabled.
  void on_text_input(const std::string_view utf8) {
    if (m_enabled) {
      m_text.append(utf8);
    }
  }

  /// Text captured since the last clear.
  [[nodiscard]] const std::string& text() const {
    return m_text;
  }

  /// Clears the captured text (keeps the enabled flag).
  void clear() {
    m_text.clear();
  }

 private:
  bool m_enabled{false};
  std::string m_text{};
};

}  // namespace App::Input
