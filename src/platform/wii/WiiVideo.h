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

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII

#endif // FN_PLATFORM_WII_WIIVIDEO_H
