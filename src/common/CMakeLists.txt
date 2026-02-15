add_library(ntrak_common
  Log.cpp
  Logger.cpp
  Paths.cpp
  UserGuide.cpp
)

target_include_directories(ntrak_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_compile_features(ntrak_common PUBLIC cxx_std_23)
