add_executable(ntrak_tests
  BrrCodecTest.cpp
  NspcAssetFileTest.cpp
  NspcEditorTest.cpp
  NspcIntegrationTest.cpp
  NspcCommandMapTest.cpp
  NspcConverterTest.cpp
  NspcFlattenTest.cpp
  NspcOptimizeTest.cpp
  SpcDspPreviewTest.cpp
  NspcProjectSongManagementTest.cpp
  NspcContentOriginTest.cpp
  NspcProjectFileTest.cpp
  NspcEngineConfigResolveTest.cpp
  NspcCompileSongScopedTest.cpp
  NspcCompileUserUploadTest.cpp
  NspcProjectFileParseSongTest.cpp
  NspcEngineConfigLoadFailureTest.cpp
  ItImportTest.cpp
)

target_include_directories(ntrak_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(ntrak_tests PRIVATE
  ntrak_nspc
  ntrak_emulation
  GTest::gtest_main
)

# Copy engine_configs.json to the test output directory so tests can find it
add_custom_command(TARGET ntrak_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ntrak_tests>/config
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/../config/engine_configs.json
    $<TARGET_FILE_DIR:ntrak_tests>/config/engine_configs.json
  COMMENT "Copying engine_configs.json to test directory"
)

include(GoogleTest)
gtest_discover_tests(ntrak_tests)
