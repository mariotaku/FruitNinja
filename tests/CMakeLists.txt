# tests/CMakeLists.txt -- minimal ctest registration.

# ---------------------------------------------------------------------------
# Unified test-executable output directory.
#
# ALL test executables (both the fn_add_game_test / add_scene_test targets
# below and the standalone add_executable ones further down, including
# tests/scenes/) land in ONE shared FLAT folder: ${CMAKE_BINARY_DIR}/tests --
# no per-config subdir. Build type is already separated by the build dir
# (build/host, build/web, build/asan, ...), so a Debug/ / Release/ subdir under
# tests/ would be redundant noise. This lets one copy of a software-GL DLL (see
# tests/README.md) sit next to every test exe.
#
# Multi-config generators (Visual Studio) otherwise append /$<CONFIG> per
# target -- and per-subdirectory CMAKE_CURRENT_BINARY_DIR defaults scatter
# targets further (tests/scenes/<Config>/ vs tests/<Config>/). Forcing every
# CMAKE_RUNTIME_OUTPUT_DIRECTORY_<CONFIG> to the SAME flat ${CMAKE_BINARY_DIR}/tests
# (no <Config> suffix) collapses all of that to one folder; the plain
# CMAKE_RUNTIME_OUTPUT_DIRECTORY covers single-config generators (Ninja/Makefiles
# ignore the _<CONFIG> forms). Since only one config is ever built per build
# dir, there is no Debug-vs-Release collision.
#
# Set BEFORE any add_executable/add_subdirectory call in this tree: as a
# normal (non-cache) variable it is inherited by add_subdirectory(scenes)
# below, and CMake uses it as each new target's default RUNTIME_OUTPUT_DIRECTORY
# property at add_executable time -- no per-target set_target_properties needed.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
foreach(_fn_test_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_fn_test_cfg}" _fn_test_cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_fn_test_cfg_upper} "${CMAKE_BINARY_DIR}/tests")
endforeach()

# ---------------------------------------------------------------------------
# GL-context ctest environment (task #125).
#
# Mesa's software-GL fallback needs three env vars (GALLIUM_DRIVER=llvmpipe,
# LIBGL_ALWAYS_SOFTWARE=1, MESA_GL_VERSION_OVERRIDE=2.1 -- see tests/README.md)
# or opengl32.dll (the DLL shim copied next to every test exe on Windows,
# see fn_copy_dll_dependency below) tries the real display adapter and can
# die with an unrecoverable native failure (observed: exit 0x80070057, no SEH
# exception -- indistinguishable from a code crash) whenever the display is
# asleep. ctest never set these, so running the suite depended on the CALLER
# having exported them first; forgetting produced a false "hundreds of tests
# failing" reading.
#
# Fix: wire ENVIRONMENT on the ctest entry automatically instead of relying on
# every add_test() call site to remember set_tests_properties(... ENVIRONMENT
# ...). fn_add_game_test() below records each GL-context executable target in
# the FN_GL_CONTEXT_TEST_EXES global property (pass NO_GL_CONTEXT as a 3rd arg
# to opt an executable out -- for the handful of targets that link
# fruit-ninja-game purely for symbol access and never call TestHarness::Init's
# SDL_GL_CreateContext, e.g. the SDL_INIT_EVENTS-only input tests); the
# add_test() override just below stamps ENVIRONMENT on every ctest entry whose
# COMMAND runs one of those executables -- so a call site literally cannot
# forget it. tests/scenes/CMakeLists.txt's add_scene_test reuses the same
# property.
#
# WIN32-only: the Mesa DLL shim only sits next to the exes on Windows: Linux/CI
# use system GL or their own software stack and must not be forced onto this
# path.
# ---------------------------------------------------------------------------
set(FN_GL_TEST_ENV "GALLIUM_DRIVER=llvmpipe;LIBGL_ALWAYS_SOFTWARE=1;MESA_GL_VERSION_OVERRIDE=2.1")
set_property(GLOBAL PROPERTY FN_GL_CONTEXT_TEST_EXES "")

# Overrides the built-in add_test(): CMake makes the previous definition of an
# overridden command available under an underscore-prefixed name (standard
# CMake idiom, used e.g. by Conan's generated toolchain files to hook
# find_package()). Every add_test(NAME <n> COMMAND <exe> ...) call in this
# directory (and tests/scenes/, which inherits this macro via
# add_subdirectory) is intercepted; if <exe> was registered as a GL-context
# executable, the env above gets attached to the new ctest entry.
macro(add_test)
    _add_test(${ARGN})
    if (WIN32)
        # ARGN can't be passed to list() directly inside a macro (it isn't a
        # normal variable -- only ${ARGN} substitution works); copy it first.
        set(_fn_gt_args ${ARGN})
        list(FIND _fn_gt_args "COMMAND" _fn_gt_cmd_idx)
        list(FIND _fn_gt_args "NAME" _fn_gt_name_idx)
        if (_fn_gt_cmd_idx GREATER -1 AND _fn_gt_name_idx GREATER -1)
            math(EXPR _fn_gt_exe_idx "${_fn_gt_cmd_idx} + 1")
            math(EXPR _fn_gt_name_val_idx "${_fn_gt_name_idx} + 1")
            list(GET _fn_gt_args ${_fn_gt_exe_idx} _fn_gt_exe)
            list(GET _fn_gt_args ${_fn_gt_name_val_idx} _fn_gt_test_name)
            get_property(_fn_gt_gl_exes GLOBAL PROPERTY FN_GL_CONTEXT_TEST_EXES)
            if (_fn_gt_exe IN_LIST _fn_gt_gl_exes)
                set_tests_properties(${_fn_gt_test_name} PROPERTIES ENVIRONMENT "${FN_GL_TEST_ENV}")
            endif()
        endif()
    endif()
endmacro()

# ---------------------------------------------------------------------------
# Helper macro: fn_add_game_test
#
# Registers an executable that links against fruit-ninja-game, SDL2::SDL2main,
# and the platform GL library. Also copies SDL2.dll next to the exe on Windows.
# This replaces the ~20 lines of boilerplate each test used to carry verbatim.
#
# Usage:
#   fn_add_game_test(<target> <source> [NO_GL_CONTEXT])
#
# NO_GL_CONTEXT opts the target out of the automatic Mesa-llvmpipe ENVIRONMENT
# wiring above -- only for targets that never call TestHarness::Init() /
# SDL_GL_CreateContext (grep this file for "no SDL"/"no GL"/"SDL_INIT_EVENTS
# only" comments to see which link fruit-ninja-game for symbol access alone).
#
# The caller then adds include dirs, ctest entries, and test properties as
# needed after the macro call -- the macro only wires the shared link/copy
# boilerplate (+ GL-context test registration above).
#
# opengl32 is linked on WIN32 only; on non-WIN32 OpenGL::GL is found and linked
# (except for test_screen which resolves glReadPixels at runtime -- that target
# is still registered via fn_add_game_test but callers can omit its GL assertion
# since the linker doesn't need it on non-WIN32 there either; the macro always
# links OpenGL::GL on non-WIN32 for consistency).
# ---------------------------------------------------------------------------
macro(fn_add_game_test _target _source)
    add_executable(${_target} ${_source} ${CMAKE_SOURCE_DIR}/tests/test_silence.cpp)

    target_include_directories(${_target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/tests
    )

    # Screenshot PNGs land directly under the test binary dir at
    # <CMAKE_CURRENT_BINARY_DIR>/screenshots/ (flat, not the old nested
    # tmp/test/screenshots). Ephemeral build artifacts, colocated with the
    # test exes. test_harness.h's ScreenshotPng/SavePng use this absolute
    # path when defined (see FN_TEST_SCREENSHOT_DIR fallback there).
    target_compile_definitions(${_target} PRIVATE
        FN_TEST_SCREENSHOT_DIR="${CMAKE_CURRENT_BINARY_DIR}/screenshots")

    # fn_link_game_engine_group (defined in the root CMakeLists.txt) wraps
    # fruit-ninja-game + mortar_engine in a GNU-ld --start-group/--end-group
    # on non-MSVC linkers -- see its definition for why the two static libs
    # are mutually dependent.
    fn_link_game_engine_group(${_target} PRIVATE)
    target_link_libraries(${_target} PRIVATE SDL2::SDL2main)

    # Tests load assets from FN_STAGING_DATA_DIR (FN_DATA_DIR_PATH points there),
    # so staging must have run before a test executes. Depend on it so building any
    # test target also stages (idempotent no-op once staged).
    if (TARGET fn_asset_staging)
        add_dependencies(${_target} fn_asset_staging)
    endif()

    # SDL2_image: used by test_harness.h for IMG_SavePNG.
    # Only linked to test targets, never to game/engine targets.
    if (TARGET SDL2_image::SDL2_image)
        target_link_libraries(${_target} PRIVATE SDL2_image::SDL2_image)
    endif()

    if (WIN32)
        target_link_libraries(${_target} PRIVATE opengl32)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(${_target} PRIVATE OpenGL::GL)
    endif()

    # Copy SDL2.dll next to the exe on Windows so the test can run in-place.
    if (WIN32 AND TARGET SDL2::SDL2)
        fn_copy_dll_dependency(${_target} SDL2::SDL2)
    endif()

    # Copy SDL2_image.dll (and any bundled format DLLs in the same dir) next to
    # the exe on Windows.  The MSVC prebuilt zip ships SDL2_image.dll in lib/x64;
    # some releases also include optional codec DLLs there.  copy_if_different
    # handles repeated POST_BUILD calls (no-op when already up-to-date).
    if (WIN32 AND TARGET SDL2_image::SDL2_image)
        fn_copy_dll_dependency(${_target} SDL2_image::SDL2_image)

        # Also copy any sibling DLLs from the same directory (optional codec
        # DLLs such as libpng16-16.dll, libjpeg-9.dll, etc.) -- not part of the
        # shared helper since it's specific to this multi-DLL zip layout.
        set(_fn_sdl2img_real SDL2_image::SDL2_image)
        get_target_property(_fn_sdl2img_alias ${_fn_sdl2img_real} ALIASED_TARGET)
        if (_fn_sdl2img_alias)
            set(_fn_sdl2img_real ${_fn_sdl2img_alias})
        endif()
        get_target_property(_fn_sdl2img_type ${_fn_sdl2img_real} TYPE)
        if (_fn_sdl2img_type STREQUAL "SHARED_LIBRARY")
            get_target_property(_fn_sdl2img_loc ${_fn_sdl2img_real} IMPORTED_LOCATION)
            get_filename_component(_fn_sdl2img_dir "${_fn_sdl2img_loc}" DIRECTORY)
            file(GLOB _fn_sdl2img_extra_dlls "${_fn_sdl2img_dir}/*.dll")
            foreach(_fn_extra_dll IN LISTS _fn_sdl2img_extra_dlls)
                add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_fn_extra_dll}"
                        $<TARGET_FILE_DIR:${_target}>)
            endforeach()
        endif()
    endif()

    # ARGN can't be used with IN_LIST directly inside a macro (only ${ARGN}
    # substitution works); copy it to a normal variable first.
    set(_fn_agt_extra_args ${ARGN})
    if (NOT "NO_GL_CONTEXT" IN_LIST _fn_agt_extra_args)
        set_property(GLOBAL APPEND PROPERTY FN_GL_CONTEXT_TEST_EXES ${_target})
    endif()
