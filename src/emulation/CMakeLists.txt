add_library(ntrak_emulation
  SpcDsp.cpp
)

# Add ares-apu subdirectory
add_subdirectory(ares-apu)

target_include_directories(ntrak_emulation PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../include
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ntrak_emulation PUBLIC ares-apu)

target_compile_features(ntrak_emulation PUBLIC cxx_std_23)

# Suppress warnings from adapted emulation code
if (NOT MSVC)
  target_compile_options(ntrak_emulation PRIVATE 
    -Wno-unused-parameter
    -Wno-sign-compare
  )
endif()
