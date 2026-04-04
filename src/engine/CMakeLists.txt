# Mortar Engine static library

add_library(mortar_engine STATIC
    # Phase 1 — Foundation
    core/SystemManager.cpp
    math/Random.cpp
    util/StringHash.cpp
    # Phase 2 — Core Singletons
    render/gl_funcs.cpp
    render/MatrixManager.cpp
    render/DisplayManager.cpp
    # Phase 3 — Rendering
    render/Renderer.cpp
    render/MortarCamera.cpp
)
target_include_directories(mortar_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mortar_engine PUBLIC SDL2::SDL2)
target_compile_features(mortar_engine PUBLIC cxx_std_11)

# Legacy engine sources added directly to executable
target_sources(fruit-ninja PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tex_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Mesh.cpp
)
