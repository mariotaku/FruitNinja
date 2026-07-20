#ifndef FN_PLATFORM_WII_MEM2ALLOC_H
#define FN_PLATFORM_WII_MEM2ALLOC_H

// Port specific: no binary counterpart -- the Wii target's MEM1 (24 MB,
// "main" RAM, where libogc's default heap and the XFB/GX FIFO live) runs
// tight (~500 KB free margin per task #61's LogHeapUsage survey) while MEM2
// (64 MB, "auxiliary/ARAM-replacement" RAM added by the Wii over the
// GameCube) sits almost entirely unused -- nothing in this port allocates
// from it. GX samples textures from either arena identically, so moving the
// biggest movable MEM1 consumer (texture pixel buffers: tiled GX + retained
// linear copies, see gl_funcsWii.cpp) to MEM2 is free headroom with no
// rendering-behaviour change.
//
// Wii_MEM2Init() carves an allocator arena out of MEM2 via
// SYS_GetArena2Lo/Hi + SYS_SetArena2Lo (leaving a headroom margin below Hi
// for libogc/IOS) and must run once at boot, before any texture/asset load.
// Wii_MEM2Alloc/Free are a free-list allocator over that carved region.
//
// MAIN-THREAD-ONLY: textures and SFX load synchronously on the main thread
// (BlockLoader::PreloadBlock, TextureManager, gl_funcsWii.cpp's texture
// path) -- there is no second thread ever calling these, so no locking.
//
// Only compiled when FRUIT_PLATFORM_WII is set.
#ifdef FRUIT_PLATFORM_WII

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// Carves the MEM2 allocator arena. Call exactly once, early at boot (after
// VIDEO/GX init, before any texture/asset load -- see mainWii.cpp).
void Wii_MEM2Init();

// Allocates `size` bytes with `align`-byte alignment (default 32, the GX
// texture / DSP requirement) from the MEM2 arena. Returns NULL on failure
// (arena exhausted or Wii_MEM2Init() never called). Main-thread-only.
void* Wii_MEM2Alloc(u32 size, u32 align);

// Frees a block previously returned by Wii_MEM2Alloc. NULL is a no-op.
// NEVER pass a MEM1 pointer (plain malloc/memalign) here, and never pass a
// Wii_MEM2Alloc'd pointer to plain free() -- the two heaps are disjoint
// arenas and mismatched alloc/free corrupts either heap's free list.
void Wii_MEM2Free(void* p);

// Bytes currently free in the MEM2 allocator's own heap (not the raw arena
// -- reflects fragmentation/headroom of the carved region). For LogHeapUsage
// diagnostics only.
u32 Wii_MEM2FreeBytes();

#ifdef __cplusplus
}
#endif

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_MEM2ALLOC_H
