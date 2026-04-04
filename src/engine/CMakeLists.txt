# Mortar Engine static library
# Replaces old fn_engine, fn_math, fn_platform

add_library(mortar_engine STATIC
    # Phase 1 — Foundation
    core/SystemManager.cpp
    math/Random.cpp
    util/StringHash.cpp
)
target_include_directories(mortar_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mortar_engine PUBLIC SDL2::SDL2)
target_compile_features(mortar_engine PUBLIC cxx_std_11)

# Legacy fn_engine kept temporarily for game code that still uses old includes
add_library(fn_engine STATIC
    gl_funcs.cpp
    tex_loader.cpp
    Renderer.cpp
    Mesh.cpp
)
target_include_directories(fn_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fn_engine PUBLIC SDL2::SDL2 mortar_engine)
target_link_libraries(fruit-ninja PRIVATE fn_engine)
