#include "Core/TransitionScene.hpp"

#include <glad/glad.h>

#include <memory>

#include "Core/Debug/Instrumentor.hpp"
#include "Core/Input/InputManager.hpp"
#include "Core/Log.hpp"
#include "Core/LogCategory.hpp"

namespace App {

std::unique_ptr<TransitionScene> TransitionScene::create() {
  // The constructor is private; only the factory may build a scene.
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  return std::unique_ptr<TransitionScene>{new TransitionScene()};
}

void TransitionScene::update(const float /*delta_time*/, const Input::InputManager& /*input*/) {
  if (!m_logged) {
    m_logged = true;
    App::Log::debug(LogCategory::Scenario,
        "Transition: interface 29 completed; Kay'l introduction not yet "
        "implemented (GRID world context 0 remains resident)");
  }
}

void TransitionScene::render() {
  APP_PROFILE_FUNCTION();

  glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

}  // namespace App
