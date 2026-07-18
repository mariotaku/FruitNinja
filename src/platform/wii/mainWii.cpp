// Port specific: Wii entry point (devkitPPC / libogc).
//
// Boots VIDEO + GX + WPAD + libfat, constructs the Game, and drives a
// fixed-timestep loop (fn::FixedStepDriver) of pollInput -> stepUpdate(1/60)
// -> renderFrame -> swap. HOME quits. Input comes from up to 4 Wiimote IR
// pointers via InputTranslatorWii (see InputTranslatorWii.h).
//
// PASS 1 SCOPE: boot to a cleared screen. The GX draw shim (gl_funcsWii.cpp)
// no-ops actual geometry this pass, so the presented frame is the clear
// colour. GX/VIDEO/WPAD/fat init here is real so Pass 2's draws land.
//
// Only compiled when FRUIT_PLATFORM_WII is set.
#ifdef FRUIT_PLATFORM_WII

#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <cstring>

#include "Game.h"
#include "platform/FixedStepDriver.h"
#include "platform/wii/WiiVideo.h"
#include "platform/wii/InputTranslatorWii.h"
#include "debug/Logger.h"

// ---------------------------------------------------------------------------
// Shared VIDEO/GX state (WiiVideo.h seam consumed by DisplayManagerWii.cpp).
// ---------------------------------------------------------------------------
namespace {
GXRModeObj* s_rmode      = 0;
void*       s_xfb[2]     = { 0, 0 };
int         s_xfbActive  = 0;
void*       s_gxFifo     = 0;
const u32   kFifoSize    = 256 * 1024;
}

namespace fn {
namespace wii {

void* CurrentXFB() { return s_xfb[s_xfbActive]; }
void  FlipXFB()    { s_xfbActive ^= 1; }
void* VideoMode()  { return s_rmode; }

} // namespace wii
} // namespace fn

// ---------------------------------------------------------------------------

static Game g_game;
static fn::FixedStepDriver g_driver;

static void WiiVideoInit() {
    VIDEO_Init();
    s_rmode = VIDEO_GetPreferredMode(NULL);

    s_xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(s_rmode));
    s_xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(s_rmode));
    s_xfbActive = 0;

    VIDEO_Configure(s_rmode);
    VIDEO_SetNextFramebuffer(s_xfb[s_xfbActive]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (s_rmode->viTVMode & VI_NON_INTERLACE) {
        VIDEO_WaitVSync();
    }
}

