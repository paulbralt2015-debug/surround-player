cmake_minimum_required(VERSION 3.15)
project(SurroundPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Trouver FFmpeg via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat libavcodec libavutil libswresample libswscale)

# Trouver SDL2 via son module CMake officiel
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

# Liaison CORRECTE : SDL2::SDL2main AVANT SDL2::SDL2
# Sur MSYS2/MinGW, l'ordre est critique !
target_link_libraries(surround_player PRIVATE
    ${FFMPEG_LIBRARIES}
    SDL2::SDL2main
    SDL2::SDL2
)

if(WIN32)
    target_link_libraries(surround_player PRIVATE ole32 uuid)
endif()
