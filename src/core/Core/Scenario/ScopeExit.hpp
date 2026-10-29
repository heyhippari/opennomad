#pragma once

#include <utility>

namespace App {

/// Runs a callable when the scope ends unless release() was called; used for transactional
/// rollbacks that must be disarmed on the success path.
template <typename Function>
class ScopeExit {
 public:
  explicit ScopeExit(Function function) : m_function{std::move(function)} {}
  ~ScopeExit() {
    if (m_active) {
      m_function();
    }
  }

  ScopeExit(const ScopeExit&) = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;
  ScopeExit(ScopeExit&&) = delete;
  ScopeExit& operator=(ScopeExit&&) = delete;

  void release() {
    m_active = false;
  }

 private:
  Function m_function;
  bool m_active{true};
};

}  // namespace App
