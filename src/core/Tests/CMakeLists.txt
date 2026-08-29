# Test cases

add_executable(ResourcesTest Resources.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME ResourcesTest COMMAND ResourcesTest)
target_link_libraries(ResourcesTest PRIVATE doctest::doctest Core)

add_executable(GameSettingsTest GameSettings.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME GameSettingsTest COMMAND GameSettingsTest)
target_link_libraries(GameSettingsTest PRIVATE doctest::doctest Core)

add_executable(DisplayConfigurationTest DisplayConfiguration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME DisplayConfigurationTest COMMAND DisplayConfigurationTest)
target_link_libraries(DisplayConfigurationTest PRIVATE doctest::doctest Core)

add_executable(MetricsTest Metrics.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME MetricsTest COMMAND MetricsTest)
target_link_libraries(MetricsTest PRIVATE doctest::doctest Core)

add_executable(RenderingTest Rendering.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME RenderingTest COMMAND RenderingTest)
target_link_libraries(RenderingTest PRIVATE doctest::doctest Core)

add_executable(InputTest Input.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME InputTest COMMAND InputTest)
target_link_libraries(InputTest PRIVATE doctest::doctest Core)

add_executable(MainLoopTest MainLoop.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME MainLoopTest COMMAND MainLoopTest)
target_link_libraries(MainLoopTest PRIVATE doctest::doctest Core)

add_executable(FrameTimingTest FrameTiming.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME FrameTimingTest COMMAND FrameTimingTest)
target_link_libraries(FrameTimingTest PRIVATE doctest::doctest Core)

add_executable(WorldCameraTest WorldCamera.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME WorldCameraTest COMMAND WorldCameraTest)
target_link_libraries(WorldCameraTest PRIVATE doctest::doctest Core)

add_executable(WorldLetterboxTest WorldLetterbox.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME WorldLetterboxTest COMMAND WorldLetterboxTest)
target_link_libraries(WorldLetterboxTest PRIVATE doctest::doctest Core)

add_executable(WorldFadeTest WorldFade.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME WorldFadeTest COMMAND WorldFadeTest)
target_link_libraries(WorldFadeTest PRIVATE doctest::doctest Core)

add_executable(WorldUvPhaseTest WorldUvPhase.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME WorldUvPhaseTest COMMAND WorldUvPhaseTest)
target_link_libraries(WorldUvPhaseTest PRIVATE doctest::doctest Core)

add_executable(RuntimeMathTest RuntimeMath.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME RuntimeMathTest COMMAND RuntimeMathTest)
target_link_libraries(RuntimeMathTest PRIVATE doctest::doctest Core)

add_executable(CharacterRuntimeTest CharacterRuntime.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME CharacterRuntimeTest COMMAND CharacterRuntimeTest)
target_link_libraries(CharacterRuntimeTest PRIVATE doctest::doctest Core)

add_executable(CtlControlSetTest CtlControlSet.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME CtlControlSetTest COMMAND CtlControlSetTest)
target_link_libraries(CtlControlSetTest PRIVATE doctest::doctest Core)

add_executable(CtlControllerTest CtlController.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME CtlControllerTest COMMAND CtlControllerTest)
target_link_libraries(CtlControllerTest PRIVATE doctest::doctest Core)

add_executable(ObjectPlacementRuntimeTest ObjectPlacementRuntime.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME ObjectPlacementRuntimeTest COMMAND ObjectPlacementRuntimeTest)
target_link_libraries(ObjectPlacementRuntimeTest PRIVATE doctest::doctest Core)

add_executable(Model3DOTest Model3DO.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME Model3DOTest COMMAND Model3DOTest)
target_link_libraries(Model3DOTest PRIVATE doctest::doctest Core)

add_executable(ThreeDMTest ThreeDM.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME ThreeDMTest COMMAND ThreeDMTest)
target_link_libraries(ThreeDMTest PRIVATE doctest::doctest Core)

add_executable(DialogVoiceCodecTest DialogVoiceCodec.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME DialogVoiceCodecTest COMMAND DialogVoiceCodecTest)
target_link_libraries(DialogVoiceCodecTest PRIVATE doctest::doctest Core)

add_executable(RuntimeTextTest RuntimeText.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME RuntimeTextTest COMMAND RuntimeTextTest)
target_link_libraries(RuntimeTextTest PRIVATE doctest::doctest Core)

