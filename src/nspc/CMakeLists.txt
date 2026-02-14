add_library(ntrak_nspc
  Base64.cpp
  BrrCodec.cpp
  NspcAssetFile.cpp
  NspcCommand.cpp
  NspcCommandHistory.cpp
  NspcConverter.cpp
  NspcData.cpp
  NspcEditor.cpp
  NspcFlatten.cpp
  NspcCompile.cpp
  NspcCompileSongScoped.cpp
  NspcCompileUserUpload.cpp
  NspcCompileRoundTrip.cpp
  NspcParser.cpp
  NspcEngine.cpp
  NspcProject.cpp
  NspcProjectFile.cpp
  NspcOptimize.cpp
  NspcSpcExport.cpp
  ItImport.cpp
)

target_include_directories(ntrak_nspc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_features(ntrak_nspc PUBLIC cxx_std_23)

target_link_libraries(ntrak_nspc PUBLIC nlohmann_json::nlohmann_json ntrak_emulation ntrak_common)
