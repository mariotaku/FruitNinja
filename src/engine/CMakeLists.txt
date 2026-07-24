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
    render/Layout.cpp        # Port specific: opt-in widescreen layout seam (DIFFERS enhancement)
)
if(NOT FN_ASM_VERIFY AND NOT FRUIT_PLATFORM_WII)
    target_sources(mortar_engine PRIVATE
        render/gl_funcsSDL.cpp
        render/gl_funcsWin32.cpp
        render/DisplayManagerSDL.cpp
    )
endif()
if(NOT FN_ASM_VERIFY AND FRUIT_PLATFORM_WII)
    # Wii: GL-on-GX shim (backs every gl* call the core render TUs make) +
    # the GX/VIDEO swap. Replaces the SDL/Win32 GL backend files above so
    # Renderer.cpp/Texture.cpp/ShaderProgram.cpp/etc. compile unchanged.
    target_sources(mortar_engine PRIVATE
        render/gl_funcsWii.cpp
        render/DisplayManagerWii.cpp
    )
endif()
if(NOT FN_ASM_VERIFY AND FN_PREBAKED_FONTS)
    # Prebaked FreeType glyph atlas store (task #51/#52/#54; non-Wii support
    # added under FN_PREBAKED_FONTS) -- the SOLE glyph + face-metric source
    # whenever this option is ON (no FreeType/stb fallback, see
    # FontCacheObjectTTF.h/.cpp). Wii: tiled-GX .gxtx pages. Non-Wii: linear
    # WebP-in-.tex pages. Outer-gated on FN_PREBAKED_FONTS (was
    # FRUIT_PLATFORM_WII-only) inside the file itself.
    target_sources(mortar_engine PRIVATE
        render/BakedFontWii.cpp
    )
endif()
# Renderer backend: link-time split (mirrors GameSDL.cpp/GameWii.cpp). The GL
# backend (RendererGL.cpp) drives every platform whose gl* is real GL or the
# GX shim's baseline; the Wii native path swaps in RendererGX.cpp, which issues
# GX draws directly (separate proj + modelview -> correct 3D perspective).
fn_add_platform_source(_fn_renderer_platform_src render/RendererGX.cpp render/RendererGL.cpp)
target_sources(mortar_engine PRIVATE ${_fn_renderer_platform_src})
if(NOT FN_ASM_VERIFY)
    target_sources(mortar_engine PRIVATE
        render/NineSlice.cpp        # Port specific: 9-slice panel draw (no binary counterpart)
    )
endif()
target_sources(mortar_engine PRIVATE
    # Phase 3 — Rendering
    render/Renderer.cpp       # portable public API; backend (RendererGL/GX.cpp) selected above
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
    elseif(FRUIT_PLATFORM_WII)
        # Wii: ASND/AESND backend. SoundManagerWii.cpp is added to
        # fruit-ninja-game (not mortar_engine) by
        # src/platform/wii/CMakeLists.txt -- see src/platform/wii/README.md.
    else()
        target_sources(mortar_engine PRIVATE
            audio/SoundManagerSDL.cpp
        )
    endif()
endif()
if(NOT FN_ASM_VERIFY AND FN_OGG_AUDIO AND NOT EMSCRIPTEN)
    # webOS (SDL backend): decode sfx/*.ogg via stb_vorbis (public domain,
    # third_party/stb/stb_vorbis.c) instead of reading raw sfx/*.wav.pcm --
    # see SoundManagerSDL.cpp's LoadSound FN_OGG_AUDIO branch. Loop points come
    # from the build-generated table below (the .ogg has no .wav.pcm header).
    # Emscripten already has its own Ogg path (SoundManagerWebAudio.cpp, browser
    # decode) and never compiles SoundManagerSDL.cpp, so this block is
    # redundant there even if FN_OGG_AUDIO were somehow also set -- excluded
    # explicitly to avoid compiling stb_vorbis.c into a TU that never uses it.
    target_sources(mortar_engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/stb/stb_vorbis.c
    )
    # Single-file public-domain decoder: no stdio dependency needed (we always
    # decode from an in-memory buffer via stb_vorbis_decode_memory), so trim
    # the pulled-in libc surface for the cross/buildroot toolchain.
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/stb/stb_vorbis.c
        PROPERTIES COMPILE_DEFINITIONS "STB_VORBIS_NO_STDIO;STB_VORBIS_NO_PUSHDATA_API"
    )

    # Build-generated loop-start table (stage-assets.py --gen-loop-table; see
    # src/engine/audio/SfxLoopTable.h for the contract). Written by the
    # fn_asset_staging custom target BEFORE mortar_engine compiles it -- see
    # the add_dependencies(mortar_engine fn_asset_staging) call in the root
    # CMakeLists.txt (placed there because fn_asset_staging isn't defined yet
    # at the point add_subdirectory(src/engine) runs).
    set(FN_SFX_LOOP_TABLE_CPP "${CMAKE_BINARY_DIR}/generated/SfxLoopTable.generated.cpp")
    set_source_files_properties(${FN_SFX_LOOP_TABLE_CPP} PROPERTIES GENERATED TRUE)
    target_sources(mortar_engine PRIVATE ${FN_SFX_LOOP_TABLE_CPP})
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
    render/BakedStringBox.cpp
    render/BakedStringTTF.cpp
    render/FancyBakedString.cpp
    # MenuBackground — ChangeBackground + GetCurrentBackground
    MenuBackground.cpp
)
# Dynamic TTF glyph rendering -- backend-neutral (see the Mortar::TtfFace
# seam, src/engine/render/TtfBackend.h). These two TUs never call FreeType or
# stb_truetype directly, so they compile for every platform/config including
# FN_PREBAKED_FONTS builds. FN_TTF_BACKEND below selects exactly which backend
# .cpp implements the TtfFace seam when FN_PREBAKED_FONTS is OFF; when it is
# ON no backend is selected at all (task #54, see below).
target_sources(mortar_engine PRIVATE
    render/FontCacheObjectTTF.cpp
    render/FontTTFRegistry.cpp
)
# Task #54: FN_PREBAKED_FONTS builds have NO TtfFace backend at all --
# FontCacheObjectTTF is guarded FN_PREBAKED_FONTS to never construct/call one
# (see that file's m_Face guards); every glyph + face metric comes from the
# offline-baked FNT3 atlas (BakedFontWii.cpp, added above under the
# FN_PREBAKED_FONTS block) with NO FreeType/stb fallback -- same shape on
# host/web FN_PREBAKED_FONTS builds as on Wii (where the option is always ON).
# Neither TtfBackendStb.cpp (stb_truetype.h) nor TtfBackendFreetype.cpp is
# compiled or linked in that configuration, and FN_TTF_BACKEND is irrelevant.
if (NOT FN_PREBAKED_FONTS)
    if (FN_TTF_BACKEND STREQUAL "stb")
        target_sources(mortar_engine PRIVATE render/TtfBackendStb.cpp)
        target_include_directories(mortar_engine PRIVATE ${CMAKE_SOURCE_DIR})
    else()
        target_sources(mortar_engine PRIVATE render/TtfBackendFreetype.cpp)
    endif()
