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
