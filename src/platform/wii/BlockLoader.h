#ifndef FN_PLATFORM_WII_BLOCKLOADER_H
#define FN_PLATFORM_WII_BLOCKLOADER_H

// Task #36 Stage 2 -- synchronous block-scoped resource preload (V1).
// See tmp/wii/loader-blueprint.md sections 6/7 (Stage 2) for the design;
// this V1 deliberately simplifies it:
//   - SYNCHRONOUS only -- PreloadBlock() blocks the main thread while it
//     loads. No LWP thread, no mutex queue, no spinner (blueprint Stage 3).
//   - No eviction/FreeBlock -- everything preloaded here stays resident for
//     the process lifetime (V1 "union fits budget" per the task; Stage 4
//     would add FreeBlock()).
//   - Idempotent -- every underlying Load call (TextureManager::
//     LoadLocalisedTexture, SoundManager::PreLoadSound, MeshManager::Load)
//     already cache-checks internally, so calling PreloadBlock twice for the
//     same block is a cheap no-op the second time.
//
// WHY a per-block texture/model vector at all, given TextureManager's cache
// is just a WeakPtr map (see ResBlock.h / loader-blueprint.md Risk R1)? The
// cache does NOT keep a texture resident by itself -- the strong SmartPtr
// returned by Load() must be held somewhere or the texture is evicted the
// instant that temporary SmartPtr goes out of scope. BlockLoader::s_Held
// is that holder. Meshes need no separate holder: MeshManager::Load already
// inserts the strong SmartPtr<Model> into its own m_Models list (see
// MeshManager.cpp:70-73), so the manager itself keeps it resident.
//
// Only compiled when FRUIT_PLATFORM_WII is set.
#ifdef FRUIT_PLATFORM_WII

#include "platform/wii/ResBlock.h"

namespace fn {
namespace wii {

class BlockLoader {
public:
    // Force-loads every texture/SFX/mesh/font-size in the manifest for
    // `block` (INGAME's manifest includes the GAMEOVER deltas -- see file
    // comment in BlockLoader.cpp -- since gameover pops instantly over the
    // frozen game with no load-covering transition of its own). Synchronous:
    // does not return until every entry has been loaded (or failed) --
    // intended to be called at a block-entry hook that already sits behind a
    // fade/transition (GameModeScreen::SetupLevel, ShopScreen ctor).
    //
    // Safe to call every time the block is entered -- idempotent (see file
    // comment above).
    static void PreloadBlock(ResBlockFlag block);
};

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_BLOCKLOADER_H
