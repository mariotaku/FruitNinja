# tests/scenes/CMakeLists.txt
#
# Standalone scene test harnesses -- visual dev tools for debugging individual
# engine entities without the full Splash->Game flow.
#
# These are NOT registered as CTest tests; they are interactive programs
# intended to be launched manually when the GPU is available.
#
# Each links against fruit-ninja-game (which carries GameInitialise, all
# entity types, ActorManager, Renderer, etc.) and SDL2.
#
# Build alongside the main fruit-ninja target:
#   cmake --build build/host -j
#
# Run (GPU required):
#   ./build/host/tests/scenes/scene_fruit_scene.exe   --interactive
#   ./build/host/tests/scenes/scene_slash_scene.exe   --interactive

# Port specific: standalone entity test harness -- excluded from asm-verify /
# symbol-diff scope because the files are under tests/ not src/.

# ---- helper macro: adds one scene test exe with the shared boilerplate ----
macro(add_scene_test _target _source)
    add_executable(${_target} ${_source})

    target_include_directories(${_target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/tests
    )

    target_link_libraries(${_target} PRIVATE fruit-ninja-game SDL2::SDL2main)

    if (WIN32)
        target_link_libraries(${_target} PRIVATE opengl32)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(${_target} PRIVATE OpenGL::GL)
    endif()

    # Copy SDL2.dll next to the exe on Windows so it can be run in-place.
    if (WIN32 AND TARGET SDL2::SDL2)
        set(_sdl2_real SDL2::SDL2)
        get_target_property(_alias ${_sdl2_real} ALIASED_TARGET)
        if (_alias)
            set(_sdl2_real ${_alias})
        endif()
        get_target_property(_sdl2_type ${_sdl2_real} TYPE)
        if (_sdl2_type STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET ${_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2::SDL2>
                    $<TARGET_FILE_DIR:${_target}>)
        endif()
    endif()
endmacro()

# ---- scene_fruit: single watermelon in a ballistic arc ----
add_scene_test(scene_fruit_scene scene_fruit.cpp)

# ---- scene_slash: blade trail driven by mouse/touch drag ----
add_scene_test(scene_slash_scene scene_slash.cpp)

# ---- scene_bomb_scale: pixel-level bomb-scale regression test ----
# Renders HOME and DOJO bombs side-by-side, reads back the framebuffer, and
# asserts that the DOJO bomb's rendered bounding-box is ~0.825x the HOME bomb's.
# Registers as a CTest so it can be run headless in CI.
# Port specific: standalone entity scale regression test
add_scene_test(scene_bomb_scale scene_bomb_scale.cpp)
add_test(NAME bomb_scale COMMAND scene_bomb_scale)
set_tests_properties(bomb_scale PROPERTIES TIMEOUT 60)