endmacro()

# ---------------------------------------------------------------------------
# Pure-logic tests (no SDL, no GL, no audio)
# ---------------------------------------------------------------------------

# UTF-8 text regression guard (#216 mojibake fix).
#
# Two layers:
#   1. Utf8StringIterator unit coverage (ASCII, 2/3/4-byte, mixed, malformed, Reset).
#   2. BakedStringBox recording guard: a stub FontCacheObjectTTF records every
#      codepoint GetGlyph() receives; asserts decoded codepoints not raw bytes.
#
# Built as a standalone target (no mortar_engine, no SDL, no GL, no FreeType):
#   - Compiles BakedStringBox.cpp, Utf8StringIterator.cpp, Utf8StringProxy.cpp,
#     Colour.cpp directly.
#   - The test file itself provides the recording FontCacheObjectTTF stub plus
#     no-op stubs for GL, MatrixManager, Renderer, FontInterface.
#   - __bada__ is defined by the test file so gl_compat.h gives stub GL types
#     (no system GL header needed).
# Cross-build safe: no lambdas, no auto, no range-for, no enum class.
add_executable(test_utf8_text
    test_utf8_text.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/BakedStringBox.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/Utf8StringIterator.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/Utf8StringProxy.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/math/Colour.cpp
)
target_include_directories(test_utf8_text PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src
)
# FN_GL_STUB activates the stub GL typedefs in gl_compat.h (same as __bada__
# but without the sizeof static_asserts that fire on x64 host builds).
target_compile_definitions(test_utf8_text PRIVATE FN_GL_STUB)
# No link against mortar_engine or fruit-ninja-game: the recording stub
# FontCacheObjectTTF and all GL/engine stubs are defined in the test file.
add_test(NAME utf8_text COMMAND test_utf8_text)

# FixedStepDriver unit test -- no SDL, no GL, no audio.
# Covers steady 60/144/30 Hz streams, 500ms spike clamping, alpha bounds.
# Header-only target: only needs the src/ include path.
add_executable(test_fixedstep test_fixedstep.cpp)
target_include_directories(test_fixedstep PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME fixedstep COMMAND test_fixedstep)

# AsciiString SSO correctness + GROUP A free-function coverage (strlen+1 bug regression
# guard + MakeLowerCase/MakeUpperCase/StringToLower/StringToUpper/FindSubstring/
# StartsWithWord/IsStringInDelimitedList/ParseFloats/CombineFilePaths/WildCardFit).
# FindSubstring lock: pins the degenerate always-0xFFFFFFFF binary behavior.
# Pure in-process: no GPU, no audio, no file I/O.
add_executable(test_asciistring test_asciistring.cpp)
target_link_libraries(test_asciistring PRIVATE mortar_engine)
target_include_directories(test_asciistring PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME asciistring COMMAND test_asciistring)
set_tests_properties(asciistring PROPERTIES TIMEOUT 60)

# Transition / easing curves -- GROUP B v1.6.1 @0x0014e8c4..@0x0014eaa0.
# Covers StraightTransition, SquareTransition, FullTransition, InAndOut,
# SinPulse, JumpyPulse, JumpySinPulse, SinInAndOut, SinTransition(helper).
# Pure CPU: no GPU, no audio, no file I/O.
add_executable(test_transition test_transition.cpp)
target_link_libraries(test_transition PRIVATE mortar_engine)
target_include_directories(test_transition PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME transition COMMAND test_transition)
set_tests_properties(transition PROPERTIES TIMEOUT 60)

# --- #228 engine low-level leaf coverage (pure-CPU, no GPU/audio/IO) ---
# StringHash: Jenkins lookup3 + case-fold; assert known binary DAT hash constants.
add_executable(test_stringhash test_stringhash.cpp)
target_link_libraries(test_stringhash PRIVATE mortar_engine)
target_include_directories(test_stringhash PRIVATE ${CMAKE_SOURCE_DIR}/src/engine)
add_test(NAME stringhash COMMAND test_stringhash)

# Batch 4 of task #195: FileStringHash / HashTypeConvert / EncodeUTF8FromUCS.
# FileStringHash: dsstofile normalization (backslash->slash, A-Z->a-z), == StringHash for lowercase input.
# HashTypeConvert: 7-entry type-hash table (fruit/bomb/coin/slash/blast/jiblet/fruitray).
# EncodeUTF8FromUCS: ASCII, 2-byte (U+00E9), 3-byte (U+4E2D) codepoints.
# Uses fn_add_game_test (fruit-ninja-game carries HashTypeConvert); no SDL_Init needed.
fn_add_game_test(test_math_utils test_math_utils.cpp NO_GL_CONTEXT)
add_test(NAME math_utils COMMAND test_math_utils)
set_tests_properties(math_utils PROPERTIES TIMEOUT 30)

# Vec3 / _Vector3<float> math (Dot/Magnitude/Normalise incl. zero/near-zero). Header-only.
add_executable(test_vec3 test_vec3.cpp)
target_include_directories(test_vec3 PRIVATE ${CMAKE_SOURCE_DIR}/src/engine)
add_test(NAME vec3 COMMAND test_vec3)

# Colour: BGRA channel order, PlatformColour packing, Tint, Lerp.
add_executable(test_colour test_colour.cpp)
target_link_libraries(test_colour PRIVATE mortar_engine)
target_include_directories(test_colour PRIVATE ${CMAKE_SOURCE_DIR}/src/engine)
add_test(NAME colour COMMAND test_colour)

# SmartPtr<T> / ReferenceCounter intrusive ref-count lifecycle. Header-only.
add_executable(test_smartptr test_smartptr.cpp)
target_include_directories(test_smartptr PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/engine)
add_test(NAME smartptr COMMAND test_smartptr)

# MemoryPool<T> Create/Pop/Push/Reset/Destroy ctor-dtor contract.
add_executable(test_memorypool test_memorypool.cpp)
target_link_libraries(test_memorypool PRIVATE mortar_engine)
target_include_directories(test_memorypool PRIVATE ${CMAKE_SOURCE_DIR}/src/engine)
add_test(NAME memorypool COMMAND test_memorypool)

# Matrix44: column-major identity/multiply/scale/translate/RotZ + transform-point.
add_executable(test_matrix44 test_matrix44.cpp)
target_link_libraries(test_matrix44 PRIVATE mortar_engine)
target_include_directories(test_matrix44 PRIVATE ${CMAKE_SOURCE_DIR}/src/engine ${CMAKE_SOURCE_DIR}/src)
add_test(NAME matrix44 COMMAND test_matrix44)

# Math::Random 64-bit LCG: determinism, range bound, RandF [0,range), distribution.
add_executable(test_random test_random.cpp)
target_link_libraries(test_random PRIVATE mortar_engine)
target_include_directories(test_random PRIVATE ${CMAKE_SOURCE_DIR}/src/engine)
add_test(NAME random COMMAND test_random)

# Collision: ColSphere/ColLine intersect predicates + penetration normal/depth.
add_executable(test_collision test_collision.cpp)
target_link_libraries(test_collision PRIVATE mortar_engine)
target_include_directories(test_collision PRIVATE ${CMAKE_SOURCE_DIR}/src/engine)
add_test(NAME collision COMMAND test_collision)

# DataReader / VectorDataReader byte-stream: Read clamp/EOF, ReadLE<T> byte order.
add_executable(test_datareader test_datareader.cpp)
target_link_libraries(test_datareader PRIVATE mortar_engine)
target_include_directories(test_datareader PRIVATE ${CMAKE_SOURCE_DIR}/src/engine ${CMAKE_SOURCE_DIR}/src)
add_test(NAME datareader COMMAND test_datareader)

# DataStreamReader unit test (task #195 Wave B).
# High-fan-in parser leaf: ReadBasicType/ReadRaw/Read(string)/MakeSubReader/
# operator>>(Immutable) against a hand-built LE byte buffer.
# Pure CPU: no GL, no SDL, no audio.
add_executable(test_datastreamreader test_datastreamreader.cpp)
target_link_libraries(test_datastreamreader PRIVATE mortar_engine)
target_include_directories(test_datastreamreader PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME datastreamreader COMMAND test_datastreamreader)
set_tests_properties(datastreamreader PROPERTIES TIMEOUT 30)

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

# Requires the (gitignored, not distributed -- Halfbrick copyright) raw Bada
# asset dump at FruitNinjaBada/Data; absent on a fresh public clone. Skip
# registration rather than failing when the user hasn't brought their own dump.
if (EXISTS "${CMAKE_SOURCE_DIR}/FruitNinjaBada/Data")
    add_executable(test_localisation test_localisation.cpp)
    # Links fruit-ninja-game (not just mortar_engine) because StringTableUtilLoadStringsTable
    # reads game_work.languageFlag (v1.6.1 @0x0014ca5c -- the binary's StringTableUtil* live in
    # the game code range and read game state), so the engine StringTable TU references game_work.
    # fn_link_game_engine_group (root CMakeLists.txt): GNU-ld --start-group
    # wrap for the fruit-ninja-game <-> mortar_engine mutual dependency.
    fn_link_game_engine_group(test_localisation PRIVATE)
    target_include_directories(test_localisation PRIVATE
        ${CMAKE_SOURCE_DIR}/src/engine
    )
    # Portable default data dir (was a hardcoded C:/Users/Mariotaku/... machine path).
    # Points at the raw Bada asset tree (not FN_STAGING_DATA_DIR) -- StringTable::Load
    # reads translations_*.str directly and doesn't need the WebP-transcoded staging copy.
    target_compile_definitions(test_localisation PRIVATE
        FN_BADA_DATA_DIR="${CMAKE_SOURCE_DIR}/FruitNinjaBada/Data")
    add_test(NAME localisation COMMAND test_localisation)
else()
    message(STATUS "test_localisation skipped: FruitNinjaBada/Data not present (bring your own game dump)")
endif()

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

# Binary chunk-stream reader + .fnt parse helper free functions (task #195).
# Covers ReadFloat/ReadVec3/ReadString/ReadChunkHash (v1.6.1: 0x00238250,
# 0x00238260, 0x002381e0, 0x0023819c) and Next_Word_Is/Get_Next_Value/
# Parse_Char/Parse_Kerning (v1.6.1: 0x0024bdf4, 0x0024bb54, 0x0024be44, 0x0024c0b0).
# Pure CPU: no GL, no SDL, no audio. Cross-build safe.
add_executable(test_stream_read test_stream_read.cpp)
target_link_libraries(test_stream_read PRIVATE mortar_engine)
target_include_directories(test_stream_read PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME stream_read COMMAND test_stream_read)
set_tests_properties(stream_read PROPERTIES TIMEOUT 30)

