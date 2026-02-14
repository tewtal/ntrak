add_library(ntrak_audio
  AudioEngine.cpp
  SpcPlayer.cpp
)

target_include_directories(ntrak_audio PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
  ${miniaudio_SOURCE_DIR}
)

target_link_libraries(ntrak_audio PUBLIC
  ntrak_emulation
)

target_compile_features(ntrak_audio PUBLIC cxx_std_23)
