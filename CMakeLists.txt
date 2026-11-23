cmake_minimum_required(VERSION 3.20)
project(zt_port CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL3 REQUIRED CONFIG)

file(GLOB ZT_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp)
add_executable(zt
  src/main.cpp
  src/host.cpp
  src/assets.cpp
  src/audio.cpp
  src/music.cpp
  src/ymfm/ymfm_opn.cpp
  src/ymfm/ymfm_adpcm.cpp
  src/ymfm/ymfm_ssg.cpp
  src/input.cpp
  src/md/gems.cpp
  ${ZT_GAME_SOURCES}
)
target_compile_options(zt PRIVATE -Wall -Wextra)
set_source_files_properties(src/audio.cpp src/music.cpp src/ymfm/ymfm_opn.cpp src/ymfm/ymfm_adpcm.cpp
  src/ymfm/ymfm_ssg.cpp PROPERTIES COMPILE_OPTIONS -Wno-unused-parameter)
target_link_libraries(zt PRIVATE SDL3::SDL3)
if(WIN32)
  # No console window; no MinGW runtime DLLs to ship; no debug info from the
  # static MinGW libraries.
  set_target_properties(zt PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(zt PRIVATE -static -s)
endif()
set_target_properties(zt PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
