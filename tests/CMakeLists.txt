# tests/CMakeLists.txt — minimal ctest registration.

add_executable(test_localisation test_localisation.cpp)
target_link_libraries(test_localisation PRIVATE mortar_engine)
target_include_directories(test_localisation PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
add_test(NAME localisation COMMAND test_localisation)

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
else()
    # Linux/macOS: link the system GL so test_screen.cpp's direct
    # glReadPixels call resolves. The rest of the engine loads GL via
    # SDL_GL_GetProcAddress, but the test takes a screenshot via the
    # static symbol.
    find_package(OpenGL REQUIRED)
    target_link_libraries(test_screen PRIVATE OpenGL::GL)
endif()

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

foreach(screen main dojo about shop gamemode)
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