static void WiiGxInit() {
    s_gxFifo = MEM_K0_TO_K1(memalign(32, kFifoSize));
    memset(s_gxFifo, 0, kFifoSize);
    GX_Init(s_gxFifo, kFifoSize);

    GXColor bg = { 0, 0, 0, 255 };
    GX_SetCopyClear(bg, GX_MAX_Z24);

    // EFB / copy setup from the chosen video mode.
    GX_SetViewport(0, 0, s_rmode->fbWidth, s_rmode->efbHeight, 0, 1);
    f32 yscale = GX_GetYScaleFactor(s_rmode->efbHeight, s_rmode->xfbHeight);
    u32 xfbHeight = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, s_rmode->fbWidth, s_rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, s_rmode->fbWidth, s_rmode->efbHeight);
    GX_SetDispCopyDst(s_rmode->fbWidth, xfbHeight);
    GX_SetCopyFilter(s_rmode->aa, s_rmode->sample_pattern, GX_TRUE, s_rmode->vfilter);
    GX_SetFieldMode(s_rmode->field_rendering,
                    ((s_rmode->viHeight == 2 * s_rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_CopyDisp(s_xfb[s_xfbActive], GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_InvalidateTexAll();
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // Route stdout/stderr (the LoggerWii printf backend) to the OS report
    // channel so LOG_* output shows up in Dolphin's log / USB Gecko for
    // bring-up debugging. Harmless on real hardware.
    SYS_STDIO_Report(true);

    WiiVideoInit();
    WiiGxInit();

    // libfat: mounts sd:/ and usb:/ so FileSystemWii can read assets.
    if (!fatInitDefault()) {
        LOG_ERROR("mainWii", "fatInitDefault() failed -- no SD/USB filesystem");
    }

    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, s_rmode->fbWidth, s_rmode->xfbHeight);

    LOG_INFO("mainWii", "VIDEO/GX/WPAD/fat initialised (%dx%d)",
             (int)s_rmode->fbWidth, (int)s_rmode->efbHeight);

    if (!g_game.init(NULL, NULL)) {
        LOG_ERROR("mainWii", "Failed to init game");
        return 1;
    }

    static InputTranslatorWii inputTranslator;
    inputTranslator.Init();

    // Monotonic clock: gettime() ticks -> ms via ticks_to_millisecs.
    u64 lastTicks = gettime();

    while (g_game.running) {
        WPAD_ScanPads();

        // HOME on any remote quits (host-only affordance; no binary equiv).
        //
        // Port specific: the IR pointer itself IS the finger (matches
        // InputTranslatorWii.h's "IR pointer -> Touch finger channel" model,
        // the direct analogue of a touch being present/absent on the SDL
        // backend). No A/B-button gating is used -- pointing the remote at
        // the sensor bar presses the blade, pointing away releases it, same
        // as a finger touching/lifting off a touchscreen. ir.valid is WPAD's
        // own "is the dot currently visible" signal, so a remote pointed off
        // the sensor bar naturally reports no press; there is no "last known
        // position" fallback to preserve since the IR read already carries
        // that semantic (ir.valid false leaves ir.x/y at their last value,
        // but DrainWiimoteIR only acts on it while irValid is true).
        for (int chan = 0; chan < InputTranslatorWii::MAX_REMOTES; ++chan) {
            u32 down = WPAD_ButtonsDown(chan);
            if (down & WPAD_BUTTON_HOME) {
                g_game.running = false;
            }
            // Feed each remote's IR pointer into its fixed finger channel.
            // WPAD_SetVRes(WPAD_CHAN_ALL, fbWidth, xfbHeight) above makes
            // ir.x/ir.y report in that same fb pixel space; normalize here
            // to [0,1] top-left/y-down (the same convention SDL's normalized
            // touch coords use) before handing off to DrainWiimoteIR, which
            // forwards to Layout::TouchToGame for the actual game-coord
            // transform (see InputTranslatorWii.cpp).
            ir_t ir;
            WPAD_IR(chan, &ir);
            float nx = 0.0f, ny = 0.0f;
            if (ir.valid && s_rmode->fbWidth > 0 && s_rmode->xfbHeight > 0) {
                nx = ir.x / (float)s_rmode->fbWidth;
                ny = ir.y / (float)s_rmode->xfbHeight;
            }
            inputTranslator.DrainWiimoteIR(chan, ir.valid != 0, nx, ny);
        }

        g_game.pollInput();
        if (!g_game.running) {
            break;
        }

        u64 nowTicks = gettime();
        double elapsedMs = (double)ticks_to_millisecs(nowTicks - lastTicks);
        lastTicks = nowTicks;

        int steps = g_driver.advance(elapsedMs);
        for (int i = 0; i < steps && g_game.running; ++i) {
            inputTranslator.DispatchForSimTick();
            g_game.stepUpdate();
        }

        // Port specific: no binary counterpart. Per-PRESENT UI tick (see
        // Game.h tickRealtimeUi), matching GameSDL.cpp's run() loop -- reuses
        // this loop's own wall-clock elapsedMs (gettime()/ticks_to_millisecs
        // above), converted to seconds, rather than a second clock source.
        g_game.tickRealtimeUi((float)(elapsedMs / 1000.0));

        g_game.renderFrame((float)g_driver.alpha(), steps);
    }

    g_game.shutdown();
    WPAD_Shutdown();
    return 0;
}

#endif // FRUIT_PLATFORM_WII
