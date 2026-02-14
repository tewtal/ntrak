add_library(ntrak_app
  App.cpp
)

target_include_directories(ntrak_app PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(ntrak_app PUBLIC
  imgui
  glfw
  OpenGL::GL
  ntrak_ui
  ntrak_common
)

target_compile_features(ntrak_app PUBLIC cxx_std_23)
