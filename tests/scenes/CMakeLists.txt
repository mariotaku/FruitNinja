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

    # fn_link_game_engine_group (defined in the root CMakeLists.txt) wraps
    # fruit-ninja-game + mortar_engine in a GNU-ld --start-group/--end-group
    # on non-MSVC linkers -- see its definition for why the two static libs
    # are mutually dependent.
    fn_link_game_engine_group(${_target} PRIVATE)
    target_link_libraries(${_target} PRIVATE SDL2::SDL2main)

    # SDL2_image: test_harness.h uses IMG_SavePNG.
    if (TARGET SDL2_image::SDL2_image)
        target_link_libraries(${_target} PRIVATE SDL2_image::SDL2_image)
    endif()

    if (WIN32)
        target_link_libraries(${_target} PRIVATE opengl32)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(${_target} PRIVATE OpenGL::GL)
    endif()

    # Copy SDL2.dll next to the exe on Windows so it can be run in-place.
    if (WIN32 AND TARGET SDL2::SDL2)
        fn_copy_dll_dependency(${_target} SDL2::SDL2)
    endif()

    # Copy SDL2_image.dll (and any bundled format DLLs) next to the exe.
    if (WIN32 AND TARGET SDL2_image::SDL2_image)
        fn_copy_dll_dependency(${_target} SDL2_image::SDL2_image)

        # Also copy any sibling DLLs from the same directory (optional codec
        # DLLs such as libpng16-16.dll, libjpeg-9.dll, etc.) -- not part of the
        # shared helper since it's specific to this multi-DLL zip layout.
        set(_sdl2img_real SDL2_image::SDL2_image)
        get_target_property(_sdl2img_alias ${_sdl2img_real} ALIASED_TARGET)
        if (_sdl2img_alias)
            set(_sdl2img_real ${_sdl2img_alias})
        endif()
        get_target_property(_sdl2img_type ${_sdl2img_real} TYPE)
        if (_sdl2img_type STREQUAL "SHARED_LIBRARY")
            get_target_property(_sdl2img_loc ${_sdl2img_real} IMPORTED_LOCATION)
            get_filename_component(_sdl2img_dir "${_sdl2img_loc}" DIRECTORY)
            file(GLOB _sdl2img_extra_dlls "${_sdl2img_dir}/*.dll")
            foreach(_extra_dll IN LISTS _sdl2img_extra_dlls)
                add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_extra_dll}"
                        $<TARGET_FILE_DIR:${_target}>)
            endforeach()
        endif()
    endif()

    # task #125: register with the same FN_GL_CONTEXT_TEST_EXES global property
    # that tests/CMakeLists.txt's add_test() override reads, so the ctest
    # entries below (bomb_scale, fruit_splat, ...) automatically get the Mesa
    # llvmpipe env -- every scene test here renders, none opt out.
    set_property(GLOBAL APPEND PROPERTY FN_GL_CONTEXT_TEST_EXES ${_target})
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

# ---- scene_fruit_splat: splat spawn + draw regression test ----
# Slices a watermelon, asserts a SplatEntity spawns (alive in pool) and
# renders non-background pixels via DrawActiveSplats.
# Port specific: standalone splat spawn+draw regression test
add_scene_test(scene_fruit_splat scene_fruit_splat.cpp)
add_test(NAME fruit_splat COMMAND scene_fruit_splat)
set_tests_properties(fruit_splat PROPERTIES TIMEOUT 60)

# ---- scene_fruit_splat --critical: critical-flash colour-lerp capture ----
# Spawns a splat via MakeSplat with an out-of-range fruitType (matching the
# real critical-slice call site), lands it, then captures 3 PNGs at
# m_ColourPhase ~= 1.5 / 0.5 / 0.0 verifying the CRITICAL_COLOUR ->
# FruitTypeColour lerp. Screenshots: tmp/test/screenshots/crit_splat/*.png
add_test(NAME fruit_splat_critical COMMAND scene_fruit_splat --critical --screenshot)
set_tests_properties(fruit_splat_critical PROPERTIES TIMEOUT 60)

