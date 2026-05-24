# Mortar Engine static library

add_library(mortar_engine STATIC
    # Phase 1 — Foundation
    core/SystemManager.cpp
    core/MortarGame.cpp
    math/Random.cpp
    math/MathUtil.cpp
    math/Math.cpp
    compression/LZ8.cpp
    util/Immutable.cpp
    util/StringHash.cpp
    util/AsciiString.cpp
    util/StringTable.cpp
    util/Localisation.cpp
    util/PathCI.cpp
    # Phase 2 — Core Singletons
    render/gl_funcsSDL.cpp
    render/gl_funcsWin32.cpp
    render/MatrixManager.cpp
    render/DisplayManager.cpp
    render/DisplayManagerSDL.cpp
    # Phase 3 — Rendering
    render/Renderer.cpp
    render/MortarCamera.cpp
    # Phase 4 — Asset Pipeline
    asset/IFile.cpp
    asset/IFileSystem.cpp
    asset/IFile_Direct.cpp
    asset/FileSystemPosix.cpp
    asset/FileSystemWin32.cpp
    asset/File.cpp
    asset/FileManager.cpp
    asset/Texture.cpp
    asset/ReloadableTexture.cpp
    asset/TextureManager.cpp
    asset/DataReader.cpp
    asset/ResourceLoader.cpp
    asset/AnimationState.cpp
    asset/Skeleton.cpp
    asset/Mesh.cpp
    asset/Model.cpp
    asset/MeshManager.cpp
    asset/Effect.cpp
    asset/EffectDataTypes.cpp
    asset/SharedEffectProperties.cpp
    # Phase 5 — Input
    input/Touch.cpp
    input/InputManager.cpp
    input/InputDevice.cpp
    input/InputDeviceBada.cpp
    # Phase 6 — Audio
    audio/MortarSound.cpp
    audio/SoundManagerSDL.cpp
    audio/GameSound.cpp
    # Phase 7 — Particles
    # Entity + ActorManager live in src/entities/ (game-namespace port),
    # not the mortar_engine lib — the engine-namespace stubs that used to
    # be here were never wired in and have been removed.
    # Phase 7.5 -- Collision primitives
    collision/Col.cpp
    collision/ColSphere.cpp
    collision/ColLine.cpp
    collision/ColAABB.cpp
    particle/PSPParticleManager.cpp
    # Phase 8 — Font
    render/Utf8StringIterator.cpp
    render/Font.cpp
    render/BakedString.cpp
    # MenuBackground — ChangeBackground + GetCurrentBackground
    MenuBackground.cpp
    # Phase 9 — Network (online services stub; all methods no-op)
    network/NetworkManager.cpp
    # Phase 10 — System stubs (Bada OS services; all no-op)
    system/PowerManager.cpp
    # Debug logger (SDL_Log backend)
    ${CMAKE_SOURCE_DIR}/src/debug/LoggerSDL.cpp
)
target_include_directories(mortar_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# GL backend link target. find_package(OpenGL) covers desktop GL_COMPAT
# (libGL on Linux/Mesa; opengl32 on Windows); the ES1 / ES2 paths are
# wired separately by FRUIT_GL_API.
if (FRUIT_GL_API STREQUAL "GL_COMPAT")
    find_package(OpenGL REQUIRED)
    target_link_libraries(mortar_engine PUBLIC OpenGL::GL)
elseif (FRUIT_GL_API STREQUAL "ES1")
    # webOS / embedded Linux. libGLESv1_CM + libEGL.
    target_link_libraries(mortar_engine PUBLIC GLESv1_CM EGL)
endif()

target_link_libraries(mortar_engine PUBLIC SDL2::SDL2 tinyxml2::tinyxml2)
target_compile_features(mortar_engine PUBLIC cxx_std_11)
