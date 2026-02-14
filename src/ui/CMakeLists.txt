add_library(ntrak_ui
  BuildPanel.cpp
  AssetsPanel.cpp
  PatternEditorPanel.cpp
  PatternEditorPanelEditing.cpp
  PatternEditorPanelUtils.cpp
  SequenceEditorPanel.cpp
  ProjectPanel.cpp
  SongPortDialog.cpp
  UiManager.cpp
  SpcPlayerPanel.cpp
  SpcInfoPanel.cpp
  ControlPanel.cpp
  AramUsagePanel.cpp
  QuickGuidePanel.cpp
)

target_include_directories(ntrak_ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(ntrak_ui PUBLIC
  imgui
  nfd
  ntrak_audio
  ntrak_emulation
  ntrak_nspc
)

target_compile_features(ntrak_ui PUBLIC cxx_std_23)
