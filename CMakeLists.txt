cmake_minimum_required(VERSION 3.20)
# The build's version, shown in the status bar; keep installer/app.rc and
# installer/creative_cell.nsi in step.
project(creative_cell VERSION 0.1.1 LANGUAGES C CXX RC)

# Static MSVC runtime: the installed app runs on a clean Windows without the
# VC++ redistributable. Set before the dependencies so they match.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)

# --- GLFW: window + input ---
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# --- GLM: math, header-only ---
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# --- Dear ImGui: the same commit as Expanse (1.93 WIP, 2026-09-24) ---
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG e7289367973e97012a247d6b0a6c4c31b2d4216c
)
FetchContent_MakeAvailable(imgui)

if(MSVC)
  add_compile_options(/utf-8 /MP)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# --- the core: loading, the library, scanning, background work (no window;
# the self-test links it too) ---
add_library(cc_core STATIC
  src/model_loader.cpp
  src/library.cpp
  src/scanner.cpp
  src/util.cpp
  src/jobs.cpp
  src/soft_render.cpp
  third_party/ufbx/ufbx.c
  third_party/sqlite/sqlite3.c
)
target_include_directories(cc_core PUBLIC src third_party)
target_link_libraries(cc_core PUBLIC glm::glm shell32 ole32)
target_compile_definitions(cc_core PRIVATE SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_WAL_SYNCHRONOUS=1)
if(MSVC)
  set_source_files_properties(third_party/sqlite/sqlite3.c third_party/ufbx/ufbx.c PROPERTIES COMPILE_OPTIONS "/W0")
endif()

# --- the app ---
add_executable(creative_cell
  src/main.cpp
  src/app.cpp
  src/ui_style.cpp
  src/ui_chrome.cpp
  src/ui_sidebar.cpp
  src/ui_browser.cpp
  src/ui_inspector.cpp
  src/renderer.cpp
  src/thumbs.cpp
  src/nav_cube.cpp
  src/camera.cpp
  src/gl_loader.cpp
  src/file_dialog.cpp
  src/ole_drag.cpp
  src/shell_integration.cpp
  src/ui_viewer_mode.cpp
  src/stb_impl.cpp
  installer/app.rc
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(creative_cell PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(creative_cell PRIVATE cc_core glfw opengl32 dwmapi comdlg32 uuid)
target_compile_definitions(creative_cell PRIVATE
  CC_VERSION="${PROJECT_VERSION}"
  CC_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
# The shipped program: CreativeCell.exe, a windowed app (no console).
set_target_properties(creative_cell PROPERTIES OUTPUT_NAME "CreativeCell" WIN32_EXECUTABLE ON)
if(MSVC)
  target_link_options(creative_cell PRIVATE /ENTRY:mainCRTStartup)
endif()

# --- Explorer's thumbnails of 3D files (a shell extension; the app
# registers it from Settings > Windows) ---
add_library(cc_thumbs SHARED src/thumb_provider.cpp src/thumb_provider.def)
target_link_libraries(cc_thumbs PRIVATE cc_core shlwapi)
# A check of the DLL as Explorer uses it: thumbnail of a file to a PNG.
add_executable(cc_thumbtest tests/thumbtest.cpp src/stb_impl.cpp)
target_link_libraries(cc_thumbtest PRIVATE cc_core shlwapi)
add_dependencies(cc_thumbtest cc_thumbs)

# --- self-test: loaders and the library, no window ---
add_executable(cc_selftest tests/selftest.cpp)
target_link_libraries(cc_selftest PRIVATE cc_core)
enable_testing()
add_test(NAME selftest COMMAND cc_selftest)
