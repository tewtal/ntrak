add_executable(ntrak_song_dump
  NspcSongDump.cpp
)

target_include_directories(ntrak_song_dump PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(ntrak_song_dump PRIVATE
  ntrak_nspc
  ntrak_emulation
)

target_compile_features(ntrak_song_dump PRIVATE cxx_std_23)

if (MSVC)
  target_compile_options(ntrak_song_dump PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(ntrak_song_dump PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET ntrak_song_dump POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ntrak_song_dump>/config
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/../../config/engine_configs.json
    $<TARGET_FILE_DIR:ntrak_song_dump>/config/engine_configs.json
  COMMENT "Copying engine_configs.json to tool output directory"
)
