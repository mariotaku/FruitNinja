cmake_minimum_required(VERSION 3.10)
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

add_executable(fruit-ninja
    src/main.cpp
    src/gl_funcs.cpp
    src/tex_loader.cpp
    src/renderer.cpp
    src/game.cpp
    src/splash_screen.cpp
    src/menu_button.cpp
    src/dojo_screen.cpp
    src/mesh.cpp
    src/main_screen.cpp
)

target_include_directories(fruit-ninja PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(fruit-ninja PRIVATE SDL2::SDL2main SDL2::SDL2 tinyxml2::tinyxml2)

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/FruitNinjaBada/Data" FN_DATA_DIR_PATH)
configure_file(src/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)
target_include_directories(fruit-ninja PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
