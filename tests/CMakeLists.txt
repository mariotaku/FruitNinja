# tests/CMakeLists.txt — minimal ctest registration.

add_executable(test_localisation test_localisation.cpp)
target_link_libraries(test_localisation PRIVATE mortar_engine)
target_include_directories(test_localisation PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME localisation COMMAND test_localisation)

# LinkedHeap unit test -- no GPU/audio, pure allocator correctness.
# Exercises tail-rewind (the shutdown crash path) and forward-merge.
add_executable(test_linkedheap test_linkedheap.cpp)
target_link_libraries(test_linkedheap PRIVATE mortar_engine)
target_include_directories(test_linkedheap PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME linkedheap COMMAND test_linkedheap)

# Mortar::Delegate template — size + bind + invoke correctness.
add_executable(test_delegate test_delegate.cpp)
target_include_directories(test_delegate PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME delegate COMMAND test_delegate)

# Per-screen smoke test. Each entry boots a hidden SDL+GL context,
# inits the full game, pushes the named screen, and ticks 30 frames.
# Catches font / vertex / matrix / divide-by-zero regressions that
# the home screen alone doesn't exercise.
add_executable(test_screen test_screen.cpp)
target_link_libraries(test_screen PRIVATE fruit-ninja-game SDL2::SDL2main)
if (WIN32)
    target_link_libraries(test_screen PRIVATE opengl32)
endif()
# Linux/macOS: glReadPixels resolved at runtime via SDL_GL_GetProcAddress
# inside test_screen.cpp -- no static GL link needed.

# SDL2.dll must sit next to test_screen.exe on Windows (same as the
# fruit-ninja exe). copy_if_different keeps the rule cheap on rebuilds.
if (WIN32 AND TARGET SDL2::SDL2)
    set(_sdl2_real SDL2::SDL2)
    get_target_property(_alias ${_sdl2_real} ALIASED_TARGET)
    if (_alias)
        set(_sdl2_real ${_alias})
    endif()
    get_target_property(_sdl2_type ${_sdl2_real} TYPE)
    if (_sdl2_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET test_screen POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:test_screen>)
    endif()
endif()

foreach(screen main dojo about shop gamemode gameover gameover-transition classic)
    add_test(NAME screen_${screen} COMMAND test_screen ${screen})
    set_tests_properties(screen_${screen} PROPERTIES TIMEOUT 60)
endforeach()

# Per-mode gameplay smoke test. Boots GameInit, fires SetupLevel for the
# named mode, ticks ~3s, asserts wave count + at least one spawn.
add_executable(test_gameplay test_gameplay.cpp)
target_link_libraries(test_gameplay PRIVATE fruit-ninja-game SDL2::SDL2main)
if (WIN32)
    target_link_libraries(test_gameplay PRIVATE opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_gameplay PRIVATE OpenGL::GL)
endif()
if (WIN32 AND TARGET SDL2::SDL2)
    set(_sdl2_real SDL2::SDL2)
    get_target_property(_alias ${_sdl2_real} ALIASED_TARGET)
    if (_alias)
        set(_sdl2_real ${_alias})
    endif()
    get_target_property(_sdl2_type ${_sdl2_real} TYPE)
    if (_sdl2_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET test_gameplay POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:test_gameplay>)
    endif()
endif()
foreach(mode classic combo arcade zen)
    add_test(NAME gameplay_${mode} COMMAND test_gameplay ${mode})
    set_tests_properties(gameplay_${mode} PROPERTIES TIMEOUT 60)
endforeach()

# Bomb-spawn diagnostic. Spawns bombs directly via WaveManager::SpawnBomb,
# ticks 200 frames per variant, asserts no entity leak.
# To see per-frame trace output, configure with -DFRUITNINJA_BOMB_TRACE=ON
# (that define propagates to fruit-ninja-game; the test exe itself carries
# no trace define since Bomb.cpp lives in the library).
add_executable(test_bomb_spawn test_bomb_spawn.cpp)
target_link_libraries(test_bomb_spawn PRIVATE fruit-ninja-game SDL2::SDL2main)
if (WIN32)
    target_link_libraries(test_bomb_spawn PRIVATE opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_bomb_spawn PRIVATE OpenGL::GL)
endif()
if (WIN32 AND TARGET SDL2::SDL2)
    set(_sdl2_real SDL2::SDL2)
    get_target_property(_alias ${_sdl2_real} ALIASED_TARGET)
    if (_alias)
        set(_sdl2_real ${_alias})
    endif()
    get_target_property(_sdl2_type ${_sdl2_real} TYPE)
    if (_sdl2_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET test_bomb_spawn POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:test_bomb_spawn>)
    endif()
