// Port specific: Wii backend for DisplayManager -- the GX/VIDEO swap that
// replaces DisplayManagerSDL.cpp's SDL_GL_SwapWindow. The portable part of
// DisplayManager lives in DisplayManager.cpp; this file only provides the
// pieces that touch GX/VIDEO.
//
// DisplayManagerSDL.cpp defines exactly one method (SwapBuffers); this twin
// provides the same one.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see src/engine/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "render/DisplayManager.h"
#include "platform/wii/WiiVideo.h"
#include "platform/wii/SplashBootScreen.h"

#include <gccore.h>

namespace Mortar {

void DisplayManager::SwapBuffers(void* /*window*/) {
    void* xfb   = fn::wii::CurrentXFB();
    void* rmode = fn::wii::VideoMode();

    GX_DrawDone();
    GX_SetColorUpdate(GX_TRUE);
    // Force Z-update ON so GX_CopyDisp actually clears the depth buffer. The
    // EFB Z clear is gated by the last GX_SetZMode's write-enable; if the frame
    // ends with depth-write OFF (painter's-order 2D), the Z clear silently
    // no-ops and stale fruit depth carries into next frame -> black-center on
    // the tumbling menu fruit. Wii analog of DisplayManager.cpp:66's
    // glDepthMask(GL_TRUE) before glClear. GX_ALWAYS/test-irrelevant since this
    // is only setting the write-enable for the copy-clear.
    GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);

    // Port specific: boot-splash -> game handoff bridge (see
    // SplashBootScreen.h and the #48 fix note). Between the boot-time splash
    // draw (mainWii.cpp) and the game's own first real DrawStartFade() call,
    // the Splash->Game task transition + frontend load stall would otherwise
    // present several frames of black-cleared EFB. Until DrawStartFade
    // reports in via NotifyGameSplashDrew(), redraw the same embedded logo
    // quad here, AFTER the game's own (currently empty) draw and BEFORE the
    // copy below, so the presented frame is the logo instead of black.
    // DrawSplashBootQuad() is fully self-contained (resets viewport/
    // projection/vtxdesc/TEV/zmode/cull), so clobbering whatever GX state
    // the game's draw left behind is fine -- next frame's BeginFrame resets
    // it again. The frame DrawStartFade first actually draws, the flag is
    // already set (DrawStartFade runs earlier in GameDraw, before
    // SwapBuffers), so this skips the overlay and releases the transient
    // buffer -- no double-draw, no pop, since both draw the identical quad.
    if (!fn::wii::GameSplashDrew()) {
        fn::wii::DrawSplashBootQuad();
    } else {
        fn::wii::ReleaseSplashBoot();
    }

    // GX_CopyDisp clears (via GX_SetCopyClear, set in glClearColor) and copies
    // the EFB to the external framebuffer for scan-out. On the boot pass no
    // geometry was drawn, so this presents the clear colour -- exactly the
    // "cleared screen = success" goal.
    GX_CopyDisp(xfb, GX_TRUE);
    GX_Flush();

    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode && ((GXRModeObj*)rmode)->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }

    // Advance to the other XFB for the next frame.
    fn::wii::FlipXFB();

    m_bSwapPending = m_bSwapPending ? 0 : 1;
}

} // namespace Mortar

#endif // FRUIT_PLATFORM_WII
