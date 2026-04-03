cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0079 NEW)
project(FruitNinja LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Dependencies ---

find_package(SDL2 REQUIRED)

include(FetchContent)
FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG        10.0.0
)
FetchContent_MakeAvailable(tinyxml2)

# --- Executable ---

add_executable(fruit-ninja src/main.cpp src/Game.cpp)
target_include_directories(fruit-ninja PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(fruit-ninja PRIVATE SDL2::SDL2main SDL2::SDL2 tinyxml2::tinyxml2)

# Game.cpp needs headers from subsystems (linked via sub-CMakeLists)
# Each subsystem does: target_link_libraries(fruit-ninja PRIVATE fn_xxx)

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/FruitNinjaBada/Data" FN_DATA_DIR_PATH)
configure_file(src/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

# --- Subsystems (order matters for dependencies) ---
add_subdirectory(src/math)
add_subdirectory(src/engine)
add_subdirectory(src/hud)
add_subdirectory(src/screens)
add_subdirectory(src/platform)
add_subdirectory(src/entities)
add_subdirectory(src/game)
add_subdirectory(src/effects)
add_subdirectory(src/powerups)
add_subdirectory(src/sound)
