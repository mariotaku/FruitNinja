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
    util/Transition.cpp
    util/Utf8Encode.cpp
    util/StringTable.cpp
    util/Localisation.cpp
    util/PathCI.cpp
    util/PathFunctions.cpp
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
        render/NineSlice.cpp        # Port specific: 9-slice panel draw (no binary counterpart)
    )
endif()
target_sources(mortar_engine PRIVATE
    # Phase 3 — Rendering
    render/Renderer.cpp
    render/Shaders.cpp        # Port specific: GLES2 2D shader sources
    render/ShaderProgram.cpp  # Port specific: GL program wrapper
    render/MortarCamera.cpp
    render/QuadUtil.cpp
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
    asset/TextureConverter.cpp
    asset/TextureLoader.cpp
    asset/AlternativeTextureLoader.cpp
    asset/DataReader.cpp
    asset/DataStreamReader.cpp
    asset/FileDataReader.cpp
    asset/VectorDataReader.cpp
    asset/ResourceLoader.cpp
    asset/AnimationState.cpp
    asset/AnimationList.cpp
    asset/AnimationManager.cpp
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
    if(EMSCRIPTEN)
        # Web build: replace the SDL software mixer with the Web Audio API
        # backend (browser Ogg/Vorbis decode + off-main-thread float mixing).
        # SoundManagerSDL.cpp is NOT compiled here; SoundManagerWebAudio.cpp
        # implements the same SoundManager.h API. See that file's header.
        target_sources(mortar_engine PRIVATE
            audio/SoundManagerWebAudio.cpp
        )
    else()
        target_sources(mortar_engine PRIVATE
            audio/SoundManagerSDL.cpp
        )
    endif()
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
    render/BakedStringTTF.cpp
    render/FancyBakedString.cpp
    # MenuBackground — ChangeBackground + GetCurrentBackground
    MenuBackground.cpp
)
# TiXml.cpp is the sole tinyxml2 TU for the TiXml-1 shim.
# Excluded from asm-verify cross-build: no binary counterpart in this form.
if(NOT FN_ASM_VERIFY)
    target_sources(mortar_engine PRIVATE xml/TiXml.cpp)
endif()
# BinaryXml.cpp stubs ParseBinary/WriteBinary (no tinyxml2 dep; cross-build safe).
target_sources(mortar_engine PRIVATE xml/BinaryXml.cpp)
target_sources(mortar_engine PRIVATE
    # Phase 9 — Network (online services stub; all methods no-op)
    network/NetworkManager.cpp
    network/OpenFeint.cpp
    network/P2PMessageHandling.cpp
    # Phase 10 — System stubs (Bada OS services; all no-op)
    system/PowerManager.cpp
    system/PlatformFuncs.cpp
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
        # TODO: GLES2 2D shader path (Renderer::DrawShaded2D) needs GL2.0 entry
        # points (glCreateShader etc.) that libGLESv1_CM does not export -- this
        # target will fail to link until ES1 moves to libGLESv2 or gains a
        # runtime-loaded GLES2 subset. Out of scope for the GLES2 2D migration
        # (host/Emscripten only); revisit before building for webOS.
        target_link_libraries(mortar_engine PUBLIC GLESv1_CM EGL)
    elseif (FRUIT_GL_API STREQUAL "EMSCRIPTEN")
        # Port specific: WebGL is provided by emcc link flags (root CMakeLists.txt,
        # fruit-ninja executable target). No system GL library to link here.
        # No target_link_libraries call needed here.
    endif()
    target_link_libraries(mortar_engine PUBLIC SDL2::SDL2 tinyxml2::tinyxml2 Freetype::Freetype)
    # Port specific: web compressed textures (libwebp). Decoder linked into the
    # engine (TextureFileFormat.cpp's ReadWebP). PRIVATE: only the .cpp needs it;
    # the public header #if's WebP out. Present on desktop too (reader is inert).
    if (TARGET webpdecoder)
        target_link_libraries(mortar_engine PRIVATE webpdecoder)
        # FetchContent source layout puts the public headers at <src>/src/webp/;
        # add that base so `#include "webp/decode.h"` resolves regardless of what
        # the webpdecoder target exports as its INTERFACE include dir.
        if (DEFINED libwebp_SOURCE_DIR)
            target_include_directories(mortar_engine PRIVATE ${libwebp_SOURCE_DIR}/src)
        endif()
    endif()
endif()
target_compile_features(mortar_engine PUBLIC cxx_std_11)
