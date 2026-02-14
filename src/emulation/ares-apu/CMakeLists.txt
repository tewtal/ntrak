cmake_minimum_required(VERSION 3.14)

add_library(ares-apu
  src/spc700.cpp
  src/dsp.cpp
  src/smp.cpp
  src/ares-apu.cpp
)

target_include_directories(ares-apu PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_include_directories(ares-apu PRIVATE src)
# Ensure proper compilation on MSVC
if(MSVC)
  target_compile_options(ares-apu PRIVATE /permissive-)
endif()

# Example: tone generator (optional, only build if this is the top-level project)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  add_executable(ares-apu-tone examples/tone.cpp)
  target_link_libraries(ares-apu-tone PRIVATE ares-apu)
endif()
