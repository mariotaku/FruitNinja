# tests/CMakeLists.txt — minimal ctest registration.

# AsciiString SSO correctness -- regression guard for the strlen+1/strlen mis-RE.
# Pure in-process: no GPU, no audio, no file I/O.
add_executable(test_asciistring test_asciistring.cpp)
target_link_libraries(test_asciistring PRIVATE mortar_engine)
target_include_directories(test_asciistring PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME asciistring COMMAND test_asciistring)

add_executable(test_localisation test_localisation.cpp)
target_link_libraries(test_localisation PRIVATE mortar_engine)
target_include_directories(test_localisation PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME localisation COMMAND test_localisation)

# MatrixManager singleton + matrix math correctness.
# Pure math: no GPU, no SDL, no audio. Catches singleton pattern regressions.
add_executable(test_matrix_manager test_matrix_manager.cpp)
target_link_libraries(test_matrix_manager PRIVATE mortar_engine)
target_include_directories(test_matrix_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME matrix_manager COMMAND test_matrix_manager)

# ResourceLoader / DataReader / VectorDataReader parser unit test.
# Pure in-memory (no file I/O, no GPU, no audio). Uses two deterministic
# byte vectors: (a) real apple.mad header trace, (b) hand-built 1-child
# nested buffer. High-fan-in parser: bugs here cascade into every model/asset load.
add_executable(test_resourceloader test_resourceloader.cpp)
target_link_libraries(test_resourceloader PRIVATE mortar_engine)
target_include_directories(test_resourceloader PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME resourceloader COMMAND test_resourceloader)

add_executable(test_texture_format test_texture_format.cpp)
target_link_libraries(test_texture_format PRIVATE mortar_engine)
target_include_directories(test_texture_format PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME texture_format COMMAND test_texture_format)

# LinkedHeap unit test -- no GPU/audio, pure allocator correctness.
# Exercises tail-rewind (the shutdown crash path), forward-merge, and split-path
# integrity. FN_TEST unlocks the test-only accessors in LinkedHeap.h.
add_executable(test_linkedheap test_linkedheap.cpp)
target_link_libraries(test_linkedheap PRIVATE mortar_engine)
target_include_directories(test_linkedheap PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
target_compile_definitions(test_linkedheap PRIVATE FN_TEST)
add_test(NAME linkedheap COMMAND test_linkedheap)

# Mortar::Delegate template — size + bind + invoke correctness.
add_executable(test_delegate test_delegate.cpp)
target_include_directories(test_delegate PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME delegate COMMAND test_delegate)

# Mortar::Event multicast — subscribe/fire/unsubscribe correctness.
# High-fan-in infra: ComboModifier, ExplodyFruitModifier, TimeSinkModifier,
# SuperFruitGlow, and FruitCamera all depend on this type.
# No GPU / audio dependency; pure in-process template test.
add_executable(test_event test_event.cpp)
target_include_directories(test_event PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME event COMMAND test_event)

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

# Menu-bomb scale regression test. Proves that a Dojo menu bomb ends up
# 0.825x the size of a Home menu bomb after the DojoScreen shrink is applied.
# Expected to FAIL until the root bug is fixed (Dojo restScale shrink not
# reflected in m_BaseScale capture because the capture fires in Update, after
# the DojoScreen shrink is applied -- or vice versa).
add_executable(test_menubutton_scale test_menubutton_scale.cpp)
target_link_libraries(test_menubutton_scale PRIVATE fruit-ninja-game SDL2::SDL2main)
if (WIN32)
    target_link_libraries(test_menubutton_scale PRIVATE opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_menubutton_scale PRIVATE OpenGL::GL)
endif()
if (WIN32 AND TARGET SDL2::SDL2)
    set(_sdl2_real SDL2::SDL2)
    get_target_property(_alias ${_sdl2_real} ALIASED_TARGET)
    if (_alias)
        set(_sdl2_real ${_alias})
    endif()
    get_target_property(_sdl2_type ${_sdl2_real} TYPE)
    if (_sdl2_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET test_menubutton_scale POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:test_menubutton_scale>)
    endif()
endif()
add_test(NAME menubutton_scale COMMAND test_menubutton_scale)
set_tests_properties(menubutton_scale PROPERTIES TIMEOUT 60)

# DojoScreen bomb-scale regression test. Drives the REAL DojoScreen::Update
# flow (state-0 fade-in + button creation) and asserts the back-bomb ends up
# 0.825x the Home bomb size. Catches regressions where m_pTrackedFruit is null
# at shrink time (so entity scale never gets the 0.825 factor).
add_executable(test_dojoscreen_bomb_scale test_dojoscreen_bomb_scale.cpp)
target_link_libraries(test_dojoscreen_bomb_scale PRIVATE fruit-ninja-game SDL2::SDL2main)
if (WIN32)
    target_link_libraries(test_dojoscreen_bomb_scale PRIVATE opengl32)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_dojoscreen_bomb_scale PRIVATE OpenGL::GL)
endif()
if (WIN32 AND TARGET SDL2::SDL2)
    set(_sdl2_real SDL2::SDL2)
    get_target_property(_alias ${_sdl2_real} ALIASED_TARGET)
    if (_alias)
        set(_sdl2_real ${_alias})
    endif()
    get_target_property(_sdl2_type ${_sdl2_real} TYPE)
    if (_sdl2_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET test_dojoscreen_bomb_scale POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:test_dojoscreen_bomb_scale>)
    endif()
endif()
add_test(NAME dojoscreen_bomb_scale COMMAND test_dojoscreen_bomb_scale)
set_tests_properties(dojoscreen_bomb_scale PROPERTIES TIMEOUT 60)

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

# ---- Standalone scene test harnesses (visual dev tools, NOT CTest tests) ----
# scene_fruit_scene.exe  -- single watermelon in a ballistic arc
# scene_slash_scene.exe  -- blade trail driven by mouse/touch drag
# Port specific: standalone entity test harness; excluded from asm-verify/symbol-diff.
if (NOT EMSCRIPTEN)
    add_subdirectory(scenes)
endif()

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
