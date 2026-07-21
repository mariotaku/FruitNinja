#ifndef FN_PLATFORM_WII_WIIVIDEO_H
#define FN_PLATFORM_WII_WIIVIDEO_H

// Port specific: shared VIDEO/GX state seam between mainWii.cpp (which owns
// VIDEO_Init + the double-buffered XFBs + GX fifo) and DisplayManagerWii.cpp
// (which needs the active XFB + rmode to run GX_CopyDisp at swap time).
//
// Kept as an opaque accessor rather than exposing gccore types in a header so
// this file is includable from portable TUs for review; the .cpp side casts.
//
// Only meaningful when FRUIT_PLATFORM_WII is set.
#ifdef FRUIT_PLATFORM_WII

class InputTranslatorWii;

namespace fn {
namespace wii {

// The framebuffer GX_CopyDisp should write for the frame currently being
// presented. mainWii flips this between its two XFBs each swap. Returned as
// void* (real type: void* xfb from SYS_AllocateFramebuffer / MEM_K0_TO_K1).
void* CurrentXFB();

// Advance to the other XFB after a swap (mainWii calls this, or the swap does).
void  FlipXFB();

// GXRModeObj* (opaque) chosen at boot -- needed for GX_CopyDisp height, VI
// flush, and interlace handling.
void* VideoMode();

// Port specific: boot-splash -> game handoff flag (see SplashBootScreen.h).
// StartupEffects.cpp's DrawStartFade() calls NotifyGameSplashDrew() the
// first time it actually draws the logo (game task fully loaded and
// GameDraw reaching the splash-overlay tail). DisplayManagerWii::SwapBuffers
// polls GameSplashDrew() every frame: while false it keeps redrawing the
// embedded boot-splash quad over whatever the game drew (bridging the
// Splash-task -> Game-task load-stall gap that would otherwise present a
// black EFB); once true it stops and releases the transient splash buffer.
void NotifyGameSplashDrew();
bool GameSplashDrew();

// Port specific: the single InputTranslatorWii instance mainWii.cpp owns
// (drives WPAD IR/A into it every frame). GameWii.cpp's renderFrame() reads
// current pointer state from it via WiiPointer::Draw -- see
// InputTranslatorWii::GetPointer(). No binary equivalent.
InputTranslatorWii& GetInputTranslator();

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_WIIVIDEO_H