add_executable(RuntimeTextLayoutTest RuntimeTextLayout.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME RuntimeTextLayoutTest COMMAND RuntimeTextLayoutTest)
target_link_libraries(RuntimeTextLayoutTest PRIVATE doctest::doctest Core)

add_executable(WorldTextTest WorldText.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME WorldTextTest COMMAND WorldTextTest)
target_link_libraries(WorldTextTest PRIVATE doctest::doctest Core)

add_executable(DialogPerformanceRuntimeTest
  DialogPerformanceRuntime.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME DialogPerformanceRuntimeTest COMMAND DialogPerformanceRuntimeTest)
target_link_libraries(DialogPerformanceRuntimeTest PRIVATE doctest::doctest Core)

add_executable(BodyAnimationResourcesTest
  BodyAnimationResources.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME BodyAnimationResourcesTest COMMAND BodyAnimationResourcesTest)
target_link_libraries(BodyAnimationResourcesTest PRIVATE doctest::doctest Core)

add_executable(Texture3DTTest Texture3DT.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME Texture3DTTest COMMAND Texture3DTTest)
target_link_libraries(Texture3DTTest PRIVATE doctest::doctest Core)

add_executable(FontFNTTest FontFNT.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME FontFNTTest COMMAND FontFNTTest)
target_link_libraries(FontFNTTest PRIVATE doctest::doctest Core)

add_executable(QdAdpTest QdAdp.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME QdAdpTest COMMAND QdAdpTest)
target_link_libraries(QdAdpTest PRIVATE doctest::doctest Core)

add_executable(SCXTest SCX.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME SCXTest COMMAND SCXTest)
target_link_libraries(SCXTest PRIVATE doctest::doctest Core)

add_executable(SCXScriptTest SCXScript.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME SCXScriptTest COMMAND SCXScriptTest)
target_link_libraries(SCXScriptTest PRIVATE doctest::doctest Core)

add_executable(SFXTest SFX.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME SFXTest COMMAND SFXTest)
target_link_libraries(SFXTest PRIVATE doctest::doctest Core)

add_executable(SfxRuntimeTest SfxRuntime.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME SfxRuntimeTest COMMAND SfxRuntimeTest)
target_link_libraries(SfxRuntimeTest PRIVATE doctest::doctest Core)

add_executable(ScenarioManagerTest ScenarioManager.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME ScenarioManagerTest COMMAND ScenarioManagerTest)
target_link_libraries(ScenarioManagerTest PRIVATE doctest::doctest Core)

add_executable(ScenarioRuntimeTest ScenarioRuntime.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME ScenarioRuntimeTest COMMAND ScenarioRuntimeTest)
target_link_libraries(ScenarioRuntimeTest PRIVATE doctest::doctest Core)

add_executable(IamArchiveTest IamArchive.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME IamArchiveTest COMMAND IamArchiveTest)
target_link_libraries(IamArchiveTest PRIVATE doctest::doctest Core)

add_executable(IamStartTest IamStart.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME IamStartTest COMMAND IamStartTest)
target_link_libraries(IamStartTest PRIVATE doctest::doctest Core)

add_executable(IamAreaTest IamArea.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME IamAreaTest COMMAND IamAreaTest)
target_link_libraries(IamAreaTest PRIVATE doctest::doctest Core)

add_executable(IamSceneTest IamScene.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME IamSceneTest COMMAND IamSceneTest)
target_link_libraries(IamSceneTest PRIVATE doctest::doctest Core)

add_executable(IamGlobalTest IamGlobal.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME IamGlobalTest COMMAND IamGlobalTest)
target_link_libraries(IamGlobalTest PRIVATE doctest::doctest Core)

add_executable(IamDialogTest IamDialog.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME IamDialogTest COMMAND IamDialogTest)
target_link_libraries(IamDialogTest PRIVATE doctest::doctest Core)

add_executable(IamObjectTest IamObject.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME IamObjectTest COMMAND IamObjectTest)
target_link_libraries(IamObjectTest PRIVATE doctest::doctest Core)

add_executable(AreaScriptRuntimeTest AreaScriptRuntime.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME AreaScriptRuntimeTest COMMAND AreaScriptRuntimeTest)
target_link_libraries(AreaScriptRuntimeTest PRIVATE doctest::doctest Core)

add_executable(AreaVmDebugStateTest AreaVmDebugState.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME AreaVmDebugStateTest COMMAND AreaVmDebugStateTest)
target_link_libraries(AreaVmDebugStateTest PRIVATE doctest::doctest Core)

