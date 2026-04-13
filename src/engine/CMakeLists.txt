# Mortar Engine static library

add_library(mortar_engine STATIC
    # Phase 1 — Foundation
    core/SystemManager.cpp
    core/MortarGame.cpp
    math/Random.cpp
    util/StringHash.cpp
    # Phase 2 — Core Singletons
    render/gl_funcs.cpp
    render/MatrixManager.cpp
    render/DisplayManager.cpp
    # Phase 3 — Rendering
    render/Renderer.cpp
    render/MortarCamera.cpp
    # Phase 4 — Asset Pipeline
    asset/tex_loader.cpp
    asset/Texture.cpp
    asset/TextureManager.cpp
    asset/ResourceLoader.cpp
    asset/Skeleton.cpp
    asset/Mesh.cpp
    asset/MeshManager.cpp
    # Phase 5 — Input
    input/Touch.cpp
    # Phase 6 — Audio
    audio/MortarSound.cpp
    audio/SoundManager.cpp
    audio/GameSound.cpp
    # Phase 7 — Entities & Particles
    entity/Entity.cpp
    entity/ActorManager.cpp
    particle/PSPParticleManager.cpp
    # Phase 8 — Font
    render/Font.cpp
    render/BakedString.cpp
)
target_include_directories(mortar_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mortar_engine PUBLIC SDL2::SDL2 tinyxml2::tinyxml2)
target_compile_features(mortar_engine PUBLIC cxx_std_11)
