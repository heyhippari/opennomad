set(NAME "Core")

include(${PROJECT_SOURCE_DIR}/cmake/StaticAnalyzers.cmake)

add_library(${NAME} STATIC
  Core/Log.cpp Core/Log.hpp Core/Debug/Instrumentor.hpp
  Core/Debug/Metrics.hpp Core/Debug/Metrics.cpp
  Core/Debug/LogSink.hpp
  Core/Debug/DebugUI.hpp Core/Debug/DebugUI.cpp
  Core/Application.cpp Core/Application.hpp
  Core/Window.cpp Core/Window.hpp
  Core/Audio/AudioTypes.hpp
  Core/Audio/LegacySpatializer.hpp Core/Audio/LegacySpatializer.cpp
  Core/Audio/VoicePool.hpp Core/Audio/VoicePool.cpp
  Core/Audio/SoundResourceCache.hpp Core/Audio/SoundResourceCache.cpp
  Core/Audio/MusicPlayer.hpp Core/Audio/MusicPlayer.cpp
  Core/Audio/AudioSystem.hpp Core/Audio/AudioSystem.cpp
  Core/Renderer.cpp Core/Renderer.hpp
  Core/Shader.cpp Core/Shader.hpp
  Core/Camera.cpp Core/Camera.hpp
  Core/CameraController.cpp Core/CameraController.hpp
  Core/Input/InputAction.hpp
  Core/Input/InputSource.hpp
  Core/Input/Binding.hpp
  Core/Input/RawInputState.hpp
  Core/Input/ControlScheme.hpp Core/Input/ControlScheme.cpp
  Core/Input/InputManager.hpp Core/Input/InputManager.cpp
  Core/Vertex.hpp
  Core/Buffers.hpp Core/Buffers.cpp
  Core/UniformBuffer.hpp Core/UniformBuffer.cpp
  Core/VertexArray.hpp Core/VertexArray.cpp
  Core/Mesh.hpp Core/Mesh.cpp
  Core/Texture.hpp Core/Texture.cpp
  Core/Scene.hpp
  Core/ModelScene.hpp Core/ModelScene.cpp
  Core/SplashScene.hpp Core/SplashScene.cpp
  Core/Framebuffer.hpp Core/Framebuffer.cpp
  Core/TextureCube.hpp Core/TextureCube.cpp
  Core/Reflection.hpp
  Core/Omikron/BinaryReader.hpp
  Core/Omikron/Model3DO.hpp Core/Omikron/Model3DO.cpp
  Core/Omikron/Texture3DT.hpp Core/Omikron/Texture3DT.cpp
  Core/Omikron/QdAdp.hpp Core/Omikron/QdAdp.cpp
  Core/Omikron/BmpImage.hpp Core/Omikron/BmpImage.cpp
  Core/Omikron/IndexedBmp8.hpp Core/Omikron/IndexedBmp8.cpp
  Core/Omikron/SCX.hpp Core/Omikron/SCX.cpp
  Core/Omikron/IamArchive.hpp Core/Omikron/IamArchive.cpp
  Core/Omikron/IamStart.hpp Core/Omikron/IamStart.cpp
  Core/Omikron/IamArea.hpp Core/Omikron/IamArea.cpp
  Core/Sprite/SpriteInstance.hpp
  Core/Sprite/SpriteRenderMode.hpp
  Core/Sprite/SpriteFrame.hpp Core/Sprite/SpriteFrame.cpp
  Core/Sprite/SpritePool.hpp Core/Sprite/SpritePool.cpp
  Core/Sprite/SpriteResource.hpp Core/Sprite/SpriteResource.cpp
  Core/Sprite/SpriteRenderer.hpp Core/Sprite/SpriteRenderer.cpp
  Core/Scenario/ScenarioManager.hpp Core/Scenario/ScenarioManager.cpp
  Core/Scenario/ScenarioRuntime.hpp Core/Scenario/ScenarioRuntime.cpp
  Core/Scenario/ScenarioStartupController.hpp Core/Scenario/ScenarioStartupController.cpp
  Core/Scenario/ScenarioEngine.hpp Core/Scenario/ScenarioEngine.cpp
  Core/Interface/InterfaceDispatcher.hpp Core/Interface/InterfaceDispatcher.cpp
  Core/Interface/I2DModel.hpp
  Core/Interface/InterfaceDescriptor.hpp
  Core/Interface/StartMenuLayout.hpp
  Core/Interface/FontManager.hpp Core/Interface/FontManager.cpp
  Core/Interface/I2DBumpBackground.hpp Core/Interface/I2DBumpBackground.cpp
  Core/Interface/I2DBumpEffect.hpp Core/Interface/I2DBumpEffect.cpp
  Core/Interface/I2DRenderer.hpp Core/Interface/I2DRenderer.cpp
  Core/Interface/InterfaceManager.hpp Core/Interface/InterfaceManager.cpp
  Core/Omikron/IamStringTable.hpp Core/Omikron/IamStringTable.cpp
  Core/Script/ScriptOpcode.hpp
  Core/Script/ScriptRuntime.hpp Core/Script/ScriptRuntime.cpp
  Core/Script/AreaScriptOpcode.hpp
  Core/Script/AreaScriptRuntime.hpp Core/Script/AreaScriptRuntime.cpp
  Core/MainMenuScene.hpp Core/MainMenuScene.cpp
  Core/TransitionScene.hpp Core/TransitionScene.cpp
  Core/DPIHandler.cpp Core/DPIHandler.hpp
  Core/FrameTiming.hpp
  Core/Resources.hpp
  Core/Startup/StartupPhase.hpp
  Core/Startup/StartupTraceRecorder.hpp Core/Startup/StartupTraceRecorder.cpp
  Core/Startup/StartupCoordinator.hpp Core/Startup/StartupCoordinator.cpp
  Core/Startup/StartupMediaPolicy.hpp
  Core/Video/VideoPlayer.hpp Core/Video/VideoPlayer.cpp
  Core/Video/VideoScene.hpp Core/Video/VideoScene.cpp
  Core/Video/StartupVideoSequence.hpp Core/Video/StartupVideoSequence.cpp)

# Define set of OS specific files to include
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_sources(${NAME} PRIVATE
    Platform/Windows/Resources.cpp)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  target_sources(${NAME} PRIVATE
    Platform/Mac/Resources.cpp)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_sources(${NAME} PRIVATE
    Platform/Linux/Resources.cpp)
endif ()

target_include_directories(${NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(glm CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)

target_link_libraries(${NAME}
  PRIVATE project_warnings
  PUBLIC fmt::fmt spdlog::spdlog SDL3::SDL3 glad::glad imgui::imgui glm::glm
    $<IF:$<TARGET_EXISTS:SDL3_image::SDL3_image-shared>,SDL3_image::SDL3_image-shared,SDL3_image::SDL3_image-static>
    $<IF:$<TARGET_EXISTS:SDL3_mixer::SDL3_mixer-shared>,SDL3_mixer::SDL3_mixer-shared,SDL3_mixer::SDL3_mixer-static>
    Settings)

target_include_directories(${NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(${NAME} PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(${NAME} PRIVATE ${FFMPEG_LIBRARIES})

add_subdirectory(Tests)
