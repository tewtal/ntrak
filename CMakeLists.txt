cmake_minimum_required(VERSION 3.20)

project(ntrak LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)

# Windows compatibility: static runtime linking to avoid redistributable dependencies
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# --- Dependencies via FetchContent ---

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.5-docking
)

FetchContent_Declare(
  miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.21
)

FetchContent_Declare(
  nfd
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
  GIT_TAG v1.3.0
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)

FetchContent_MakeAvailable(glfw imgui miniaudio nfd nlohmann_json googletest)

# --- Glad Setup (Manual Local Lib) ---
add_library(glad STATIC "${CMAKE_CURRENT_SOURCE_DIR}/libs/glad/src/glad.c")
target_include_directories(glad PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs/glad/include")

# --- System Dependencies ---
find_package(OpenGL REQUIRED)
find_package(X11)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    include_directories(${GTK3_INCLUDE_DIRS})
    add_definitions(-D__linux__) 
endif()

# --- ImGui Wrapper Setup ---
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)

target_link_libraries(imgui PUBLIC glad glfw)

if(X11_FOUND)
  target_link_libraries(imgui PUBLIC ${X11_LIBRARIES})
endif()

if(OPENGL_FOUND)
    target_link_libraries(imgui PUBLIC OpenGL::GL)
endif()

if (NOT MSVC)
  target_compile_options(imgui PRIVATE -Wno-nontrivial-memcall)
endif()

# --- Main Executable ---
add_executable(ntrak
  src/main.cpp
)

if(WIN32)
  # Build GUI subsystem on non-Debug configs so the app doesn't open a console window.
  set_target_properties(ntrak PROPERTIES
    WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
  )

  # Target Windows 10 (0x0A00) for better compatibility
  target_compile_definitions(ntrak PRIVATE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
  )

  # Embed application manifest for compatibility and DPI awareness
  if(MSVC)
    target_sources(ntrak PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/packaging/ntrak.manifest)
    set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/packaging/ntrak.manifest PROPERTY VS_DEPLOYMENT_CONTENT TRUE)
  endif()
endif()

add_subdirectory(src/common)
add_subdirectory(src/ui)
add_subdirectory(src/audio)
add_subdirectory(src/emulation)
add_subdirectory(src/nspc)
add_subdirectory(src/app)
add_subdirectory(src/tools)

# --- Testing ---
option(BUILD_TESTING "Build the testing tree" ON)
if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

target_include_directories(ntrak PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ntrak PRIVATE
  ntrak_app
  ntrak_ui
  ntrak_audio
  ntrak_emulation
  ntrak_nspc
  ntrak_common
)

if(UNIX AND NOT APPLE)
    target_link_libraries(ntrak PRIVATE ${GTK3_LIBRARIES})
endif()

target_compile_features(ntrak PRIVATE cxx_std_23)

if (MSVC)
  target_compile_options(ntrak PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(ntrak PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(ntrak PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_custom_command(TARGET ntrak POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:ntrak>/assets
  COMMENT "Copying assets to output directory"

  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/config
          $<TARGET_FILE_DIR:ntrak>/config
  COMMENT "Copying config to output directory"

  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/examples
          $<TARGET_FILE_DIR:ntrak>/examples
  COMMENT "Copying examples to output directory"

  COMMAND ${CMAKE_COMMAND} -E make_directory
          $<TARGET_FILE_DIR:ntrak>/docs
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/USER_GUIDE.md
          $<TARGET_FILE_DIR:ntrak>/docs/USER_GUIDE.md
  COMMENT "Copying user guide to output directory"
)

# --- Install targets (used by AppImage packaging and system installs) ---
install(TARGETS ntrak RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/ntrak/assets)
install(DIRECTORY config/ DESTINATION share/ntrak/config)
install(DIRECTORY examples/ DESTINATION share/ntrak/examples)
install(FILES USER_GUIDE.md DESTINATION share/ntrak/docs)
install(FILES packaging/ntrak.desktop DESTINATION share/applications)
install(FILES packaging/ntrak.svg DESTINATION share/icons/hicolor/scalable/apps)
