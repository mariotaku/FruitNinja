# Mortar Engine static library

add_library(mortar_engine ${FN_LIB_TYPE}
    # Phase 1 — Foundation
    core/SystemManager.cpp
    core/MortarGame.cpp
    math/Random.cpp
    math/MathUtil.cpp
    math/Math.cpp
    math/Colour.cpp
    compression/LZ8.cpp
    util/Immutable.cpp
    util/StringHash.cpp
    util/AsciiString.cpp
    util/StringTable.cpp
    util/Localisation.cpp
    util/PathCI.cpp
    util/LinkedHeap.cpp
    # Phase 2 — Core Singletons
    render/MatrixManager.cpp
    render/MatrixStack.cpp
    render/DisplayManager.cpp
)
if(NOT FN_ASM_VERIFY)
    target_sources(mortar_engine PRIVATE
        render/gl_funcsSDL.cpp
        render/gl_funcsWin32.cpp
        render/DisplayManagerSDL.cpp
    )
endif()
target_sources(mortar_engine PRIVATE
    # Phase 3 — Rendering
    render/Renderer.cpp
    render/MortarCamera.cpp
    # Phase 4 — Asset Pipeline
    asset/IFile.cpp
    asset/IFileSystem.cpp
    asset/IFile_Direct.cpp
    asset/File.cpp
)
if(NOT FN_ASM_VERIFY)
    target_sources(mortar_engine PRIVATE
        asset/FileSystemPosix.cpp
        asset/FileSystemWin32.cpp
    )
endif()
target_sources(mortar_engine PRIVATE
    asset/FileManager.cpp
    asset/Texture.cpp
    asset/ReloadableTexture.cpp
    asset/TextureManager.cpp
    asset/TextureSource.cpp
    asset/TextureFileFormat.cpp
    asset/TextureLoader.cpp
    asset/AlternativeTextureLoader.cpp
    asset/DataReader.cpp
    asset/FileDataReader.cpp
    asset/VectorDataReader.cpp
    asset/ResourceLoader.cpp
    asset/AnimationState.cpp
    asset/Skeleton.cpp
    asset/Geometry.cpp
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
    audio/GameSound.cpp
)
if(NOT FN_ASM_VERIFY)
    target_sources(mortar_engine PRIVATE
        audio/SoundManagerSDL.cpp
    )
endif()
target_sources(mortar_engine PRIVATE
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
    render/Utf8StringProxy.cpp
    render/Utf8StringIterator.cpp
    render/Font.cpp
    render/BakedString.cpp
    render/FontInterface.cpp
    render/FontCacheObjectTTF.cpp
    render/FontTTFRegistry.cpp
    render/BakedStringBox.cpp
    # MenuBackground — ChangeBackground + GetCurrentBackground
    MenuBackground.cpp
    # XML loading helper (CI fallback for case-sensitive filesystems)
    xml/XmlLoad.cpp
)
# TiXml.cpp is the sole tinyxml2 TU for the TiXml-1 shim.
# Excluded from asm-verify cross-build: no binary counterpart in this form.
if(NOT FN_ASM_VERIFY)
    target_sources(mortar_engine PRIVATE xml/TiXml.cpp)
endif()
target_sources(mortar_engine PRIVATE
    # Phase 9 — Network (online services stub; all methods no-op)
    network/NetworkManager.cpp
    network/P2PMessageHandling.cpp
    # Phase 10 — System stubs (Bada OS services; all no-op)
    system/PowerManager.cpp
    # Debug logger (SDL_Log backend)
)
if(NOT FN_ASM_VERIFY)
    target_sources(mortar_engine PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../debug/LoggerSDL.cpp)
endif()
target_include_directories(mortar_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
)
if(FN_ASM_VERIFY)
    target_include_directories(mortar_engine BEFORE PRIVATE
        ${FN_ASM_VERIFY_TOOLS}/cross-headers)
endif()

if(NOT FN_ASM_VERIFY)
    # GL backend link target. find_package(OpenGL) covers desktop GL_COMPAT
    # (libGL on Linux/Mesa; opengl32 on Windows); the ES1 / ES2 paths are
    # wired separately by FRUIT_GL_API.
    if (FRUIT_GL_API STREQUAL "GL_COMPAT")
        find_package(OpenGL REQUIRED)
        target_link_libraries(mortar_engine PUBLIC OpenGL::GL)
    elseif (FRUIT_GL_API STREQUAL "ES1")
        # webOS / embedded Linux. libGLESv1_CM + libEGL.
        target_link_libraries(mortar_engine PUBLIC GLESv1_CM EGL)
    elseif (FRUIT_GL_API STREQUAL "EMSCRIPTEN")
        # Port specific: WebGL is provided by emcc link flags (-sLEGACY_GL_EMULATION=1
        # -sGL_FFP_ONLY=1). No system GL library to link; the flags live on the
        # fruit-ninja executable target (root CMakeLists.txt).
        # No target_link_libraries call needed here.
    endif()
    target_link_libraries(mortar_engine PUBLIC SDL2::SDL2 tinyxml2::tinyxml2 Freetype::Freetype)
endif()
target_compile_features(mortar_engine PUBLIC cxx_std_11)