add_executable(test_texture_format test_texture_format.cpp)
target_link_libraries(test_texture_format PRIVATE mortar_engine)
target_include_directories(test_texture_format PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME texture_format COMMAND test_texture_format)

# TextureConverter utility functions: COUNT_BITS, MultiplyColorComponent,
# CorrectHalfTexel, GetConversionChannelRank.
# Pure CPU: no GPU, no SDL, no audio.
add_executable(test_texture_converter test_texture_converter.cpp)
target_link_libraries(test_texture_converter PRIVATE mortar_engine)
target_include_directories(test_texture_converter PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src
)
add_test(NAME texture_converter COMMAND test_texture_converter)

# GX texture tiler/encoder (tools/lib/gx_encoder.py) selftest -- pins the
# byte-exact "GXT1" tile layouts (RGBA8 AR/GB half split, RGB565/RGB5A3
# big-endian u16 tiles, RGB5A3 opaque-bit threshold, oob padding) that the
# Wii loader uploads verbatim (gl_funcsWii.cpp Wii_UploadTiledGX). High
# fan-in leaf: every Wii texture goes through these tilers at staging time.
# Pure Python stdlib -- no GPU, no game binary, no audio.
add_test(NAME gx_encoder COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tools/lib/gx_encoder.py --selftest)
set_tests_properties(gx_encoder PROPERTIES TIMEOUT 30)

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

# Mortar path-utility functions -- Tokenize, PathFromTokens, PathGetParent,
# PathNormalize, PathConcatenate, PathGetRelative. Pure in-process: no GPU/audio/IO.
# Guards the PathGetParent trailing-slash fix (ResourceLoader.cpp).
add_executable(test_path_utils test_path_utils.cpp)
target_link_libraries(test_path_utils PRIVATE mortar_engine)
target_include_directories(test_path_utils PRIVATE ${CMAKE_SOURCE_DIR}/src/engine)
add_test(NAME path_utils COMMAND test_path_utils)
set_tests_properties(path_utils PROPERTIES TIMEOUT 30)

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