endif()
add_test(NAME bomb_spawn COMMAND test_bomb_spawn all)
set_tests_properties(bomb_spawn PROPERTIES TIMEOUT 60)

# BONUS_PHASE stall regression test. Boots Arcade-mode GameOverScreen in
# STATE_BONUS_PHASE and ticks 600 frames (10s). Asserts state advances
# to STATE_MAIN_DISPLAY within the timeout. Currently FAILS (stall bug).
add_executable(test_bonus_phase test_bonus_phase.cpp)
target_link_libraries(test_bonus_phase PRIVATE fruit-ninja-game SDL2::SDL2main)
if (WIN32)
    target_link_libraries(test_bonus_phase PRIVATE opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_bonus_phase PRIVATE OpenGL::GL)
endif()
if (WIN32 AND TARGET SDL2::SDL2)
    set(_sdl2_real SDL2::SDL2)
    get_target_property(_alias ${_sdl2_real} ALIASED_TARGET)
    if (_alias)
        set(_sdl2_real ${_alias})
    endif()
    get_target_property(_sdl2_type ${_sdl2_real} TYPE)
    if (_sdl2_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET test_bonus_phase POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:test_bonus_phase>)
    endif()
endif()
add_test(NAME bonus_phase COMMAND test_bonus_phase)
set_tests_properties(bonus_phase PROPERTIES TIMEOUT 120)

# BonusScreen unit test. Constructs a BonusScreen directly with 3 faked
# awards (no BonusManager), ticks frames, asserts size populated, score
# advances, dismiss timer fires. Catches ctor / Update / phase-timer
# regressions without needing to play a full Arcade run.
add_executable(test_bonus_screen test_bonus_screen.cpp)
target_link_libraries(test_bonus_screen PRIVATE fruit-ninja-game SDL2::SDL2main)
if (WIN32)
    target_link_libraries(test_bonus_screen PRIVATE opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_bonus_screen PRIVATE OpenGL::GL)
endif()
if (WIN32 AND TARGET SDL2::SDL2)
    set(_sdl2_real SDL2::SDL2)
    get_target_property(_alias ${_sdl2_real} ALIASED_TARGET)
    if (_alias)
        set(_sdl2_real ${_alias})
    endif()
    get_target_property(_sdl2_type ${_sdl2_real} TYPE)
    if (_sdl2_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET test_bonus_screen POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:test_bonus_screen>)
    endif()
endif()
add_test(NAME bonus_screen COMMAND test_bonus_screen)
set_tests_properties(bonus_screen PROPERTIES TIMEOUT 60)

# Per-mode spawn lifecycle diagnostic. Logs every fruit/bomb spawn with
# full pos/vel/gravity parameters plus per-wave spawner config so
# divergences vs. binary expectations are readable line-by-line. Always
# returns success unless setup fails -- it's a log dump, not pass/fail.
add_executable(test_spawn_lifecycle test_spawn_lifecycle.cpp)
target_link_libraries(test_spawn_lifecycle PRIVATE fruit-ninja-game SDL2::SDL2main)
if (WIN32)
    target_link_libraries(test_spawn_lifecycle PRIVATE opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_spawn_lifecycle PRIVATE OpenGL::GL)
endif()
if (WIN32 AND TARGET SDL2::SDL2)
    set(_sdl2_real SDL2::SDL2)
    get_target_property(_alias ${_sdl2_real} ALIASED_TARGET)
    if (_alias)
        set(_sdl2_real ${_alias})
    endif()
    get_target_property(_sdl2_type ${_sdl2_real} TYPE)
    if (_sdl2_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET test_spawn_lifecycle POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:test_spawn_lifecycle>)
    endif()
endif()
foreach(spawn_mode classic arcade zen)
    add_test(NAME ${spawn_mode}_spawn COMMAND test_spawn_lifecycle --mode=${spawn_mode})
endforeach()
# Per-mode timeout matches the in-game session length + slack:
#   classic 30s + 5s buffer + 15s slack = 50s
#   arcade  60s + 5s buffer + 30s slack = 90s
#   zen     90s + 5s buffer + 30s slack = 120s
set_tests_properties(classic_spawn PROPERTIES TIMEOUT 50)
set_tests_properties(arcade_spawn  PROPERTIES TIMEOUT 90)
set_tests_properties(zen_spawn     PROPERTIES TIMEOUT 120)
