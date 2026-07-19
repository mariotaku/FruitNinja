#ifndef FN_PLATFORM_WII_SPLASHBOOTSCREEN_H
#define FN_PLATFORM_WII_SPLASHBOOTSCREEN_H

// Port specific: Wii-only. Draws the HB logo splash's FRAME-0 appearance
// (see StartupEffects.cpp DrawStartFade) as a raw-GX draw straight into the
// EFB -- no TextureManager, no filesystem, no Mortar::Mesh/Renderer -- so it
// works before any of those subsystems exist, AND can be re-issued every
// frame thereafter to bridge the gap between the boot draw and the game's
// own first real DrawStartFade() call.
//
// Uses an embedded, COMPRESSED, pre-cropped/pre-downscaled copy of the logo
// (see the generated hb_logo_splash_blob.cpp / tools/wii/make-splash-blob.py /
// src/platform/wii/CMakeLists.txt). Only the compressed blob (tens of KB) is
// resident in .rodata; PrepareSplashBoot() inflates it once into a transient
// ~300 KB buffer (memalign'd) and uploads it into a retained GXTexObj so
// DrawSplashBootQuad() can redraw it cheaply on every subsequent frame until
// ReleaseSplashBoot() frees the transient buffer.
//
// --- Boot gap (SD mount + asset staging load) ---
// mainWii.cpp calls PrepareSplashBoot() once, right after WiiGxInit() (GX
// must already be initialised: FIFO, viewport, copy/scissor state,
// GX_CopyDisp done at least once) and before fatInitDefault(), then draws +
// presents one frame with DrawSplashBootQuad().
//
// --- State-transition gap (Splash task -> Game task load stall) ---
// DisplayManagerWii::SwapBuffers calls DrawSplashBootQuad() every frame,
// AFTER the game's own draw, until the game's DrawStartFade() reports (via
// NotifyGameSplashDrew()) that it has drawn its own first real frame --
// which is pixel-identical to this embedded copy, so the handoff is
// seamless. SwapBuffers then calls ReleaseSplashBoot() exactly once to free
// the transient buffer.
//
// Must reproduce DrawStartFade's on-screen appearance exactly (same crop
// baked into the pre-processed asset, same quad, same white-on-black
// frame-0 state) so there is no visible pop when the real draw takes over.
#ifdef FRUIT_PLATFORM_WII

namespace fn {
namespace wii {

// Inflate the embedded blob and upload it into a retained GXTexObj. Call
// once, right after WiiGxInit() and before fatInitDefault(). Returns false
// (and leaves the retained texture unset) if the embedded asset or the
// inflate looks wrong -- callers should then just skip the boot splash
// entirely (DrawSplashBootQuad() safely no-ops when not prepared).
bool PrepareSplashBoot();

// Draws the backdrop + logo quads into the current EFB using the retained
// GXTexObj from PrepareSplashBoot(). Fully self-contained: sets up its own
// viewport/scissor/projection/vtxdesc/TEV/zmode/cull state every call, so it
// is safe to call on any frame regardless of what state the caller left GX
// in. Does NOT call GX_CopyDisp/VIDEO_*/GX_DrawDone and does NOT free
// anything -- the caller presents. No-op if PrepareSplashBoot() hasn't
// succeeded yet, or after ReleaseSplashBoot() has run.
void DrawSplashBootQuad();

// Frees the transient inflate buffer. Idempotent -- safe to call more than
// once or before PrepareSplashBoot(). After this, DrawSplashBootQuad()
// becomes a no-op.
void ReleaseSplashBoot();

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_SPLASHBOOTSCREEN_H
