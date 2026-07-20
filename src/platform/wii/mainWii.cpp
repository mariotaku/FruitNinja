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
#include <ogc/stm.h>
#include <malloc.h>
#include <cstring>
#include <sys/stat.h>
#include <errno.h>

#include "Game.h"
#include "config.h"
#include "platform/FixedStepDriver.h"
#include "platform/wii/WiiVideo.h"
#include "platform/wii/InputTranslatorWii.h"
#include "platform/wii/SplashBootScreen.h"
#include "platform/wii/SoundManagerWii.h"
#include "platform/wii/BlockLoader.h"  // LogHeapUsage (task #36/#59 residency diagnostic)
#include "game/SettingsSave.h"
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
bool        s_gameSplashDrew = false;
}

namespace fn {
namespace wii {

void* CurrentXFB() { return s_xfb[s_xfbActive]; }
void  FlipXFB()    { s_xfbActive ^= 1; }
void* VideoMode()  { return s_rmode; }

void NotifyGameSplashDrew() { s_gameSplashDrew = true; }
bool GameSplashDrew()       { return s_gameSplashDrew; }

} // namespace wii
} // namespace fn

// ---------------------------------------------------------------------------

static Game g_game;
static fn::FixedStepDriver g_driver;

// Port specific: power/reset callbacks fire in system/interrupt context where
// file IO is unsafe (SaveOnExit/SaveSettings do tinyxml2 disk writes). So the
// callbacks only set a flag; the main loop polls it each frame and does the
// actual save + shutdown from normal task context.
static volatile bool g_PowerOff = false;
static volatile bool g_Reset = false;