endif()
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
    # MP revival (feat/mp-revival): transport seam, port-only, no binary counterpart
    network/LoopbackTransport.cpp
    network/MpTransport.cpp
    # Phase 10 — System stubs (Bada OS services; all no-op)
    system/PowerManager.cpp
    system/PlatformFuncs.cpp
    # Debug logger (SDL_Log backend)
)
if(NOT FN_ASM_VERIFY)
    if(FRUIT_PLATFORM_WII)
        target_sources(mortar_engine PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../debug/LoggerWii.cpp)
    else()
        target_sources(mortar_engine PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../debug/LoggerSDL.cpp)
    endif()
endif()
target_include_directories(mortar_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
)
if(FN_ASM_VERIFY)
    target_include_directories(mortar_engine BEFORE PRIVATE
        ${FN_ASM_VERIFY_TOOLS}/cross-headers)
endif()

if(NOT FN_ASM_VERIFY AND NOT FRUIT_PLATFORM_WII)
    # GL backend link target. find_package(OpenGL) covers desktop GL_COMPAT
    # (libGL on Linux/Mesa; opengl32 on Windows); the ES1 / ES2 paths are
    # wired separately by FRUIT_GL_API. Skipped entirely for Wii -- GX
    # (libogc2) replaces GL and is linked from the wii platform lib/exe, not
    # here (see src/platform/wii/README.md's "GX render backend mapping").
    if (FRUIT_GL_API STREQUAL "GL_COMPAT")
        find_package(OpenGL REQUIRED)
        target_link_libraries(mortar_engine PUBLIC OpenGL::GL)
    elseif (FRUIT_GL_API STREQUAL "ES1")
        # Embedded Linux fixed-function. libGLESv1_CM + libEGL.
        # NOTE: GLES2 2D shader path (Renderer::DrawShaded2D) needs GL2.0 entry
        # points (glCreateShader etc.) that libGLESv1_CM does not export -- this
        # target fails to link on this backend. webOS uses FRUIT_GL_API_ES2
        # below instead, which links the real libGLESv2.
        target_link_libraries(mortar_engine PUBLIC GLESv1_CM EGL)
    elseif (FRUIT_GL_API STREQUAL "ES2")
        # webOS TV / modern embedded Linux. Real GLES2 (libGLESv2) + libEGL --
        # unlike ES1, this exports the shader entry points the 2D path needs,
        # so no hand-declared stub block is required (see gl_compat.h).
        # Plain library names resolve via the buildroot cross sysroot.
        target_link_libraries(mortar_engine PUBLIC GLESv2 EGL)
    elseif (FRUIT_GL_API STREQUAL "EMSCRIPTEN")
        # Port specific: WebGL is provided by emcc link flags (root CMakeLists.txt,
        # fruit-ninja executable target). No system GL library to link here.
        # No target_link_libraries call needed here.
    endif()
    target_link_libraries(mortar_engine PUBLIC SDL2::SDL2 tinyxml2::tinyxml2)
    if (NOT FN_PREBAKED_FONTS AND FN_TTF_BACKEND STREQUAL "freetype")
        target_link_libraries(mortar_engine PUBLIC Freetype::Freetype)
    endif()
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
if(NOT FN_ASM_VERIFY AND FRUIT_PLATFORM_WII)
    # tinyxml2 is portable C++ and still needed on Wii (see root CMakeLists.txt).
    target_link_libraries(mortar_engine PUBLIC tinyxml2::tinyxml2)
endif()
target_compile_features(mortar_engine PUBLIC cxx_std_11)
