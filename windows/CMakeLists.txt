# Window Sweaters for Windows (x64 and ARM64).
#
#   cmake -S windows -B build/x64 -A x64
#   cmake --build build/x64 --config Release
#
# or just run windows\build.ps1, which builds both architectures and the zips.
cmake_minimum_required(VERSION 3.21)
project(WindowSweaters VERSION 1.5.0 LANGUAGES C RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Static runtime: the zip runs without installing the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(SHARED_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# The renderer, colourways and patterns shared with the macOS app, built
# against the CoreGraphics shim.
add_library(knit STATIC
  ${SHARED_SRC}/knit.c
  ${SHARED_SRC}/chart.c
  ${SHARED_SRC}/apps.c
  src/cg.c
  src/cg_win.c)
# The repo root, not src/: src/windows.h would shadow <windows.h>.
target_include_directories(knit PUBLIC shim ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(knit PUBLIC UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(knit PUBLIC $<$<COMPILE_LANGUAGE:C>:/FIcompat.h /utf-8 /W3
  /wd4244 /wd4305 /wd4267 /wd4996 /wd4068>)
target_link_libraries(knit PUBLIC windowscodecs ole32)

add_executable(WindowSweaters WIN32
  src/main.c
  src/tracker.c
  src/procinfo.c
  src/autoyarn_win.c
  src/hidden_win.c
  src/menu.c
  src/icon.c
  src/paths.c
  src/app.rc
  src/app.manifest)
target_link_libraries(WindowSweaters PRIVATE knit user32 gdi32 shell32 dwmapi shcore
  version advapi32 uuid)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets/window-sweaters.ico)
  set_source_files_properties(src/app.rc PROPERTIES COMPILE_DEFINITIONS KNIT_HAVE_ICON)
endif()

# Tests: renderer invariants plus PNGs for a visual check.
enable_testing()
add_executable(render-test tests/render_test.c tests/test_paths.c)
target_link_libraries(render-test PRIVATE knit)
add_test(NAME render COMMAND render-test)
