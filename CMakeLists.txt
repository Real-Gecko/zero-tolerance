cmake_minimum_required(VERSION 3.20)
project(zt_port CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth)

file(GLOB ZT_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp)
add_executable(zt
  src/main.cpp
  src/host.cpp
  src/audio.cpp
  src/input.cpp
  src/md/mem.cpp
  src/md/vdp.cpp
  src/md/pad.cpp
  src/md/gems.cpp
  ${ZT_GAME_SOURCES}
)
target_compile_options(zt PRIVATE -Wall -Wextra)
target_link_libraries(zt PRIVATE SDL3::SDL3 PkgConfig::FLUIDSYNTH)
set_target_properties(zt PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Shared library for tools/difftest.py (translated routines vs. the ROM in Unicorn).
add_library(zt_test SHARED EXCLUDE_FROM_ALL
  test/zt_test.cpp src/audio.cpp src/md/mem.cpp src/md/vdp.cpp src/md/pad.cpp src/md/gems.cpp ${ZT_GAME_SOURCES})
target_include_directories(zt_test PRIVATE src)
target_link_libraries(zt_test PRIVATE SDL3::SDL3 PkgConfig::FLUIDSYNTH)

# 68000 interpreter running the original game code for tools/difftest.py.
add_library(m68k SHARED EXCLUDE_FROM_ALL tools/m68k/m68k.cpp tools/m68k/api.cpp tools/m68k/md_test.cpp)
target_compile_options(m68k PRIVATE -Wall -Wextra)

# The original ROM on the interpreter with the port's VDP (tools/m68k/ztref.cpp).
add_executable(ztref EXCLUDE_FROM_ALL tools/m68k/ztref.cpp tools/m68k/m68k.cpp src/md/mem.cpp src/md/vdp.cpp src/md/pad.cpp)
target_include_directories(ztref PRIVATE src)
target_compile_options(ztref PRIVATE -Wall -Wextra)
target_link_libraries(ztref PRIVATE SDL3::SDL3)