# BakedStringBox::ComputeBaselineY vertical baseline formula pin.
# Pins the exact binary formula from v1.6.1 RebuildAlignments @0x00245c78
# for all three vertical-alignment cases (center-V single/multi, top, bottom).
# Critically pins the -1.5 coefficient in the single-line center-V case.
# Pure in-process: no GPU, no audio, no FreeType, no SDL.
# Cross-build safe: no lambdas, no auto, no range-for, no enum class.
add_executable(test_bakedstringbox_baseline
    test_bakedstringbox_baseline.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/BakedStringBox.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/FancyBakedString.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/BakedStringTTF.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/Utf8StringIterator.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/render/Utf8StringProxy.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/math/Colour.cpp
)
target_include_directories(test_bakedstringbox_baseline PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(test_bakedstringbox_baseline PRIVATE FN_GL_STUB)
add_test(NAME bakedstringbox_baseline COMMAND test_bakedstringbox_baseline)
set_tests_properties(bakedstringbox_baseline PROPERTIES TIMEOUT 30
    LABELS "category_rendering;component_bakedstringbox")

# MenuButton scratch-backdrop anchor regression guard.
# Pins GetAdjustedPos() = pos + Vec3(480,320,0)*m_HudScale for two cases:
#   1. quit-bomb: pos=(0,0,0), m_HudScale=(0.375,-0.3,0) -> (180,-96,0) NOT (0,0,0)
#   2. pos-anchored: pos=(16,-66,0), m_HudScale=(0,0,0) -> pos unchanged
# Binary refs: v1.6.1 MenuButton::Draw @0x0019c2e4, GetAdjustedPos @0x136c2c.
# Before the fix, Draw used raw pos for the scratch decal anchor; the quit-bomb
# carries its position in m_HudScale (pos=0), so the backdrop appeared at (0,0).
# Pure in-process: no GPU, no audio, no SDL. Cross-build safe.
add_executable(test_menubutton_anchor
    test_menubutton_anchor.cpp
    ${CMAKE_SOURCE_DIR}/src/hud/HUDControl.cpp
)
# No __bada__ — HUDControl.cpp compiles the same way fruit-ninja-game does.
# The test file provides a no-op Debug::Log stub to satisfy the linker without
# pulling in the SDL-bound src/debug/LoggerSDL.cpp.
target_link_libraries(test_menubutton_anchor PRIVATE mortar_engine)
target_include_directories(test_menubutton_anchor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME menubutton_anchor COMMAND test_menubutton_anchor)
set_tests_properties(menubutton_anchor PROPERTIES TIMEOUT 30)

# ScrollingMenu::UpdateRealtime f==1.0f equivalence guard (per-present shop
# scroll rate task). Pins the dt-scaled Phase 4 (integrate) / Phase 5
# (layout) / Phase 7 (spring-back) forms in UpdateRealtime(1/60) against
# hand-derived expected values matching the __bada__ per-tick literal forms
# (pv*=0.9; vel+=pv; spring*=0.75/0.25; snap+=0.1) -- see the phase-split
# comment above ScrollingMenu::Update in src/hud/ScrollingMenu.cpp.
#
# Links the full fruit-ninja-game lib (not a hand-picked source + stub set):
# ScrollingMenu's dependency graph (MenuBackground -> ItemManager ->
# ShopScreen/FruitSaveData/AchievementManager/SlashEntity, ...) is coupled
# enough that hand-picking sources + storage-only stubs for each missing
# symbol chases an ever-growing tail. The lib already builds and links
# cleanly for the fn_add_game_test targets below; the test itself still
# never touches GL/audio/SDL -- it only constructs a ScrollingMenu in-process
# and calls UpdateRealtime()/AddItem() directly, no SDL_Init/window/GL
# context. fruit-ninja-game contains no main() (that lives solely in the
# mainSDL.cpp TU, excluded from the lib -- see "Game library (everything
# except mainSDL.cpp)" above), so this test's own plain int main() is the
# only entry point, same as test_shopscreen.cpp etc.
add_executable(test_scrollingmenu_updaterealtime
    test_scrollingmenu_updaterealtime.cpp
)
# fn_link_game_engine_group (root CMakeLists.txt): GNU-ld --start-group
# wrap for the fruit-ninja-game <-> mortar_engine mutual dependency.
fn_link_game_engine_group(test_scrollingmenu_updaterealtime PRIVATE)
target_include_directories(test_scrollingmenu_updaterealtime PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME scrollingmenu_updaterealtime COMMAND test_scrollingmenu_updaterealtime)
set_tests_properties(scrollingmenu_updaterealtime PROPERTIES TIMEOUT 30)

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

# DojoScreen headless layout/structure guard. Verifies the faithful
# Init()->Reset()->CreateButtons() activation path creates all expected HUD
# controls: 3 ring buttons (back-bomb/shop/about) and 2 distinct-position
# BSButtons (FB+TW social stubs). Headless -- passes with ctest -E screenshot.
# Screenshot variants write tmp/test/screenshots/dojo/<lang>.png (need GPU).
fn_add_game_test(test_dojoscreen test_dojoscreen.cpp)
add_test(NAME dojoscreen COMMAND test_dojoscreen)
set_tests_properties(dojoscreen PROPERTIES TIMEOUT 60)
add_test(NAME screenshot_dojo_english COMMAND test_dojoscreen --screenshot --lang=english_us)
add_test(NAME screenshot_dojo_chinese COMMAND test_dojoscreen --screenshot --lang=chinese)
set_tests_properties(screenshot_dojo_english PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_dojo;lang_english_us")
set_tests_properties(screenshot_dojo_chinese PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_dojo;lang_chinese")

# DojoScreen bomb-scale regression test. Drives the REAL DojoScreen activation
# flow (Init()->Reset()->CreateButtons()) and asserts the back-bomb ends up
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

# BONUS SCREEN -> GAME OVER transition crash repro (task #40). Boots Arcade,
# activates the x2 (score_mult) + freeze powerups so their ScreenEffect HUD
# controls (ScoreMultiplyerBoard / TimeSinkControl) are live, then ticks the
# full game loop through the bonus finale + STATE_MAIN_DISPLAY handoff while the
# powerups expire and their boards linger in the HUD. Meant to be run under
# ASAN to catch the wasm "index out of bounds" (stale-vtable/UAF) natively with
# a symbolized stack. PASSES when the flow completes without a crash/stall.
fn_add_game_test(test_bonus_crash test_bonus_crash.cpp)
add_test(NAME bonus_crash COMMAND test_bonus_crash)
set_tests_properties(bonus_crash PROPERTIES TIMEOUT 180)

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
fn_add_game_test(test_slash_reset test_slash_reset.cpp NO_GL_CONTEXT)
add_test(NAME slash_reset COMMAND test_slash_reset)
set_tests_properties(slash_reset PROPERTIES TIMEOUT 30)

# SlashEntity::CollideWithSphere blade-vs-sphere segment-clamp regression guard.
# Covers three cases:
#   (1) CROSSING HIT  -- ribbon through sphere center; segment-closest=center -> HIT
#   (2) BLANK MISS    -- far-corner stroke; segment-clamped closest >> radius -> MISS
#                        (pre-#306 infinite-line over-trigger would still miss here, but
#                        segment-reversal regressions are caught by the clamp-to-1 path)
#   (3) NEAR-BOUNDARY -- closest just outside radius -> MISS; just inside -> HIT
# Pure buffer-content check: no SDL init, no GL, no audio -- follows test_slash_reset.cpp
# bootstrap pattern (fn_add_game_test, SlashEntity::Init only).
fn_add_game_test(test_slash_collision test_slash_collision.cpp NO_GL_CONTEXT)
add_test(NAME slash_collision COMMAND test_slash_collision)
set_tests_properties(slash_collision PROPERTIES TIMEOUT 30)

# CheckCombo (v1.6.1 @0x001320b4) classification pins -- guards the alternating
# gate polarity (genuine ABAB -> 0x18; 2-fruit two-type slice -> NOT 0x18) and
# the slot-COUNT (not fruit-type) semantics of the 0x14 (5 fruit, 2 types, 3+2)
# and 0x15 (5 fruit, 3 types, one pair) branches, plus 0x16/0x17/0x04/fallback
# and the dominant-type out param. High-fan-in scoring leaf: feeds the game-over
# fact board and persisted save data. Pure logic: no SDL init, no GL, no audio.
fn_add_game_test(test_slash_combo test_slash_combo.cpp NO_GL_CONTEXT)
add_test(NAME slash_combo COMMAND test_slash_combo)
set_tests_properties(slash_combo PROPERTIES TIMEOUT 30)

# InputTranslatorSDL drain/dispatch split invariant (#173 bridge-to-origin fix).
# Verifies that DrainSDLEvent() accumulates pending state WITHOUT dispatching
# and DispatchForSimTick() consumes pending state. Pins the invariant that
# m_PointCount (AddPoint) only advances inside a tick that also runs UpdatePoints.
# Covers: FINGERDOWN drain-then-flush, FINGERMOTION position-only update (#163),
# multiple motions -> single flush-move (catch-up invariant G), FINGERUP deferred
# release, held-finger single-dispatch-per-tick, ReleaseAllFingers atomicity (#162).
# Uses FN_TEST to expose pending-state accessors; SDL_INIT_EVENTS only (no GL/audio).
fn_add_game_test(test_input_tick_invariant test_input_tick_invariant.cpp NO_GL_CONTEXT)
target_compile_definitions(test_input_tick_invariant PRIVATE FN_TEST)
add_test(NAME input_tick_invariant COMMAND test_input_tick_invariant)
set_tests_properties(input_tick_invariant PROPERTIES TIMEOUT 30)

# Per-present (native 120Hz) UI SCROLL finger-tracking regression guard (task
# #13). Pins Mortar::Touch::RefreshLivePos()/GetLivePos(): sub-tick
# FINGERMOTIONs queued without a DispatchForSimTick leave states1[slot].currY
# stale, RefreshLivePos+GetLivePos surfaces the newest queued ring sample, the
# ring's m_eventHead/m_eventTail are unchanged by the scan (read-only -- never
# races/desyncs the 60Hz sim-tick drain), and a following DispatchForSimTick
# still applies every queued sample in order (determinism). Same harness
# pattern as test_input_tick_invariant (real InputTranslatorSDL + real Touch,
# SDL_INIT_EVENTS only -- no GL/audio).
fn_add_game_test(test_realtime_drag_livepos test_realtime_drag_livepos.cpp NO_GL_CONTEXT)
target_compile_definitions(test_realtime_drag_livepos PRIVATE FN_TEST)
add_test(NAME realtime_drag_livepos COMMAND test_realtime_drag_livepos)
set_tests_properties(realtime_drag_livepos PROPERTIES TIMEOUT 30)

# Press-vs-motion blade gate regression guard (tap-bridge fix).
# Drives the REAL chain SDL events -> InputTranslatorSDL -> InputManager ->
# SlashEntity with synthetic event streams (SDL_INIT_EVENTS only; no GL, no
# window, no audio). Pins:
#   * a stationary TAP emits TouchScreen+TouchDown but NO TouchMove;
#   * 3 discrete taps never bridge into a slash -- with m_BombHitEdge both
#     clear (per-press Reset re-seeds a degenerate stroke) and latched
#     (post-bomb game-over: point count must not change at all);
#   * a SWIPE still tracks (points appended per motion tick) and starts at
#     the press position, not the previous stroke's stale position;
#   * a same-tick DOWN+MOTION fast flick still registers on the press frame.
# MUST FAIL on the old curr!=prev press-frame TouchMove heuristic.
# FN_TEST exposes InputTranslatorSDL state + the SlashEntity bomb-latch seam.
fn_add_game_test(test_slash_input test_slash_input.cpp NO_GL_CONTEXT)
target_compile_definitions(test_slash_input PRIVATE FN_TEST)
add_test(NAME slash_input COMMAND test_slash_input)
set_tests_properties(slash_input PROPERTIES TIMEOUT 30)

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

# MainScreen -> GameModeScreen transition ASAN driver ("slicing NEW GAME").
# Fires the REAL MenuButton::Update slice gate on the NEW GAME ring's tracked
# fruit (m_bSliced + diverging vel/m_SecondVel), then steps the frame loop one
# frame at a time through GameModeCallback -> STATE_MODE_SELECT ->
# GameModeScreen construction + HUD insertion, so the transition's crash
# window (a WEB/wasm32 UAF the native host build otherwise tolerates
# silently) is exercised under ASAN (build/asan) on every frame.
fn_add_game_test(test_mainscreen_to_gamemode test_mainscreen_to_gamemode.cpp)
add_test(NAME mainscreen_to_gamemode COMMAND test_mainscreen_to_gamemode)
set_tests_properties(mainscreen_to_gamemode PROPERTIES TIMEOUT 60)

# Arcade-round-then-GameModeScreen ASAN driver -- reproduces a UAF on the
# SHARED red_ring texture (game_work.m_RingTex[16]) that only surfaces on the
# SECOND GameModeScreen::CreateControls call (MainScreen -> GameModeScreen ->
# Arcade -> GameOver -> menu -> GameModeScreen again). A fresh-boot single
# transition (test_mainscreen_to_gamemode) does not exercise the prior
# release/reacquire cycle on m_RingTex[16] across MainScreen/GameModeScreen/
# GameOverScreen, so it passes even when this over-release bug is present.
fn_add_game_test(test_arcade_then_gamemode test_arcade_then_gamemode.cpp)
add_test(NAME arcade_then_gamemode COMMAND test_arcade_then_gamemode)
set_tests_properties(arcade_then_gamemode PROPERTIES TIMEOUT 180)

# Ring-texture refcount imbalance ASAN driver -- round-trips GameModeScreen
# creation/teardown 6x and asserts game_work.m_RingTex[16] (red_ring) /
# m_RingTex[13] (arcade ring) SmartPtr<Texture> refcounts return to baseline
# after EVERY round trip, then confirms both survive into a DojoScreen entry
# (DojoScreen::CreateButtons also assigns m_RingTex[16] to its own back
# button -- the exact site a drifting refcount eventually crashes). A single
# transition (test_mainscreen_to_gamemode) cannot catch a slow per-visit
# drift; only repetition does.
fn_add_game_test(test_ring_texture_lifecycle test_ring_texture_lifecycle.cpp)
add_test(NAME ring_texture_lifecycle COMMAND test_ring_texture_lifecycle)
set_tests_properties(ring_texture_lifecycle PROPERTIES TIMEOUT 240)

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
# m_PauseAmount=0.0f (the STATE_GAME_START conditions that triggered UpdateComboSpeed
# to fill m_SpeedControl[0], permanently closing the UpdateWave spawn-pump gate).
# Must FAIL on the old [0]-collapse code and PASS after the [0]/[1] split fix.
# Asserts >= 3 wave transitions in 600 frames (10s of simulated gameplay).
fn_add_game_test(test_arcade_spawn test_arcade_spawn.cpp)
add_test(NAME arcade_spawn_real COMMAND test_arcade_spawn)
set_tests_properties(arcade_spawn_real PROPERTIES TIMEOUT 120)

# Wave-timeline diagnostic. Drives the full spawn pipeline for N frames and
# logs per-second spawn events (fruit type, bombs, super-fruit) with wave
# transitions. Modes: classic/arcade/zen/combo.
# NOT deterministic, and asserts nothing about the spawn content: the test's
# wm->m_Random.Seed(12345) call is immediately overwritten by the following
# PrepareForLevelStart -> WaveManager::Reset, which reseeds from the
# wall-clock-seeded global (WaveManager.cpp: m_Random.Seed(Math::g_Random.Rand32(0))).
# The per-second spawn log goes to stdout and is compared against nothing.
# The ONLY assertion is pipeline liveness: returns 1 if setup failed or if
# zero fruit spawned across the whole run, 0 otherwise.
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

# Screenshot CAPTURE driver -- capture-only, NOT a golden comparison. Boots a
# named screen to its stable fully-drawn state and writes the framebuffer to
# tmp/test/screenshots/<screen>_screen.png. Each ctest entry runs with
# --screenshot so the PNG is written automatically.
# TestHarness::ScreenshotPng does glReadPixels -> IMG_SavePNG -> return true:
# no golden is loaded, nothing is diffed, no tolerance is applied. So every
# screenshot_* case here passes whenever boot succeeds and its fixture's
# pointer checks hold -- "screenshot_* green" means "the game booted and drew
# something", NOT "the image matches a reference". The PNGs exist to be
# eyeballed by a human, which is deliberate and useful; just don't read them
# as visual regression coverage.
# The ONLY real golden comparison in this suite is tests/golden/renderer/
# (8 committed PNGs, tests/test_renderer.cpp, per-pixel tolerance compare) --
# a 64x64 FBO of 2D primitives with no game init and no RNG involved.
# (The scene_* tests are a separate thing again: they DO assert on real
# outcomes -- e.g. scene_coin asserts a 6-particle coin_collect burst,
# scene_fruit_splat asserts drawn pixels -- so they are not capture-only.)
fn_add_game_test(test_screenshot test_screenshot.cpp)
add_test(NAME screenshot_bonus    COMMAND test_screenshot bonus    --screenshot)
add_test(NAME screenshot_gameover COMMAND test_screenshot gameover --screenshot)
set_tests_properties(screenshot_bonus    PROPERTIES TIMEOUT 60)
set_tests_properties(screenshot_gameover PROPERTIES TIMEOUT 60)

# --- BonusScreen content x language matrix (task #32) ---
# Reproduce all of them with:   ctest -R screenshot_bonus   (or slice via -L)
# Each writes tmp/test/screenshots/bonus/<content>_<lang>.png. Content keys
# (curated award count / total-score magnitude, see RunBonus in
# test_screenshot.cpp for exact award data):
#   one   -- 1 award (gold, natural total 50)
#   two   -- 2 awards (gold+red, natural total 105)
#   three -- 3 awards (gold+red+blue, natural total 155)
#   big   -- 3 awards, TOTAL number forced to a 6-digit value (123456) to
#            exercise the size-scale formula's worst case and bottom-band fit
# The plain `screenshot_bonus` test above (no --content) is untouched and
# keeps producing bonus/default byte-for-byte for back-compat.
foreach(_bonus_content one two three big)
    foreach(_bonus_lang english_us chinese japanese korean arabic)
        add_test(NAME screenshot_bonus_${_bonus_content}_${_bonus_lang}
                 COMMAND test_screenshot bonus --screenshot
                         --content=${_bonus_content} --lang=${_bonus_lang})
        set_tests_properties(screenshot_bonus_${_bonus_content}_${_bonus_lang} PROPERTIES TIMEOUT 60
            LABELS "category_rendering;component_bonusscreen;lang_${_bonus_lang}")
    endforeach()
endforeach()

# Per-mode FruitFact board screenshot driver.
# Drives a per-mode GameOverScreen to STATE_MAIN_DISPLAY so the corresponding
# FruitFactPage is created and rendered. Outputs (every filename always carries
# a _3x2/_16x9 aspect suffix -- see test_fruitfact.cpp's --widescreen comment):
#   tmp/test/screenshots/fruitfact/classic_3x2.png (+ classic_16x9.png)
#   tmp/test/screenshots/fruitfact/arcade_3x2.png  (+ arcade_16x9.png)
#   tmp/test/screenshots/fruitfact/zen_3x2.png     (+ zen_16x9.png)
fn_add_game_test(test_fruitfact test_fruitfact.cpp)
# FN_TEST exposes BaseScreen::GetHUDControlsForTest() for the --isolated render
# (keep-set of FruitFact page child controls). Layout-neutral (method-only guard).
target_compile_definitions(test_fruitfact PRIVATE FN_TEST)
# Base per-mode tests (default English locale, default fact).
# LABELS let you slice the suite, e.g.:
#   ctest -L screen_fruitfact          # every fruitfact render test
#   ctest -L lang_chinese              # every Chinese-locale render test
#   ctest -L mode_zen -L lang_korean   # intersection (zen AND korean)
add_test(NAME screenshot_fruitfact_classic COMMAND test_fruitfact classic --screenshot)
add_test(NAME screenshot_fruitfact_arcade  COMMAND test_fruitfact arcade  --screenshot)
add_test(NAME screenshot_fruitfact_zen     COMMAND test_fruitfact zen     --screenshot)
set_tests_properties(screenshot_fruitfact_classic PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_fruitfact;mode_classic;fact_default;lang_english_us")
set_tests_properties(screenshot_fruitfact_arcade  PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_fruitfact;mode_arcade;fact_default;lang_english_us")
set_tests_properties(screenshot_fruitfact_zen     PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_fruitfact;mode_zen;fact_default;lang_english_us")

# --- Widescreen (3:2 vs 16:9) game-over/fact-board layout comparison ---
# See test_fruitfact.cpp's --widescreen comment (mirrors test_widescreen_render.cpp
# segment 2's widescreen-aware component loop). Every base test above already
# writes a "_3x2" file (WideSuffix() defaults false); these add the "_16x9"
# counterpart so the highscore-number/board layout can be diffed side by side:
#   tmp/test/screenshots/fruitfact/classic_3x2.png / classic_16x9.png (etc.)
foreach(_ff_mode classic arcade zen)
    add_test(NAME screenshot_fruitfact_${_ff_mode}_widescreen
             COMMAND test_fruitfact ${_ff_mode} --screenshot --widescreen)
    set_tests_properties(screenshot_fruitfact_${_ff_mode}_widescreen PROPERTIES TIMEOUT 60
        LABELS "category_rendering;screen_fruitfact;mode_${_ff_mode};fact_default;lang_english_us;widescreen")
endforeach()

# --- Mode x language x fact matrix (i18n + per-fruit fact-colour render checks) ---
# Reproduce all of them with:   ctest -R fruitfact   (or slice via -L, see above)
# Each writes tmp/test/screenshots/fruitfact/<mode>_<langsuffix>[_<fact>].png
# (CLion runs from build/host/tests, so the PNGs land under build/host/tests/tmp/).
# These render real GL, so they need a display/GPU (skipped naturally in pure-headless CI).
foreach(_ff_mode classic arcade zen)
    foreach(_ff_lang english_us chinese japanese korean)
        add_test(NAME screenshot_fruitfact_${_ff_mode}_${_ff_lang}
                 COMMAND test_fruitfact ${_ff_mode} --screenshot --lang=${_ff_lang})
        set_tests_properties(screenshot_fruitfact_${_ff_mode}_${_ff_lang} PROPERTIES TIMEOUT 60
            LABELS "category_rendering;screen_fruitfact;mode_${_ff_mode};fact_default;lang_${_ff_lang}")
    endforeach()
endforeach()
# Per-fruit fact selection (fact colour varies by fruit): strawberry=red (--fact=4).
add_test(NAME screenshot_fruitfact_classic_strawberry
         COMMAND test_fruitfact classic --screenshot --fact=4)
add_test(NAME screenshot_fruitfact_zen_chinese_strawberry
         COMMAND test_fruitfact zen --screenshot --lang=chinese --fact=4)
set_tests_properties(screenshot_fruitfact_classic_strawberry PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_fruitfact;mode_classic;fact_strawberry;lang_english_us")
set_tests_properties(screenshot_fruitfact_zen_chinese_strawberry PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_fruitfact;mode_zen;fact_strawberry;lang_chinese")

# IngamePopup badge alignment screenshot test.
# Renders the NEW badge (type 0x10, gold) and SELECTED badge (type 0x11, green)
# in isolation on a dark background at their real in-game draw scale (NEW 0.8,
# SELECTED 0.5) so border-vs-text alignment matches what the game shows.
# Output PNGs per run:
#   tmp/test/screenshots/badges/new.png
#   tmp/test/screenshots/badges/selected.png
fn_add_game_test(test_badges test_badges.cpp)
add_test(NAME screenshot_badges COMMAND test_badges --screenshot)
set_tests_properties(screenshot_badges PROPERTIES TIMEOUT 60)

# CheckBox render test. Dead code in v1.6.1 but a complete real implementation.
# Renders the checkbox unchecked + checked, injecting in-memory substitute
# textures (the faithful checked/unchecked.tex art is not shipped -- see the
# widget .cpp DIFFERS notes). Validates geometry + checked state, not art.
# Output: tmp/test/screenshots/checkbox/checkbox.png.
fn_add_game_test(test_checkbox_render test_checkbox_render.cpp)
add_test(NAME checkbox_render COMMAND test_checkbox_render)
add_test(NAME screenshot_checkbox COMMAND test_checkbox_render --screenshot)
set_tests_properties(checkbox_render PROPERTIES TIMEOUT 60)
set_tests_properties(screenshot_checkbox PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_checkbox")

# UiCheckbox render test. Port-only widget toolkit (src/ui/) -- NO binary
# counterpart. Renders unchecked + checked using the REAL staged box.tex
# (NineSlice background) + check.tex (tick glyph) textures. No procedural
# fallback -- fails if fn_asset_staging hasn't generated them.
# Output: tmp/test/screenshots/ui_checkbox/ui_checkbox.png.
fn_add_game_test(test_ui_checkbox_render test_ui_checkbox_render.cpp)
add_test(NAME ui_checkbox_render COMMAND test_ui_checkbox_render)
add_test(NAME screenshot_ui_checkbox COMMAND test_ui_checkbox_render --screenshot)
set_tests_properties(ui_checkbox_render PROPERTIES TIMEOUT 60)
set_tests_properties(screenshot_ui_checkbox PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_ui_checkbox")

# UiSlider render test. Port-only widget toolkit (src/ui/) -- NO binary
# counterpart. Renders three sliders (value at min/mid/max, track 120x16)
# using the REAL staged box.tex (NineSlice track) + slider_will.tex (knob)
# textures. No procedural fallback -- fails if fn_asset_staging hasn't
# generated them.
# Output: tmp/test/screenshots/ui_slider/ui_slider.png.
fn_add_game_test(test_ui_slider_render test_ui_slider_render.cpp)
add_test(NAME ui_slider_render COMMAND test_ui_slider_render)
add_test(NAME screenshot_ui_slider COMMAND test_ui_slider_render --screenshot)
set_tests_properties(ui_slider_render PROPERTIES TIMEOUT 60)
set_tests_properties(screenshot_ui_slider PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_ui_slider")

# UiDropdown render test. Port-only widget toolkit (src/ui/) -- NO binary
# counterpart. Replaces the dead ComboBox+ListBox+VerticalScroller stack with
# a single self-contained collapsed-bar + in-place scrollable panel widget.
# Renders a collapsed dropdown and an open dropdown (scrolled + hovered) using
# REAL staged box.tex + caret texture. No procedural fallback.
# Output: tmp/test/screenshots/ui_dropdown/ui_dropdown.png.
fn_add_game_test(test_ui_dropdown_render test_ui_dropdown_render.cpp)
add_test(NAME ui_dropdown_render COMMAND test_ui_dropdown_render)
add_test(NAME screenshot_ui_dropdown COMMAND test_ui_dropdown_render --screenshot)
set_tests_properties(ui_dropdown_render PROPERTIES TIMEOUT 60)
set_tests_properties(screenshot_ui_dropdown PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_ui_dropdown")

# SliderControl render test. Dead code in v1.6.1 but a complete real
# implementation. Renders the thumb at min/mid/max, injecting in-memory substitute
# textures (the faithful slider_will.tex art is not shipped -- see the widget .cpp
# DIFFERS notes). Validates geometry + value-driven thumb position, not art.
# Output: tmp/test/screenshots/slider/slider.png.
fn_add_game_test(test_slider_render test_slider_render.cpp)
add_test(NAME slider_render COMMAND test_slider_render)
add_test(NAME screenshot_slider COMMAND test_slider_render --screenshot)
set_tests_properties(slider_render PROPERTIES TIMEOUT 60)
set_tests_properties(screenshot_slider PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_slidercontrol")

# Dropdown widget stack (ComboBox + ListBox + VerticalScroller) render test. All
# three are dead code in v1.6.1 but carry complete real implementations. Renders a
# collapsed ComboBox and an expanded ListBox (8 items > 6 visible rows -> its ctor
# creates the VerticalScroller) with hover + selected row colours and the scroller
# thumb at a mid position. Injects in-memory placeholder textures (the dropdown art
# is not shipped -- see the widget .cpp DIFFERS notes). Validates geometry + the
# ComboBox->ListBox->VerticalScroller composition wiring, not art.
# Output: tmp/test/screenshots/dropdown/dropdown.png.
fn_add_game_test(test_dropdown_render test_dropdown_render.cpp)
add_test(NAME dropdown_render COMMAND test_dropdown_render)
add_test(NAME screenshot_dropdown COMMAND test_dropdown_render --screenshot)
set_tests_properties(dropdown_render PROPERTIES TIMEOUT 60)
set_tests_properties(screenshot_dropdown PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_combobox;component_listbox;component_verticalscroller")

# SettingsScreen render test (port-improvement in-game settings modal). The
# screen AddControl's its src/ui/ widgets (UiDropdown/UiCheckbox/UiSlider) plus
# its close BSButton to game_work.mHud at HUD_LAYER_TOP_MOST; the test drives
# HUD::Update/Draw (component-isolation mode) rather than the screen's own
# Draw() directly. Captures both the collapsed panel and the LANGUAGE dropdown
# forced open (draw-order overlay + m_Active input gate). No binary
# counterpart; validates construction + draw, not a faithful layout.
# Output: tmp/test/screenshots/settings_screen/{settings,settings_expanded}.png.
fn_add_game_test(test_settings_screen_render test_settings_screen_render.cpp)
add_test(NAME settings_screen_render COMMAND test_settings_screen_render)
add_test(NAME screenshot_settings_screen COMMAND test_settings_screen_render --screenshot)
set_tests_properties(settings_screen_render PROPERTIES TIMEOUT 60)
set_tests_properties(screenshot_settings_screen PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_settingsscreen")

# Interactive settings-widget dev harness. Opens a REAL clickable window laying
# out the ported settings widgets (CheckBox / SliderControl / ComboBox stack)
# each bound to a live app parameter (m_bSoundOn / m_bMusicOn / g_MotionMode /
# g_MotionSpeedThreshold / SFX+Music volume / languageFlag). Toggle/drag/select
# and the bound parameter changes (per-change stdout log + on-screen readout).
# Manual harness -- default is INTERACTIVE (window shown; ESC/close quits). The
# registered ctest runs --headless as a cheap compile/run smoke (no assertions);
# launch the interactive window via CLion / `test_settings_interactive`.
fn_add_game_test(test_settings_interactive test_settings_interactive.cpp)
add_test(NAME settings_interactive_smoke COMMAND test_settings_interactive --headless --frames 20)
set_tests_properties(settings_interactive_smoke PROPERTIES TIMEOUT 60)

# Achievement-unlock popup screenshot test (NotificationControl, Type_Named).
# There is no achievements-list screen in v1.6.1 (AchievementsScreen is a
# defunct stub) -- this is the only on-screen achievement "drawing": the
# slide-in unlock popup that AchievementManager::UnlockedAchievement
# constructs. Renders a real AchievementInfo entry pulled from
# AchievementManager::m_All, settled in its resting position.
# Output: tmp/test/screenshots/achievement/unlock.png.
fn_add_game_test(test_achievement_notification test_achievement_notification.cpp)
add_test(NAME screenshot_achievement COMMAND test_achievement_notification --screenshot)
set_tests_properties(screenshot_achievement PROPERTIES TIMEOUT 60 LABELS "category_rendering;component_notificationcontrol")

# --- Achievement x language matrix (i18n render check) ---
# Reproduce all of them with:   ctest -R screenshot_achievement   (or slice via -L)
# Each writes tmp/test/screenshots/achievement/<achievement>_<lang>.png
# Curated 5 achievements (from FruitNinjaBada/Data/xml/achievementlist.xml), one
# per interesting case:
#   BAMBOO_BLADE  -- item unlock, no icon texture (name key ACHIEVEMENT_NEWBLADE)
#   DISCO_SLASH   -- item unlock WITH a working icon (item_discoblade; name key ACHIEVEMENT_NEWBLADE)
#   background9   -- background unlock (name key ACHIEVEMENT_NEWBACKGROUND, distinct text)
#   261524        -- numeric id, name key ACHIEVEMENT_03 (>= 03 resolves in string tables)
#   350264        -- numeric id, name key ACHIEVEMENT_19 (>= 03 resolves in string tables)
foreach(_ach_id BAMBOO_BLADE DISCO_SLASH background9 261524 350264)
    foreach(_ach_lang english_us french german russian japanese korean chinese arabic)
        add_test(NAME screenshot_achievement_${_ach_id}_${_ach_lang}
                 COMMAND test_achievement_notification --screenshot --lang=${_ach_lang} --achievement=${_ach_id})
        set_tests_properties(screenshot_achievement_${_ach_id}_${_ach_lang} PROPERTIES TIMEOUT 60
            LABELS "category_rendering;component_notificationcontrol;lang_${_ach_lang}")
    endforeach()
endforeach()

# Arcade powerup HUD (PowerUpManager) screenshot test. Renders the FROZEN /
# FRENZY / x2 banana powerups (meter bars + overlay images) in isolation for the
# 7 non-empty combinations of 1..3 active powers. Each combo activates the
# matching poweruplist.xml powers, settles them in their steady window, then
# captures. NOTE: FRENZY (speed) has no persistent overlay image -- only its
# meter bar renders. Bars overlap for multi-power combos (faithful layout; see
# the fidelity note in test_powerup_hud.cpp).
# Reproduce with:   ctest -R screenshot_powerup   (or slice via -L component_powerup_hud)
# Each writes tmp/test/screenshots/powerup/<combo>.png
fn_add_game_test(test_powerup_hud test_powerup_hud.cpp)
foreach(_pu_combo freeze frenzy x2 freeze+frenzy freeze+x2 frenzy+x2 all3)
    add_test(NAME screenshot_powerup_${_pu_combo}
             COMMAND test_powerup_hud --screenshot --combo=${_pu_combo})
    set_tests_properties(screenshot_powerup_${_pu_combo} PROPERTIES TIMEOUT 60
        LABELS "category_rendering;component_powerup_hud")
endforeach()

# Arcade "60 SECONDS ... GO!!" start intro (ready_set_go automatic power,
# ScreenEffect-driven arcade_60seconds/arcade_go images). See --combo=intro
# doc comment in test_powerup_hud.cpp for the window-timing derivation.
# Writes tmp/test/screenshots/powerup/intro_60seconds.png and intro_go.png.
add_test(NAME screenshot_arcade_intro
         COMMAND test_powerup_hud --screenshot --combo=intro)
set_tests_properties(screenshot_arcade_intro PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_screeneffect")

# Opt-in widescreen layout (Layout::SetWideLayout, src/engine/render/Layout.h)
# visual screenshot test. Segment 1 (no --combo): full h.Init() boot, captures
# MainScreen / GameModeScreen / ShopScreen via the REAL Game::renderFrame
# widescreen viewport path (GameSDL.cpp), at both 3:2 (baseline) and 16:9
# (--widescreen, resizes the hidden window to 1136x640 + SetWideLayout(true)).
# Segment 2 (--combo=freeze|frenzy|x2|freeze+frenzy|all): component-isolation
# powerup-effect capture at 16:9 only (3:2 baseline already covered by
# screenshot_powerup_freeze/frenzy in test_powerup_hud.cpp above). "x2" is the
# score_mult powerup (poweruplist.xml power name="score_mult"); "all" activates
# all 3 (freeze+speed+score_mult) at once.
# See test_widescreen_render.cpp header comment for the full design.
# Output: tmp/test/screenshots/widescreen/*.png
fn_add_game_test(test_widescreen_render test_widescreen_render.cpp)
add_test(NAME widescreen_render_3x2 COMMAND test_widescreen_render --screenshot)
add_test(NAME widescreen_render_16x9 COMMAND test_widescreen_render --screenshot --widescreen)
set_tests_properties(widescreen_render_3x2 widescreen_render_16x9 PROPERTIES TIMEOUT 90
    LABELS "category_rendering;component_widescreen")

foreach(_ws_combo freeze frenzy x2 freeze+frenzy all)
    add_test(NAME widescreen_powerup_${_ws_combo}
             COMMAND test_widescreen_render --screenshot --widescreen --combo=${_ws_combo})
    set_tests_properties(widescreen_powerup_${_ws_combo} PROPERTIES TIMEOUT 60
        LABELS "category_rendering;component_widescreen")
endforeach()

# Segment 3: in-game HUD (ScoreControl) at both 3:2 and 16:9 -- isolates the
# top-left SCORE readout (score number + watermelon icon; no wordmark in true
# steady-state gameplay, see test_widescreen_render.cpp's ingame_hud comment)
# so its widescreen edge-alignment can be eyeballed against gameover.factboard.
# Output: tmp/test/screenshots/widescreen/ingame_hud_{3x2,16x9}.png
add_test(NAME widescreen_ingame_hud_3x2
         COMMAND test_widescreen_render --screenshot --combo=ingame_hud)
add_test(NAME widescreen_ingame_hud_16x9
         COMMAND test_widescreen_render --screenshot --widescreen --combo=ingame_hud)
set_tests_properties(widescreen_ingame_hud_3x2 widescreen_ingame_hud_16x9 PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_widescreen")

# TimeSinkControl ("Berry Blast" time-award board) render test. Drives the
# real PowerUpManager::ActivatePower("time_sink") activation path (poweruplist.xml),
# settles the ACTIVE alpha ratchet, forces a natural expiry into the RELEASE
# self-animation phase, and captures the "+M:SS" fly-in label at 3 points
# (m_TimeElapsed ~0.1s/0.7s/1.0s). Was a no-op stub before the v1.6.1
# ASM-spec port (TimeSinkControl.cpp).
# Output: tmp/test/screenshots/timesinkcontrol/{peak,mid_flyin,near_award}.png
fn_add_game_test(test_timesinkcontrol_render test_timesinkcontrol_render.cpp)
add_test(NAME timesinkcontrol_render COMMAND test_timesinkcontrol_render --screenshot)
set_tests_properties(timesinkcontrol_render PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_timesinkcontrol")

# ScoreControl component screenshot test. Renders the in-game HUD score widget
# (score.tex wordmark + animated score number + BEST: highscore banner + NEW BEST! popup,
# plus the Arcade-only "x%d" score-multiplier badge) in isolation on a clean background.
# Runs 4 sequential cases in one executable: default / active / suppressed / arcade_x2.
# Output: tmp/test/screenshots/scorecontrol/{default,active,suppressed,arcade_x2}.png.
fn_add_game_test(test_scorecontrol test_scorecontrol.cpp)
add_test(NAME screenshot_scorecontrol COMMAND test_scorecontrol --screenshot)
set_tests_properties(screenshot_scorecontrol PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_scorecontrol")

# FruitSaveData SaveGame/LoadGame round-trip (#291 Part 2).
# Builds a representative FruitSaveData, SaveGame's it to ./FruitySave.xml, LoadGame's
# it into a fresh object, and asserts every round-tripped field plus that the emitted
# XML uses the BINARY element names (<state>/<total>/<unlocked>, not <que>-for-state/
# <SliceTotal>/<achievements>). Pure in-process: constructs a Game only for the
# MortarGame version singleton; no SDL/GL/audio.
fn_add_game_test(test_save_roundtrip test_save_roundtrip.cpp NO_GL_CONTEXT)
add_test(NAME save_roundtrip COMMAND test_save_roundtrip)
set_tests_properties(save_roundtrip PROPERTIES TIMEOUT 30)

# Arcade bonus-scoring writer/reader hash-matching + formula invariants.
# High-fan-in scoring leaf where 3 real bugs hid in one session: AddCombo
# wrong-key+flat-count, No-Bananas missing per-name AddToTotal, and _drops
# written on slice (should be _total-only on slice, _drops on the KillFruit
# drop path). Covers: (1) AddCombo running-sum + best-combo max against the
# real xml/bonusAwards.xml combo table, under the literal combo_bonus/
# best_combo hashes; (2) Bonus::IsAchieved/BonusType::GetBest tier gating for
# combo_bonus and the No-Bananas (frenzy/freeze/scorex2 equals=0) bonus type;
# (3) ClearTotals wipes m_Totals (per-game) but not m_SessionTotals
# (lifetime); (4) FruitSaveData-level _total vs _drops writer-key separation
# (does NOT drive the real Fruit::CollisionResponse -- too heavy for this
# leaf test, see the CASE 4 gap comment). Pure in-process: constructs a Game
# + FileSystem_Direct only (mirrors GameInitialise.cpp Step 3) so
# BonusManager::Init() loads the real bonusAwards.xml; no SDL/GL/audio.
fn_add_game_test(test_bonus_manager test_bonus_manager.cpp NO_GL_CONTEXT)
add_test(NAME bonus_manager COMMAND test_bonus_manager)
set_tests_properties(bonus_manager PROPERTIES TIMEOUT 30)

# PowerUpManager::ClearTimedPowers / Reset Purchaseable()-vs-IsPurchaseable()
# predicate regression guard. Bomb-hit ClearTimedPowers must strip active
# timed cost-0 powers (freeze/frenzy/score x2 -- single="true" but no
# <purchase_info>) while leaving genuinely purchased (cost>0) powers alone.
# Pure in-process: Game + FileSystem_Direct only, loads the real
# xml/poweruplist.xml; no SDL/GL/audio.
fn_add_game_test(test_powerup test_powerup.cpp NO_GL_CONTEXT)
add_test(NAME powerup COMMAND test_powerup)
set_tests_properties(powerup PROPERTIES TIMEOUT 30)

# AddToCurrentScore clamp regression guard (#222 fix).
# Guards: score 0 + gain=3 -> 3; score 3 + penalty=-10 -> 0 (not -7); score 0
# + penalty=-5 -> 0; score 0 + gain=50 -> 50. Uses TestHarness so
# Game::GetInstance() + PowerUpManager + g_ScoreDelegate are live.
fn_add_game_test(test_game_over test_game_over.cpp)
add_test(NAME game_over COMMAND test_game_over)
set_tests_properties(game_over PROPERTIES TIMEOUT 60)

# HUD draw-alpha vs slow-mo field conflation regression guard.
# Reproduces the bug where ScoreControl::Draw read HUD+0x24 (m_globalTimeScale,
# slow-mo) instead of HUD+0x20 (per-frame draw-alpha) for score visibility.
# Drives HUD::Update (writes draw-alpha) then simulates a slow-mo writer
# (lowers +0x24 to 0.0) and asserts ScoreControl::m_DrawColour.a == 255.
# FAILS before the fix (alpha ~0); PASSES after it.
fn_add_game_test(test_hud_alpha test_hud_alpha.cpp)
add_test(NAME hud_alpha COMMAND test_hud_alpha)
set_tests_properties(hud_alpha PROPERTIES TIMEOUT 60)

# Renderer 2D pixel-readback + golden-image suite (#225 regression guard).
# Covers: solid tint, full-UV orientation, sub-rect UV (the #225 UV-convention
# bug guard), alpha blend, draw_fullscreen_quad, DrawTriList.
# Renders into a 64x64 offscreen FBO -- no game init, no asset loading.
# If no GL context is available (headless CI), the test prints
#   "SKIP: no GL context" and exits 77 (CTest SKIP, not FAIL).
# Golden images live in tests/golden/renderer/<case>.png (source tree). The test
# reads/writes them via the relative path "tests/golden/renderer/", so it must run
# from the project root -> WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}.
# Regenerate goldens: ./build/host/tests/Debug/test_renderer --update-golden  (from project root)
fn_add_game_test(test_renderer test_renderer.cpp)
add_test(NAME renderer COMMAND test_renderer)
set_tests_properties(renderer PROPERTIES
    TIMEOUT 60 SKIP_RETURN_CODE 77
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Font alignment pixel-readback test (guards RE-confirmed binary semantics).
# Renders "123" at a known anchor X with each alignment flag via Font::DrawString
# (the same path ScoreControl's score digits use) into an offscreen FBO, then reads
# pixels to find the glyph bounding box and asserts LEFT/RIGHT/CENTER placement.
# Key assertion: align=0x0D (ScoreControl's score flag) must render LEFT-anchored.
# Uses fn_add_game_test (full game boot) so pFontNumbers is loaded; the test itself
# uses an FBO for isolation rather than the window framebuffer.
# Exits 77 (CTest SKIP) if no GL context is available.
fn_add_game_test(test_font_align test_font_align.cpp)
add_test(NAME font_align COMMAND test_font_align)
set_tests_properties(font_align PROPERTIES
    TIMEOUT 60 SKIP_RETURN_CODE 77
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Sensei-board-lingers-after-quit repro diagnostic.
# Drives a real Classic GameOverScreen to STATE_MAIN_DISPLAY (creating
# FruitFactClassicFactPage + sensei GenericHUDControls), then fires the quit
# callback and ticks 180 frames with per-frame logging.  Asserts that
# pGameOverScreen goes null and no FruitFactControl/GenericHUDControl lingers.
# FAILS if the sensei board does not tear down (reproduces the bug);
# PASSES (with a note) if headless path reaps cleanly (real-app-only bug).
# FN_TEST enables BaseScreen::GetHUDControlsForTest() for pointer-identity
# tracking of sensei GenericHUDControls (GetType()==1 is too broad to use alone).
fn_add_game_test(test_sensei_teardown test_sensei_teardown.cpp)
target_compile_definitions(test_sensei_teardown PRIVATE FN_TEST)
add_test(NAME sensei_teardown COMMAND test_sensei_teardown)
set_tests_properties(sensei_teardown PROPERTIES TIMEOUT 120)

# Multi-language x gradient/effect BakedStringBox screenshot grid.
# Renders a 6-language x 6-effect grid using the TTF BakedStringBox API.
# Detects glyph presence per cell; falls back to "[no glyphs:XX]" caption.
# API note: SetStroke/SetShadow store params but rendering pass is a port TODO.
# Output: tmp/test/screenshots/text_render/grid.png
fn_add_game_test(test_text_render test_text_render.cpp)
add_test(NAME test_text_render_screenshot COMMAND test_text_render --screenshot)
set_tests_properties(test_text_render_screenshot PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)

# Latin + CJK glyph-clip visual regression grid.
# Renders real in-game strings pulled live (per-language Localisation::Load
# reload) from the shipped stringtables/translations_*.str -- EN/ZH/ZHT/JA/KO
# -- at the game's real BakedStringBox font sizes (14/22/30px), with a
# per-cell grid + baseline + ascent reference line so tall-glyph clipping
# (kanji radicals poking above the cell top) is visually obvious.
# Reproduces the Wii tall-glyph clip on host when configured with
# -DFN_TTF_BACKEND=stb -DFN_ENABLE_HD_ASSETS=OFF (matches the Wii SS=1 path).
# Output: tmp/test/screenshots/cjk_grid/grid.png
fn_add_game_test(test_cjk_grid test_cjk_grid.cpp)
add_test(NAME test_cjk_grid_screenshot COMMAND test_cjk_grid --screenshot)
set_tests_properties(test_cjk_grid_screenshot PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
    LABELS "category_rendering;component_bakedstringbox")

# BakedStringBox effects-combination screenshot grid.
# Renders a 4-column x 7-row grid covering every effect setter and combination:
#   plain / 2-color gradient / metallic gold+silver (fill row)
#   shadow small/medium/inner-glow/+gradient (shadow row)
#   stroke thin/thick/2-color / metal+shadow+stroke (stroke row)
#   font sizes 10/16/24/32px (size row)
#   alignment left-H/center-H/right-H/center-H+top-V (align row)
#   worldspace clipping no-clip/left-half/top-half/center-band (clip row)
#   ZH/JA/KO/AR language samples with metallic/shadow/gradient/plain (lang row)
# Validates the 200-byte BakedStringBox re-layout: shadow/gradient/metallic/
# stroke/clipping fields at their verified v1.6.1 offsets.
# Cell labels drawn in Verdana; falls back to pFontMain bitmap when absent.
# Output: tmp/test/screenshots/text_effects/grid.png
fn_add_game_test(test_text_effects test_text_effects.cpp)
add_test(NAME test_text_effects_screenshot COMMAND test_text_effects --screenshot)
set_tests_properties(test_text_effects_screenshot PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
    LABELS "category_rendering;component_bakedstringbox")

# Mortar::FancyBakedString layer-stack screenshot grid.
# Renders a 3-column x 2-row grid exercising the class's whole point -- the
# shadow/glow/main/stroke back-to-front layer stack -- directly (no HUD/screen):
#   main only / main+shadow(BLUR) / main+stroke(INNER_GLOW, unwired)
#   main+glow(STROKE) / main+shadow+stroke combo / main + 3-stop ApplyGradientSplit
# Hard-asserts every cell draws foreground pixels, and that the shadow cell paints
# text-area pixels the plain cell does not (proves the BLUR layer's padded
# rasterisation actually renders, not just a smoke test).
# Output: tmp/test/screenshots/fancybakedstring/grid.png
fn_add_game_test(test_fancybakedstring_render test_fancybakedstring_render.cpp)
add_test(NAME test_fancybakedstring_render_screenshot COMMAND test_fancybakedstring_render --screenshot)
set_tests_properties(test_fancybakedstring_render_screenshot PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
    LABELS "category_rendering;component_fancybakedstring")

# ShopListItem TTF box-creation guard (#264).
# Verifies that after NewDraw() the title (m_pBox0) and category (m_pBox1)
# BakedStringBox pointers are non-null and m_TintA is cached to m_Type.
# Proves the structural v1.6.1 TTF port replaced the v1.5.x bitmap monolith.
# Headless: passes in ctest -E screenshot; no display needed.
fn_add_game_test(test_shoplistitem test_shoplistitem.cpp)
add_test(NAME shoplistitem COMMAND test_shoplistitem)
set_tests_properties(shoplistitem PROPERTIES TIMEOUT 60)

# ShopListItem isolated row render test.
# Renders a 5-row column (short CJK, long CJK, locked, new-badge, selected-badge)
# on a clean dark background with Verdana captions.
# Headless: passes in ctest -E screenshot; no display needed.
# Screenshot variants write tmp/test/screenshots/shoplistitem/<lang>.png (need GPU).
fn_add_game_test(test_shoplistitem_render test_shoplistitem_render.cpp)
add_test(NAME shoplistitem_render COMMAND test_shoplistitem_render)
set_tests_properties(shoplistitem_render PROPERTIES TIMEOUT 60)
add_test(NAME screenshot_shoplistitem_english COMMAND test_shoplistitem_render --screenshot --lang=english_us)
add_test(NAME screenshot_shoplistitem_chinese COMMAND test_shoplistitem_render --screenshot --lang=chinese)
set_tests_properties(screenshot_shoplistitem_english PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_shoplistitem;lang_english_us")
set_tests_properties(screenshot_shoplistitem_chinese PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_shoplistitem;lang_chinese")

# MenuButton isolated state render test.
# Renders 4 toggle-style MenuButtons side by side, one per Draw feature of the
# v1.6.1 MenuButton::Draw @0x0019c2e4 reconciliation:
#   normal quad+curved-label / press-dim RGB*0.5 (m_bAcceptsTouch=0) /
#   bouncing NEW badge (IngamePopup 0x10, timer pinned mid-swing) /
#   8-blade loading sparkle ring (blurry_backing.tex, timer pinned mid-anim).
# Headless: state/texture/popup wiring assertions -- passes with ctest -E screenshot.
# Screenshot variant writes tmp/test/screenshots/menubutton/states.png (needs GPU).
fn_add_game_test(test_menubutton_render test_menubutton_render.cpp)
add_test(NAME menubutton_render COMMAND test_menubutton_render)
set_tests_properties(menubutton_render PROPERTIES TIMEOUT 60
    LABELS "component_menubutton")
add_test(NAME screenshot_menubutton COMMAND test_menubutton_render --screenshot)
set_tests_properties(screenshot_menubutton PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_menubutton")

# AboutScreen headless layout/structure guard.
# Verifies the faithful AboutScreen ctor+Init path creates:
#   - 9 BakedStringBox credit-text controls (title+heading+version+6 credits)
#   - >= 10 scrolling marquee lines (CreateCreditsMarquee)
#   - Screen transitions state 0->1 (not pending-removal after settle)
# Headless -- passes with ctest -E screenshot.
# Screenshot variants write tmp/test/screenshots/about/<lang>.png (need GPU).
# FN_TEST exposes GetBuiltBoxCount()/GetMarqueeCount() test-only accessors.
fn_add_game_test(test_aboutscreen test_aboutscreen.cpp)
target_compile_definitions(test_aboutscreen PRIVATE FN_TEST)
add_test(NAME aboutscreen COMMAND test_aboutscreen)
set_tests_properties(aboutscreen PROPERTIES TIMEOUT 60)
add_test(NAME screenshot_about_english COMMAND test_aboutscreen --screenshot --lang=english_us)
add_test(NAME screenshot_about_chinese COMMAND test_aboutscreen --screenshot --lang=chinese)
set_tests_properties(screenshot_about_english PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_about;lang_english_us")
set_tests_properties(screenshot_about_chinese PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_about;lang_chinese")

# GameModeScreen headless layout/structure guard.
# Verifies the GameModeScreen activation flow:
#   - 3 BakedStringBox title/desc/info boxes built in ctor (TTF path)
#   - 4 mode buttons (back, classic, zen, arcade) in HUD after state 0->2 transition
# Headless -- passes with ctest -E screenshot.
# Screenshot variants write tmp/test/screenshots/gamemode/<lang>.png (need GPU).
fn_add_game_test(test_gamemodescreen test_gamemodescreen.cpp)
add_test(NAME gamemodescreen COMMAND test_gamemodescreen)
set_tests_properties(gamemodescreen PROPERTIES TIMEOUT 60)
add_test(NAME screenshot_gamemode_english COMMAND test_gamemodescreen --screenshot --lang=english_us)
add_test(NAME screenshot_gamemode_chinese COMMAND test_gamemodescreen --screenshot --lang=chinese)
set_tests_properties(screenshot_gamemode_english PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_gamemode;lang_english_us")
set_tests_properties(screenshot_gamemode_chinese PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_gamemode;lang_chinese")

# ShopScreen headless layout/structure guard.
# Verifies ShopScreen::Init creates m_pShopList (ScrollingMenu) and adds it to HUD.
# Also verifies ShopListItem v1.6.1 TTF NewDraw path: m_pBox0 (title) and m_pBox1
# (category) BakedStringBoxes non-null after NewDraw() -- same check as test_shoplistitem.
# Headless -- passes with ctest -E screenshot.
# Screenshot variants write tmp/test/screenshots/shop/<lang>.png (need GPU).
fn_add_game_test(test_shopscreen test_shopscreen.cpp)
add_test(NAME shopscreen COMMAND test_shopscreen)
set_tests_properties(shopscreen PROPERTIES TIMEOUT 60)
add_test(NAME screenshot_shop_english COMMAND test_shopscreen --screenshot --lang=english_us)
add_test(NAME screenshot_shop_chinese COMMAND test_shopscreen --screenshot --lang=chinese)
set_tests_properties(screenshot_shop_english PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_shop;lang_english_us")
set_tests_properties(screenshot_shop_chinese PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_shop;lang_chinese")

# IngamePopup badge localisation regression guard.
# Verifies that the NEW (0x10), SELECTED (0x11), and NEW BEST! (0x0F) badge labels
# localise correctly across 5 locales: GETSTRING(0x399/0x3c5/0x2dc) returns
# non-null non-empty strings, and each IngamePopup has at least one BakedStringBox.
# Badges are built by BuildAllPopups() inside PreloadRings() during game.init();
# the --lang= flag sets languageFlag before game.init() so glyphs are baked in
# the requested locale (same path as the real game).
# Arabic (0x14) uses arabic.ttf (loaded by PreloadFontsTTF) -- renders RTL glyphs.
# Headless -- passes with ctest -E screenshot.
# Screenshot variants write tmp/test/screenshots/badges/<lang>.png (need GPU).
fn_add_game_test(test_ingamepopup_badges test_ingamepopup_badges.cpp)
add_test(NAME ingamepopup_badges COMMAND test_ingamepopup_badges)
set_tests_properties(ingamepopup_badges PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_ingamepopup;screen_badges")
add_test(NAME screenshot_badges_english  COMMAND test_ingamepopup_badges --screenshot --lang=english_us)
add_test(NAME screenshot_badges_chinese  COMMAND test_ingamepopup_badges --screenshot --lang=chinese)
add_test(NAME screenshot_badges_japanese COMMAND test_ingamepopup_badges --screenshot --lang=japanese)
add_test(NAME screenshot_badges_korean   COMMAND test_ingamepopup_badges --screenshot --lang=korean)
add_test(NAME screenshot_badges_arabic   COMMAND test_ingamepopup_badges --screenshot --lang=arabic)
set_tests_properties(screenshot_badges_english PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_ingamepopup;screen_badges;lang_english_us")
set_tests_properties(screenshot_badges_chinese PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_ingamepopup;screen_badges;lang_chinese")
set_tests_properties(screenshot_badges_japanese PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_ingamepopup;screen_badges;lang_japanese")
set_tests_properties(screenshot_badges_korean PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_ingamepopup;screen_badges;lang_korean")
set_tests_properties(screenshot_badges_arabic PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_ingamepopup;screen_badges;lang_arabic")

# PowerUpShop draw-position isolation guard.
# Verifies PowerUpShop::Init populates m_PurchasablePowerUps and m_SlotLayout,
# and that the panel renders in-range after the +480/+320 offset removal.
# Headless -- passes with ctest -E screenshot.
# Screenshot variants write tmp/test/screenshots/powerupshop/<lang>.png (need GPU).
fn_add_game_test(test_powerupshop test_powerupshop.cpp)
add_test(NAME powerupshop COMMAND test_powerupshop)
set_tests_properties(powerupshop PROPERTIES TIMEOUT 60)
add_test(NAME screenshot_powerupshop_english COMMAND test_powerupshop --screenshot --lang=english_us)
add_test(NAME screenshot_powerupshop_chinese COMMAND test_powerupshop --screenshot --lang=chinese)
set_tests_properties(screenshot_powerupshop_english PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_powerupshop")
set_tests_properties(screenshot_powerupshop_chinese PROPERTIES TIMEOUT 60
    LABELS "category_rendering;screen_powerupshop")

# DojoScreen -> AboutScreen / ShopScreen navigation regression test.
# Exercises the REAL navigation path (DojoScreen state machine + entity-count gate)
# rather than constructing child screens directly. The existing isolated tests
# (test_aboutscreen, test_shopscreen) construct the child screens directly; THIS
# test proves the full DojoScreen->child navigation works end-to-end.
#
# Key gate: DojoScreen states 2/3 require GetNumEntities(0)==0 && (1)==0 before
# creating the child screen. TestFireAboutSlice()/TestFireShopSlice() replicate
# the real ring-slice path: ClearMenuItems() flings all live menu entities, then
# the callback fires. ActorManager::Update() reaps the flung entities so the gate
# clears and the child screen is created.
#
# Headless pass: asserts transition fired (m_pBackButton nulled, child screen in HUD).
# Screenshot pass: captures the child screen content (not blank = navigation works).
#
# FN_TEST enables TestFireAboutSlice/TestFireShopSlice/TestGetState/
# TestTransitionFired/TestGetAboutScreen on DojoScreen.
fn_add_game_test(test_dojo_navigation test_dojo_navigation.cpp)
target_compile_definitions(test_dojo_navigation PRIVATE FN_TEST)
add_test(NAME dojo_nav_about COMMAND test_dojo_navigation --mode=about)
add_test(NAME dojo_nav_shop  COMMAND test_dojo_navigation --mode=shop)
set_tests_properties(dojo_nav_about PROPERTIES TIMEOUT 120
    LABELS "category_rendering;screen_about;nav_dojo_to_about")
set_tests_properties(dojo_nav_shop PROPERTIES TIMEOUT 120
    LABELS "category_rendering;screen_shop;nav_dojo_to_shop")
add_test(NAME screenshot_dojo_nav_about COMMAND test_dojo_navigation --mode=about --screenshot)
add_test(NAME screenshot_dojo_nav_shop  COMMAND test_dojo_navigation --mode=shop  --screenshot)
set_tests_properties(screenshot_dojo_nav_about PROPERTIES TIMEOUT 120
    LABELS "category_rendering;screen_about;nav_dojo_to_about")
set_tests_properties(screenshot_dojo_nav_shop PROPERTIES TIMEOUT 120
    LABELS "category_rendering;screen_shop;nav_dojo_to_shop")

# Fruit::Init pool-reuse event-clear regression guard (#346).
# Drives the REAL ActorManager pool-reuse path: Add(0) -> subscribe dummy
# delegates to m_OnSliced/m_OnKilled/m_OnExpired -> Deactivate -> Add(0) again
# (recycle) -> Init() -> asserts all three events are Empty(). Fails if the
# three Clear() calls at the top of Fruit::Init are removed. Headless -- no
# screenshot, no audio played.
fn_add_game_test(test_fruit_pool_reuse test_fruit_pool_reuse.cpp)
add_test(NAME fruit_pool_reuse COMMAND test_fruit_pool_reuse)
set_tests_properties(fruit_pool_reuse PROPERTIES TIMEOUT 60)

# SlashEntity blade-trail texture/colour "black blob" regression investigation.
# Drives a single isolated SlashEntity through GameInit(0) + direct touch-API
# calls (no HUD/menu). Captures 4 passes: the intrinsic first-slash black
# reproduction (diagnostic only, not a hard failure), the self-corrected
# white ribbon, and a leaked-vs-clean GL blend/texenv comparison (hard
# assertions -- DrawSlice/DrawTriStrip must self-reset blend+texenv).
# Headless -- passes with ctest -E screenshot (still needs a GL context).
# Screenshot variant writes tmp/test/screenshots/slashtexture/*.png (need GPU).
fn_add_game_test(test_slash_texture_render test_slash_texture_render.cpp)
add_test(NAME slash_texture_render COMMAND test_slash_texture_render)
set_tests_properties(slash_texture_render PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_slashentity")
add_test(NAME screenshot_slash_texture COMMAND test_slash_texture_render --screenshot)
set_tests_properties(screenshot_slash_texture PROPERTIES TIMEOUT 60
    LABELS "category_rendering;component_slashentity")

# SuperFruitControl finale-tick regression guard.
# Proves the super-fruit finale is actually ticked by HUD::Update after the
# SuperFruitControl -> HUDControl3d re-base (previously created-but-never-ticked
# dead code: nothing added the controller to game_work.mHud, so its Update()
# never ran a single frame). Drives the REAL slice path (Fruit::CollisionResponse
# -> Fruit::FruitWasSlicedEvent() -> SuperFruitControl::SuperFruitSliced), then
# ticks the real frame loop and asserts m_Timer advances (the core dead-code
# proof) and that the full phase ladder runs to completion (m_Timer crosses
# m_Lifetime, then the controller self-removes from the HUD). Headless -- no
# screenshot, no GPU pixel readback; verifies tick logic only (DrawOrder finale
# VFX is a separate unported TODO).
fn_add_game_test(test_superfruit_finale_tick test_superfruit_finale_tick.cpp)
add_test(NAME superfruit_finale_tick COMMAND test_superfruit_finale_tick)
set_tests_properties(superfruit_finale_tick PROPERTIES TIMEOUT 120)