add_executable(ScenarioStartupTest ScenarioStartup.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME ScenarioStartupTest COMMAND ScenarioStartupTest)
target_link_libraries(ScenarioStartupTest PRIVATE doctest::doctest Core)

add_executable(StartupCoordinatorTest StartupCoordinator.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME StartupCoordinatorTest COMMAND StartupCoordinatorTest)
target_link_libraries(StartupCoordinatorTest PRIVATE doctest::doctest Core)

add_executable(ScenarioEngineTest ScenarioEngine.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME ScenarioEngineTest COMMAND ScenarioEngineTest)
target_link_libraries(ScenarioEngineTest PRIVATE doctest::doctest Core)

add_executable(InterfaceDispatcherTest InterfaceDispatcher.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME InterfaceDispatcherTest COMMAND InterfaceDispatcherTest)
target_link_libraries(InterfaceDispatcherTest PRIVATE doctest::doctest Core)

add_executable(IamStringTableTest IamStringTable.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME IamStringTableTest COMMAND IamStringTableTest)
target_link_libraries(IamStringTableTest PRIVATE doctest::doctest Core)

add_executable(InterfaceDescriptorTest InterfaceDescriptor.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME InterfaceDescriptorTest COMMAND InterfaceDescriptorTest)
target_link_libraries(InterfaceDescriptorTest PRIVATE doctest::doctest Core)

add_executable(VideoPhaseTest VideoPhase.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME VideoPhaseTest COMMAND VideoPhaseTest)
target_link_libraries(VideoPhaseTest PRIVATE doctest::doctest Core)

add_executable(BmpImageTest BmpImage.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME BmpImageTest COMMAND BmpImageTest)
target_link_libraries(BmpImageTest PRIVATE doctest::doctest Core)

add_executable(IndexedBmp8Test IndexedBmp8.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME IndexedBmp8Test COMMAND IndexedBmp8Test)
target_link_libraries(IndexedBmp8Test PRIVATE doctest::doctest Core)

add_executable(I2DBumpEffectTest I2DBumpEffect.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME I2DBumpEffectTest COMMAND I2DBumpEffectTest)
target_link_libraries(I2DBumpEffectTest PRIVATE doctest::doctest Core)

add_executable(I2DBumpTimelineTest I2DBumpTimeline.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME I2DBumpTimelineTest COMMAND I2DBumpTimelineTest)
target_link_libraries(I2DBumpTimelineTest PRIVATE doctest::doctest Core)

add_executable(I2DPresentationTest I2DPresentation.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME I2DPresentationTest COMMAND I2DPresentationTest)
target_link_libraries(I2DPresentationTest PRIVATE doctest::doctest Core)

add_executable(DialogTextLayoutTest DialogTextLayout.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME DialogTextLayoutTest COMMAND DialogTextLayoutTest)
target_link_libraries(DialogTextLayoutTest PRIVATE doctest::doctest Core)

add_executable(I2DBlitModeTest I2DBlitMode.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME I2DBlitModeTest COMMAND I2DBlitModeTest)
target_link_libraries(I2DBlitModeTest PRIVATE doctest::doctest Core)

add_executable(SplashSceneTest SplashScene.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME SplashSceneTest COMMAND SplashSceneTest)
target_link_libraries(SplashSceneTest PRIVATE doctest::doctest Core)

add_executable(SpriteRenderModeTest SpriteRenderMode.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME SpriteRenderModeTest COMMAND SpriteRenderModeTest)
target_link_libraries(SpriteRenderModeTest PRIVATE doctest::doctest Core)

add_executable(SpriteFrameTest SpriteFrame.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME SpriteFrameTest COMMAND SpriteFrameTest)
target_link_libraries(SpriteFrameTest PRIVATE doctest::doctest Core)

add_executable(SpritePoolTest SpritePool.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME SpritePoolTest COMMAND SpritePoolTest)
target_link_libraries(SpritePoolTest PRIVATE doctest::doctest Core)

add_executable(SpriteRendererTest SpriteRenderer.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME SpriteRendererTest COMMAND SpriteRendererTest)
target_link_libraries(SpriteRendererTest PRIVATE doctest::doctest Core)

add_executable(ScriptRuntimeTest ScriptRuntime.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME ScriptRuntimeTest COMMAND ScriptRuntimeTest)
target_link_libraries(ScriptRuntimeTest PRIVATE doctest::doctest Core)

add_executable(LegacySpatializerTest LegacySpatializer.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME LegacySpatializerTest COMMAND LegacySpatializerTest)
target_link_libraries(LegacySpatializerTest PRIVATE doctest::doctest Core)

