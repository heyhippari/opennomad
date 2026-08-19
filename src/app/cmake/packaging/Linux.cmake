# Copy the runtime shared libraries provided by vcpkg to the target App build folder.
# For development:
add_custom_command(TARGET ${NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
  $<TARGET_FILE:SDL3::SDL3>
  $<TARGET_FILE:SDL3_image::SDL3_image-shared>
  $<TARGET_FILE:spdlog::spdlog>
  $<TARGET_FILE:fmt::fmt>
  $<TARGET_FILE_DIR:${NAME}>)

# For distribution: install the full dependency closure (includes transitive libs like
# libasound/libfreetype pulled in by SDL3) next to the executable.
install(DIRECTORY ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  FILES_MATCHING PATTERN "*.so.*")

# Find the bundled shared libraries relative to the executable.
set_target_properties(${NAME} PROPERTIES
  INSTALL_RPATH "$ORIGIN"
  BUILD_WITH_INSTALL_RPATH FALSE)

# Copy assets into app bundle
# For development:
add_custom_command(TARGET ${NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  ${PROJECT_SOURCE_DIR}/src/assets
  $<TARGET_FILE_DIR:${NAME}>/../share)

# For distribution:
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/assets/ DESTINATION ${CMAKE_INSTALL_DATADIR})

# Linux app icon setup
configure_file(
  ${PROJECT_SOURCE_DIR}/src/app/Manifests/App.desktop.in
  ${CMAKE_CURRENT_BINARY_DIR}/App.desktop
  @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/App.desktop
  DESTINATION share/applications)
install(FILES ${PROJECT_SOURCE_DIR}/src/assets/icons/BaseAppIcon.png
  DESTINATION share/pixmaps
  RENAME ${APP_NAME}_icon.png)
