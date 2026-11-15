cmake_minimum_required(VERSION 3.15)
project(SurroundPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat libavcodec libavutil libswresample libswscale)

find_package(SDL2 REQUIRED)

add_executable(surround_player
    src/main.cpp
    src/player.cpp
    src/audio_dsp.cpp
    src/prologic2_decoder.cpp
)

target_include_directories(surround_player PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    src
)

target_link_libraries(surround_player PRIVATE
    ${FFMPEG_LIBRARIES}
    ${SDL2_LIBRARIES}
)

if(WIN32)
    target_link_libraries(surround_player PRIVATE ole32 uuid)
endif()