add_executable(VoicePoolTest VoicePool.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME VoicePoolTest COMMAND VoicePoolTest)
target_link_libraries(VoicePoolTest PRIVATE doctest::doctest Core)

add_executable(LogCategoryTest LogCategory.spec.cpp $<TARGET_OBJECTS:TestRunner>)
add_test(NAME LogCategoryTest COMMAND LogCategoryTest)
target_link_libraries(LogCategoryTest PRIVATE doctest::doctest Core)

# Integration tests read original game data from disk (never committed) and
# are only registered when explicitly enabled. At runtime the tests locate
# the game data through the OPENNOMAD_GAME_DATA_ROOT environment variable.
option(OPENNOMAD_GAME_DATA_TESTS "Register tests that require original game data" OFF)
if(OPENNOMAD_GAME_DATA_TESTS)
  add_executable(ThreeDMIntegrationTest
      ThreeDMIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
  add_test(NAME ThreeDMIntegrationTest COMMAND ThreeDMIntegrationTest)
  target_link_libraries(ThreeDMIntegrationTest PRIVATE doctest::doctest Core)

  add_executable(FontFNTIntegrationTest
      FontFNTIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
  add_test(NAME FontFNTIntegrationTest COMMAND FontFNTIntegrationTest)
  target_link_libraries(FontFNTIntegrationTest PRIVATE doctest::doctest Core)

  add_executable(IamDialogIntegrationTest
      IamDialogIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
  add_test(NAME IamDialogIntegrationTest COMMAND IamDialogIntegrationTest)
  target_link_libraries(IamDialogIntegrationTest PRIVATE doctest::doctest Core)

  add_executable(SCXIntegrationTest SCXIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
  add_test(NAME SCXIntegrationTest COMMAND SCXIntegrationTest)
  target_link_libraries(SCXIntegrationTest PRIVATE doctest::doctest Core)

  add_executable(SFXIntegrationTest SFXIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
  add_test(NAME SFXIntegrationTest COMMAND SFXIntegrationTest)
  target_link_libraries(SFXIntegrationTest PRIVATE doctest::doctest Core)

  add_executable(GameData3DOTextureIntegrationTest
      GameData3DOTextureIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
  add_test(NAME GameData3DOTextureIntegrationTest COMMAND GameData3DOTextureIntegrationTest)
  target_link_libraries(GameData3DOTextureIntegrationTest PRIVATE doctest::doctest Core)

  add_executable(GameDataSpriteIntegrationTest
      GameDataSpriteIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
  add_test(NAME GameDataSpriteIntegrationTest COMMAND GameDataSpriteIntegrationTest)
  target_link_libraries(GameDataSpriteIntegrationTest PRIVATE doctest::doctest Core)

  add_executable(QdAdpIntegrationTest
      QdAdpIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
  add_test(NAME QdAdpIntegrationTest COMMAND QdAdpIntegrationTest)
  target_link_libraries(QdAdpIntegrationTest PRIVATE doctest::doctest Core)

    add_executable(IamStartIntegrationTest
      IamStartIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
    add_test(NAME IamStartIntegrationTest COMMAND IamStartIntegrationTest)
    target_link_libraries(IamStartIntegrationTest PRIVATE doctest::doctest Core)

    add_executable(IamAreaIntegrationTest
      IamAreaIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
    add_test(NAME IamAreaIntegrationTest COMMAND IamAreaIntegrationTest)
    target_link_libraries(IamAreaIntegrationTest PRIVATE doctest::doctest Core)

    add_executable(IamSceneIntegrationTest
      IamSceneIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
    add_test(NAME IamSceneIntegrationTest COMMAND IamSceneIntegrationTest)
    target_link_libraries(IamSceneIntegrationTest PRIVATE doctest::doctest Core)

    add_executable(IamGlobalIntegrationTest
      IamGlobalIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
    add_test(NAME IamGlobalIntegrationTest COMMAND IamGlobalIntegrationTest)
    target_link_libraries(IamGlobalIntegrationTest PRIVATE doctest::doctest Core)

    add_executable(CtlControlSetIntegrationTest
      CtlControlSetIntegration.spec.cpp $<TARGET_OBJECTS:TestRunner>)
    add_test(NAME CtlControlSetIntegrationTest COMMAND CtlControlSetIntegrationTest)
    target_link_libraries(CtlControlSetIntegrationTest PRIVATE doctest::doctest Core)
endif()