# ---- scene_slash_blade: blade-skin colour regression test ----
# Injects a synthetic swipe for each of: default, flame, shiny_red skins.
# Reads back the rendered framebuffer and asserts the trail is non-black.
# Headless assertion reproduces "flame blade renders black on web" on native.
# Port specific: standalone blade-skin diagnostic / regression scene
add_scene_test(scene_slash_blade_scene scene_slash_blade.cpp)
add_test(NAME slash_blade COMMAND scene_slash_blade_scene)
set_tests_properties(slash_blade PROPERTIES TIMEOUT 120)

# ---- scene_special_fruit: special fruit / powerup render test ----
# Renders each special fruit / powerup spawn object (arcade bananas, special
# score fruits, bomb) and writes a PNG per object to
# tmp/test/screenshots/special_fruit/<name>.png.
# Exposes white-solid (untextured) renders as white PNGs + WARN log lines.
# Currently frenzy banana is expected to WARN (banana_speed texture bug).
# Exits 1 only if any object produces 0 non-background pixels (total failure).
# Port specific: standalone special fruit / powerup render test
add_scene_test(scene_special_fruit scene_special_fruit.cpp)
add_test(NAME special_fruit COMMAND scene_special_fruit)
set_tests_properties(special_fruit PROPERTIES TIMEOUT 120)

# ---- scene_coin: Coin currency-sprite spawn/state/draw regression test ----
# Spawns 5 coins via the exact crit-slice/fruit-kill call convention
# (Coin::MakeCoins silent=true, matching Fruit::CollisionResponse), ticks
# through WAITING -> FLYING -> HOMING, and captures 3 PNGs:
#   tmp/test/screenshots/coin/just_spawned.png (state 0, no draw by design)
#   tmp/test/screenshots/coin/mid_flight.png   (state 2, asserts drawn)
#   tmp/test/screenshots/coin/homing.png       (state 4, asserts drawn)
# Port specific: standalone coin spawn/state/draw regression test
add_scene_test(scene_coin scene_coin.cpp)
add_test(NAME scene_coin COMMAND scene_coin)
set_tests_properties(scene_coin PROPERTIES TIMEOUT 60)

# ---- scene_jiblet: Jiblet (super-fruit finale chunk) spawn/fling/draw test ----
# Spawns 8 pomegranate jiblets via the exact SuperFruitControl::SpawnJibs
# @0x001bc748 radial-fan call convention (Jiblet::Init with the
# pomegranate_jiblet.mmd mesh), ticks the fling, and captures 2 PNGs:
#   tmp/test/screenshots/jiblet/just_spawned.png (all 8 at origin, asserts drawn)
#   tmp/test/screenshots/jiblet/mid_flight.png   (radial fan, asserts drawn)
# Port specific: standalone jiblet spawn/fling/draw regression test
add_scene_test(scene_jiblet scene_jiblet.cpp)
add_test(NAME scene_jiblet COMMAND scene_jiblet)
set_tests_properties(scene_jiblet PROPERTIES TIMEOUT 60)

# ---- scene_superfruit_finale: full super-fruit finale render/screenshot test ----
# Drives the REAL finale end-to-end (Fruit::CollisionResponse -> FruitWasSlicedEvent
# -> SuperFruitControl::SuperFruitSliced -> HUD::Update ticking, same call chain
# test_superfruit_finale_tick.cpp proves live) through the full GameTaskDraw
# pipeline (Game::runFrames), and captures 3 PNGs along the timeline:
#   tmp/test/screenshots/superfruit_finale/early.png (combo text 3-stop gradient)
#   tmp/test/screenshots/superfruit_finale/mid.png   (jiblets fanning + shockwave ring ramping)
#   tmp/test/screenshots/superfruit_finale/late.png  (near self-removal, text still visible)
# Port specific: standalone super-fruit finale visual capture test
add_scene_test(scene_superfruit_finale scene_superfruit_finale.cpp)
add_test(NAME scene_superfruit_finale COMMAND scene_superfruit_finale)
set_tests_properties(scene_superfruit_finale PROPERTIES TIMEOUT 60)
