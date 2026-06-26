# tests/CMakeLists.txt -- minimal ctest registration.

# ---------------------------------------------------------------------------
# fn_png_impl: single-TU that provides FN_PNG_WRITE_IMPLEMENTATION so that
# the encoder in tests/third_party/fn_png_write.h is linked once into every
# game-test executable via fn_add_game_test. Keeps the implementation define
# out of test_harness.h (which is a header-only include in many TUs).
# ---------------------------------------------------------------------------
add_library(fn_png_impl OBJECT fn_png_impl.cpp)
target_include_directories(fn_png_impl PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

# ---------------------------------------------------------------------------
# Helper macro: fn_add_game_test
#
# Registers an executable that links against fruit-ninja-game, SDL2::SDL2main,
# and the platform GL library. Also copies SDL2.dll next to the exe on Windows.
# This replaces the ~20 lines of boilerplate each test used to carry verbatim.
#
# Usage:
#   fn_add_game_test(<target> <source>)
#
# The caller then adds include dirs, ctest entries, and test properties as
# needed after the macro call -- the macro only wires the shared link/copy
# boilerplate.
#
# opengl32 is linked on WIN32 only; on non-WIN32 OpenGL::GL is found and linked
# (except for test_screen which resolves glReadPixels at runtime -- that target
# is still registered via fn_add_game_test but callers can omit its GL assertion
# since the linker doesn't need it on non-WIN32 there either; the macro always
# links OpenGL::GL on non-WIN32 for consistency).
# ---------------------------------------------------------------------------
macro(fn_add_game_test _target _source)
    add_executable(${_target} ${_source} $<TARGET_OBJECTS:fn_png_impl>)

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

    # Copy SDL2.dll next to the exe on Windows so the test can run in-place.
    if (WIN32 AND TARGET SDL2::SDL2)
        set(_fn_sdl2_real SDL2::SDL2)
        get_target_property(_fn_alias ${_fn_sdl2_real} ALIASED_TARGET)
        if (_fn_alias)
            set(_fn_sdl2_real ${_fn_alias})
        endif()
        get_target_property(_fn_sdl2_type ${_fn_sdl2_real} TYPE)
        if (_fn_sdl2_type STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET ${_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2::SDL2>
                    $<TARGET_FILE_DIR:${_target}>)
        endif()
    endif()
endmacro()

# ---------------------------------------------------------------------------
# Pure-logic tests (no SDL, no GL, no audio)
# ---------------------------------------------------------------------------

# FixedStepDriver unit test -- no SDL, no GL, no audio.
# Covers steady 60/144/30 Hz streams, 500ms spike clamping, alpha bounds.
# Header-only target: only needs the src/ include path.
add_executable(test_fixedstep test_fixedstep.cpp)
target_include_directories(test_fixedstep PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME fixedstep COMMAND test_fixedstep)

# AsciiString SSO correctness -- regression guard for the strlen+1/strlen mis-RE.
# Pure in-process: no GPU, no audio, no file I/O.
add_executable(test_asciistring test_asciistring.cpp)
target_link_libraries(test_asciistring PRIVATE mortar_engine)
target_include_directories(test_asciistring PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME asciistring COMMAND test_asciistring)

# EffectPropertyValues / EffectPropertyList / GetColourRGB unit test.
# Exercises the 9-def mesh-material property set, SetValue/TryGetValue for
# float/bool/Vec3/Texture2D types, and parent-chain lookup.
# Pure in-process: no GPU, no audio, no file I/O.
add_executable(test_effectproperties test_effectproperties.cpp)
target_link_libraries(test_effectproperties PRIVATE mortar_engine)
target_include_directories(test_effectproperties PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME effectproperties COMMAND test_effectproperties)

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

# Mortar::Delegate template -- size + bind + invoke correctness.
add_executable(test_delegate test_delegate.cpp)
target_include_directories(test_delegate PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME delegate COMMAND test_delegate)

# Mortar::Event multicast -- subscribe/fire/unsubscribe correctness.
# High-fan-in infra: ComboModifier, ExplodyFruitModifier, TimeSinkModifier,
# SuperFruitGlow, and FruitCamera all depend on this type.
# No GPU / audio dependency; pure in-process template test.
add_executable(test_event test_event.cpp)
target_include_directories(test_event PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME event COMMAND test_event)

# BakedStringTTF unit test -- struct layout, arc math, gradient math.
# Does NOT call any BakedStringTTF methods (avoids the GL-using TU).
# Pure in-process: no GPU, no audio, no FreeType.
# Cross-build safe: no lambdas, no auto, no range-for, no enum class.
add_executable(test_bakedstringttf test_bakedstringttf.cpp)
target_link_libraries(test_bakedstringttf PRIVATE mortar_engine)
target_include_directories(test_bakedstringttf PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME bakedstringttf COMMAND test_bakedstringttf)

# ---------------------------------------------------------------------------
# Visual / component tests (SDL + GL + TestHarness)
# All use fn_add_game_test for shared link/copy boilerplate.
# ---------------------------------------------------------------------------

# Per-screen smoke test. Each entry boots a hidden SDL+GL context,
# inits the full game, pushes the named screen, and ticks 30 frames.
# Catches font / vertex / matrix / divide-by-zero regressions that
# the home screen alone doesn't exercise.
fn_add_game_test(test_screen test_screen.cpp)
foreach(screen main dojo about shop gamemode gameover gameover-transition classic)
    add_test(NAME screen_${screen} COMMAND test_screen ${screen})
    set_tests_properties(screen_${screen} PROPERTIES TIMEOUT 60)
endforeach()

# Per-mode gameplay smoke test. Boots GameInit, fires SetupLevel for the
# named mode, ticks ~3s, asserts wave count + at least one spawn.
fn_add_game_test(test_gameplay test_gameplay.cpp)
foreach(mode classic combo arcade zen)
    add_test(NAME gameplay_${mode} COMMAND test_gameplay ${mode})
    set_tests_properties(gameplay_${mode} PROPERTIES TIMEOUT 60)
endforeach()

# Bomb-spawn diagnostic. Spawns bombs directly via WaveManager::SpawnBomb,
# ticks 200 frames per variant, asserts no entity leak.
# To see per-frame trace output, configure with -DFRUITNINJA_BOMB_TRACE=ON
# (that define propagates to fruit-ninja-game; the test exe itself carries
# no trace define since Bomb.cpp lives in the library).
fn_add_game_test(test_bomb_spawn test_bomb_spawn.cpp)
add_test(NAME bomb_spawn COMMAND test_bomb_spawn all)
set_tests_properties(bomb_spawn PROPERTIES TIMEOUT 60)

# Menu-bomb scale regression test. Proves that a Dojo menu bomb ends up
# 0.825x the size of a Home menu bomb after the DojoScreen shrink is applied.
# Expected to FAIL until the root bug is fixed (Dojo restScale shrink not
# reflected in m_BaseScale capture because the capture fires in Update, after
# the DojoScreen shrink is applied -- or vice versa).
fn_add_game_test(test_menubutton_scale test_menubutton_scale.cpp)
add_test(NAME menubutton_scale COMMAND test_menubutton_scale)
set_tests_properties(menubutton_scale PROPERTIES TIMEOUT 60)

# DojoScreen bomb-scale regression test. Drives the REAL DojoScreen::Update
# flow (state-0 fade-in + button creation) and asserts the back-bomb ends up
# 0.825x the Home bomb size. Catches regressions where m_pTrackedFruit is null
# at shrink time (so entity scale never gets the 0.825 factor).
fn_add_game_test(test_dojoscreen_bomb_scale test_dojoscreen_bomb_scale.cpp)
add_test(NAME dojoscreen_bomb_scale COMMAND test_dojoscreen_bomb_scale)
set_tests_properties(dojoscreen_bomb_scale PROPERTIES TIMEOUT 60)

# Menu-fruit round-trip regression test (#130). Drives the REAL game path:
# MainScreen idle -> Play-button slice -> GameModeScreen -> entity pool
# exercise -> GameOverScreen STATE_QUIT_WAIT -> DoQuitToMenu -> fresh
# MainScreen. Asserts both Play and Dojo fruits are healthy (non-frozen,
# spinning) after the round-trip. Proves the #130 symptom is NOT in the
# state-machine or entity-pool recycling on the faithful port path.
fn_add_game_test(test_menu_fruit_roundtrip test_menu_fruit_roundtrip.cpp)
add_test(NAME menu_fruit_roundtrip COMMAND test_menu_fruit_roundtrip)
set_tests_properties(menu_fruit_roundtrip PROPERTIES TIMEOUT 120)

# BONUS_PHASE stall regression test. Boots Arcade-mode GameOverScreen in
# STATE_BONUS_PHASE and ticks 600 frames (10s). Asserts state advances
# to STATE_MAIN_DISPLAY within the timeout. Currently FAILS (stall bug).
fn_add_game_test(test_bonus_phase test_bonus_phase.cpp)
add_test(NAME bonus_phase COMMAND test_bonus_phase)
set_tests_properties(bonus_phase PROPERTIES TIMEOUT 120)

# BonusScreen unit test. Constructs a BonusScreen directly with 3 faked
# awards (no BonusManager), ticks frames, asserts size populated, score
# advances, dismiss timer fires. Catches ctor / Update / phase-timer
# regressions without needing to play a full Arcade run.
fn_add_game_test(test_bonus_screen test_bonus_screen.cpp)
add_test(NAME bonus_screen COMMAND test_bonus_screen)
set_tests_properties(bonus_screen PROPERTIES TIMEOUT 60)

# Render interpolation determinism + recycle tests.
# Covers: byte-identity after Apply(0.5)+Restore(), and recycle-guard
# (new pool occupant has hadPrev==false, no teleport-lerp from dead entity).
# When FN_RENDER_INTERP is OFF the test body compiles to a trivial pass.
fn_add_game_test(test_render_interp test_render_interp.cpp)
add_test(NAME render_interp COMMAND test_render_interp)
set_tests_properties(render_interp PROPERTIES TIMEOUT 60)

# SlashEntity::Reset() bridging-triangle regression guard.
# Proves that Reset fully wipes both ribbon buffers (pos=0, not just colour=white)
# so the head-cap slot at index m_PointCount does not carry stale positions from
# a prior slice. Pure buffer-content check: no SDL init, no GL, no audio --
# SlashEntity init only allocates heap memory (ColLine, vertex buffers).
fn_add_game_test(test_slash_reset test_slash_reset.cpp)
add_test(NAME slash_reset COMMAND test_slash_reset)
set_tests_properties(slash_reset PROPERTIES TIMEOUT 30)

# InputTranslatorSDL drain/dispatch split invariant (#173 bridge-to-origin fix).
# Verifies that DrainSDLEvent() accumulates pending state WITHOUT dispatching
# and DispatchForSimTick() consumes pending state. Pins the invariant that
# m_PointCount (AddPoint) only advances inside a tick that also runs UpdatePoints.
# Covers: FINGERDOWN drain-then-flush, FINGERMOTION position-only update (#163),
# multiple motions -> single flush-move (catch-up invariant G), FINGERUP deferred
# release, held-finger single-dispatch-per-tick, ReleaseAllFingers atomicity (#162).
# Uses FN_TEST to expose pending-state accessors; SDL_INIT_EVENTS only (no GL/audio).
fn_add_game_test(test_input_tick_invariant test_input_tick_invariant.cpp)
target_compile_definitions(test_input_tick_invariant PRIVATE FN_TEST)
add_test(NAME input_tick_invariant COMMAND test_input_tick_invariant)
set_tests_properties(input_tick_invariant PROPERTIES TIMEOUT 30)

# ---- Standalone scene test harnesses (visual dev tools, NOT CTest tests) ----
# scene_fruit_scene.exe  -- single watermelon in a ballistic arc
# scene_slash_scene.exe  -- blade trail driven by mouse/touch drag
# Port specific: standalone entity test harness; excluded from asm-verify/symbol-diff.
if (NOT EMSCRIPTEN)
    add_subdirectory(scenes)
endif()

# GameModeScreen destructor UAF regression guard.
# Exercises the back-out (QuitCallback) and mode-pick (ClassicModeCallback)
# paths that trigger GameModeScreen removal. Under ASAN (build/asan) the pre-fix
# RemoveButtons()-in-dtor UAF is caught; after the fix the test runs clean.
fn_add_game_test(test_gamemode_destructor test_gamemode_destructor.cpp)
add_test(NAME gamemode_destructor COMMAND test_gamemode_destructor)
set_tests_properties(gamemode_destructor PROPERTIES TIMEOUT 60)

# Per-mode spawn lifecycle diagnostic. Logs every fruit/bomb spawn with
# full pos/vel/gravity parameters plus per-wave spawner config so
# divergences vs. binary expectations are readable line-by-line. Always
# returns success unless setup fails -- it's a log dump, not pass/fail.
fn_add_game_test(test_spawn_lifecycle test_spawn_lifecycle.cpp)
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

# Arcade spawn-stall regression guard (WaveManager slot-collapse bug fix).
# Reproduces the "1 fruit then nothing" arcade stall: drives Reset(true) +
# m_GameDt=0.0f (the STATE_GAME_START conditions that triggered UpdateComboSpeed
# to fill m_SpeedControl[0], permanently closing the UpdateWave spawn-pump gate).
# Must FAIL on the old [0]-collapse code and PASS after the [0]/[1] split fix.
# Asserts >= 3 wave transitions in 600 frames (10s of simulated gameplay).
fn_add_game_test(test_arcade_spawn test_arcade_spawn.cpp)
add_test(NAME arcade_spawn_real COMMAND test_arcade_spawn)
set_tests_properties(arcade_spawn_real PROPERTIES TIMEOUT 120)

# Wave-timeline diagnostic. Drives the full spawn pipeline for N frames and
# logs per-second spawn events (fruit type, bombs, super-fruit) with wave
# transitions. Returns 0 if >0 fruit spawned; 1 if the pipeline is broken.
# Seed is fixed (12345) for determinism. Modes: classic/arcade/zen/combo.
# Timeouts: classic/arcade 90s, zen 120s (mode game-length + 30s slack).
fn_add_game_test(test_wave_timeline test_wave_timeline.cpp)
add_test(NAME wave_timeline_classic COMMAND test_wave_timeline --mode=classic --frames=3600)
add_test(NAME wave_timeline_arcade  COMMAND test_wave_timeline --mode=arcade  --frames=3600)
add_test(NAME wave_timeline_zen     COMMAND test_wave_timeline --mode=zen     --frames=3600)
add_test(NAME wave_timeline_combo   COMMAND test_wave_timeline --mode=combo   --frames=3600)
set_tests_properties(wave_timeline_classic PROPERTIES TIMEOUT 90)
set_tests_properties(wave_timeline_arcade  PROPERTIES TIMEOUT 90)
set_tests_properties(wave_timeline_zen     PROPERTIES TIMEOUT 120)
set_tests_properties(wave_timeline_combo   PROPERTIES TIMEOUT 90)

# GlobalProbabilityOveride fire-path verification.
# Proves parse -> arm -> gate -> fire -> correct-type for the super_pomegranate
# GPO (PointBased, CLASSIC mode) deterministically, without accumulating real
# game score. Boots the full game (needed for FruitInfo / FruitType lookup),
# then bypasses the game loop entirely for gate testing.
# Cases: (1) parse 1 PointBased entry with super_pomegranate type,
#         (2) NewGameStarted seeds threshold in [40,70],
#         (3) score=0 < threshold -> no fire,
#         (4) score=110 >= threshold=55 -> fires and writes super_pomegranate type,
#         (5) re-arm: threshold advances, second call at same score -> no fire.
fn_add_game_test(test_global_probability test_global_probability.cpp)
add_test(NAME global_probability COMMAND test_global_probability)
set_tests_properties(global_probability PROPERTIES TIMEOUT 60)

# Golden-screenshot driver. Boots a named screen to its stable fully-drawn
# state and captures the framebuffer to tmp/test/screenshots/<screen>_screen.png.
# Each ctest entry runs with --screenshot so the PNG is written automatically.
fn_add_game_test(test_screenshot test_screenshot.cpp)
add_test(NAME screenshot_bonus COMMAND test_screenshot bonus --screenshot)
set_tests_properties(screenshot_bonus PROPERTIES TIMEOUT 60)