static void OnPowerCallback(void) { g_PowerOff = true; }
static void OnResetCallback(u32 irq, void* ctx) { (void)irq; (void)ctx; g_Reset = true; }

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

    // Port specific: draw the HB logo boot splash's frame-0 appearance right
    // now, straight to the XFB via GX_CopyDisp, so there is no black screen
    // during SD mount / asset staging. GX is fully initialised at this point
    // (WiiGxInit already ran a GX_CopyDisp). PrepareSplashBoot() retains the
    // inflated texture rather than freeing it: DisplayManagerWii::SwapBuffers
    // keeps redrawing this same quad every frame (bridging the Splash-task ->
    // Game-task load-stall gap) until the game's own first real
    // DrawStartFade() call reports in via NotifyGameSplashDrew(), at which
    // point it releases the transient buffer. See SplashBootScreen.h.
    fn::wii::PrepareSplashBoot();
    fn::wii::DrawSplashBootQuad();
    GX_CopyDisp(s_xfb[s_xfbActive], GX_TRUE);
    GX_DrawDone();
    VIDEO_SetNextFramebuffer(s_xfb[s_xfbActive]);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    // libfat: mounts sd:/ and usb:/ so FileSystemWii can read assets.
    if (!fatInitDefault()) {
        LOG_ERROR("mainWii", "fatInitDefault() failed -- no SD/USB filesystem");
    }

    // Port specific: create the writable save dir (FN_SAVE_DIR) if it doesn't
    // exist yet -- unlike FN_DATA_DIR (assets), it isn't part of the staged
    // deploy. errno==EEXIST is the expected steady-state case, not an error.
    if (mkdir(FN_SAVE_DIR, 0777) != 0 && errno != EEXIST) {
        LOG_ERROR("mainWii", "mkdir('%s') failed (errno=%d) -- saves won't persist",
                   FN_SAVE_DIR, errno);
    }

    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    // WPAD_SetVRes is deliberately NOT called here: Wiimotes connect
    // asynchronously over Bluetooth, so at init __wpads[chan] is still NULL and
    // libogc silently drops the vres (unlike the data format, it is never
    // re-applied on connect). It is re-issued every frame in the loop below.

    // Port specific: no binary equivalent. Wii Home Button Menu (power button
    // on the console/remote, or the reset button) requests a clean shutdown;
    // register here so the flags above are live before the main loop starts.
    SYS_SetPowerCallback(OnPowerCallback);
    SYS_SetResetCallback(OnResetCallback);

    LOG_INFO("mainWii", "VIDEO/GX/WPAD/fat initialised (%dx%d)",
             (int)s_rmode->fbWidth, (int)s_rmode->efbHeight);
    fn::wii::LogHeapUsage("pre-init");

    if (!g_game.init(NULL, NULL)) {
        LOG_ERROR("mainWii", "Failed to init game");
        return 1;
    }

    static InputTranslatorWii inputTranslator;
    inputTranslator.Init();

    // Monotonic clock: gettime() ticks -> ms via ticks_to_millisecs.
    u64 lastTicks = gettime();

    while (g_game.running) {
        // Port specific: no binary equivalent. Power/reset requests are
        // latched by the interrupt-context callbacks above; handle them here
        // in normal task context where file IO is safe. Save BEFORE the
        // shutdown action -- neither STM_ShutdownToStandby nor
        // SYS_ResetSystem returns.
        if (g_PowerOff || g_Reset) {
            g_game.SaveOnExit();
            SaveSettings();
            if (g_PowerOff) {
                STM_ShutdownToStandby();
            } else {
                SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
            }
        }

        WPAD_ScanPads();

        // Re-apply the IR virtual resolution every frame. libogc drops
        // WPAD_SetVRes for any channel whose remote hasn't connected yet and
        // never re-applies it on connect, so a one-shot init call is a no-op
        // and each remote keeps the wiiuse default 560x420 -- which, divided by
        // the 640x480 basis below, shrinks the pointer to the top-left 87.5% of
        // the screen (offset) and drives IR toward the FOV edge (roll-skew).
        // Re-issuing here (cheap, idempotent) makes ir.x/ir.y report in the
        // (fbWidth, xfbHeight) space the normalization below assumes, the moment
        // each remote is up. MUST match the nx/ny denominators used below.
        WPAD_SetVRes(WPAD_CHAN_ALL, s_rmode->fbWidth, s_rmode->xfbHeight);

        // HOME on any remote quits (host-only affordance; no binary equiv).
        //
        // Port specific: each Wiimote behaves exactly like the SDL backend's
        // MOUSE (InputTranslatorSDL is the reference implementation). The two
        // raw signals -- A held and IR validity -- are passed SEPARATELY to
        // DrainWiimoteIR, which composes them per role/mode (see
        // InputTranslatorWii.h for the full model):
        //   - Role 1 "press finger" (channel N, Mortar::Touch slots, both
        //     modes): A held + valid IR = finger down; IR movement while held
        //     = finger motion. This is press-to-slice (motion mode OFF) and
        //     the menu/widget click (A = the mouse button) in BOTH modes.
        //   - Role 2 "hover blade" (channel 12+N, motion mode ON only): the
        //     blade follows the IR dot with no button; cuts are speed-gated
        //     (FN::g_MotionSpeedThreshold); A is INVERTED like the SDL mouse
        //     button -- pressing lifts the blade, releasing re-presses it;
        //     losing the IR dot releases it (pointing away = blade gone).
        for (int chan = 0; chan < InputTranslatorWii::MAX_REMOTES; ++chan) {
            u32 down = WPAD_ButtonsDown(chan);
            if (down & WPAD_BUTTON_HOME) {
                g_game.running = false;
            }
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
            bool aHeld = (WPAD_ButtonsHeld(chan) & WPAD_BUTTON_A) != 0;
            inputTranslator.DrainWiimoteIR(chan, ir.valid != 0, aHeld, nx, ny);
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

        // Port specific: no binary counterpart. Refills the ASND music
        // double-buffer from disk -- MUST run on the main thread, never the
        // audio interrupt (see SoundManagerWii.h / SoundManagerWii.cpp's
        // "Music streaming" section for the full rationale). No-op when no
        // music is currently playing.
        fn::wii::AudioStreamPump();

        g_game.renderFrame((float)g_driver.alpha(), steps);
    }

    g_game.shutdown();
    WPAD_Shutdown();
    return 0;
}

#endif // FRUIT_PLATFORM_WII
