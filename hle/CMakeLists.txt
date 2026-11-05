cmake_minimum_required(VERSION 3.16)
project(MacHLE C)

# Per-game identity (see src/config.h for what each one means).
set(HLE_APP_NAME      "Classic Mac App" CACHE STRING "Display name of the ported app")
set(HLE_APP_SLUG      "classic-mac-app" CACHE STRING "lower-case-dashed name (Linux data folder)")
set(HLE_APP_FILE      "App.rsrc"        CACHE STRING "Default file name of the app's resource fork")
set(HLE_EXE_NAME      "hle"             CACHE STRING "Executable / bundle name")

set(CMAKE_C_STANDARD 99)
if(EXISTS ${CMAKE_SOURCE_DIR}/VERSION)
  file(STRINGS ${CMAKE_SOURCE_DIR}/VERSION HLE_VERSION LIMIT_COUNT 1)
else()
  set(HLE_VERSION dev)
endif()

set(HLE_CORE
  src/main.c src/trap.c src/mem.c src/rsrc.c src/qd.c src/rgn.c src/font.c src/wm.c
  src/menu.c src/ctl.c src/dlg.c src/evt.c src/fs.c src/pack.c src/plat.c
  musashi/m68kcpu.c musashi/m68kops.c musashi/m68kdasm.c musashi/softfloat/softfloat.c)

if(WIN32)
  add_executable(hle WIN32 ${HLE_CORE} src/host_win.c src/audio_win.c)
  target_link_libraries(hle user32 gdi32 winmm comdlg32 shell32 ole32)
  if(MSVC)
    target_compile_definitions(hle PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_options(hle PRIVATE /ENTRY:mainCRTStartup)
  endif()
else()
  # SDL2, built statically from source by default so the result has no runtime dependency on it
  option(HLE_FETCH_SDL "Build SDL2 from source and link it statically" ON)
  if(HLE_FETCH_SDL)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2 URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
    FetchContent_MakeAvailable(SDL2)
    set(HLE_SDL SDL2::SDL2-static)
  else()
    find_package(SDL2 REQUIRED)
    set(HLE_SDL SDL2::SDL2)
  endif()
  if(APPLE)
    add_executable(hle MACOSX_BUNDLE ${HLE_CORE} src/host_sdl.c)
    set_target_properties(hle PROPERTIES
      MACOSX_BUNDLE_BUNDLE_NAME "${HLE_APP_NAME}"
      MACOSX_BUNDLE_BUNDLE_VERSION ${HLE_VERSION}
      MACOSX_BUNDLE_SHORT_VERSION_STRING ${HLE_VERSION})
  else()
    add_executable(hle ${HLE_CORE} src/host_sdl.c)
  endif()
  target_link_libraries(hle ${HLE_SDL} m)
  target_compile_options(hle PRIVATE -Wno-unused-result -Wno-format-truncation)
endif()

set_target_properties(hle PROPERTIES OUTPUT_NAME "${HLE_EXE_NAME}")
target_include_directories(hle PRIVATE src musashi)
target_compile_definitions(hle PRIVATE
  HLE_VERSION="${HLE_VERSION}"
  HLE_APP_NAME="${HLE_APP_NAME}"
  HLE_APP_SLUG="${HLE_APP_SLUG}"
  HLE_APP_FILE="${HLE_APP_FILE}")
