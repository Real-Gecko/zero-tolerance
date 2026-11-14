cmake_minimum_required(VERSION 3.20)
project(zt_port CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL3 REQUIRED CONFIG)
if(WIN32)
  # The official FluidSynth Windows release: its .pc and CMake config point at
  # the CI paths and dependencies it was built with.
  find_path(FLUIDSYNTH_INCLUDE_DIR fluidsynth.h REQUIRED)
  find_library(FLUIDSYNTH_LIBRARY NAMES fluidsynth-3 REQUIRED)
  add_library(fluidsynth INTERFACE)
  target_include_directories(fluidsynth INTERFACE ${FLUIDSYNTH_INCLUDE_DIR})
  target_link_libraries(fluidsynth INTERFACE ${FLUIDSYNTH_LIBRARY})
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth)
  add_library(fluidsynth ALIAS PkgConfig::FLUIDSYNTH)
endif()

file(GLOB ZT_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp)
add_executable(zt
  src/main.cpp
  src/host.cpp
  src/assets.cpp
  src/audio.cpp
  src/input.cpp
  src/md/gems.cpp
  ${ZT_GAME_SOURCES}
)
target_compile_options(zt PRIVATE -Wall -Wextra)
target_link_libraries(zt PRIVATE SDL3::SDL3 fluidsynth)
if(WIN32)
  # No console window; no MinGW runtime DLLs to ship; no debug info from the
  # static MinGW libraries.
  set_target_properties(zt PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(zt PRIVATE -static -s)
endif()
set_target_properties(zt PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
