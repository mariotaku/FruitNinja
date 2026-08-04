// test_slash_input -- end-to-end guard for the per-finger blade input chain.
//
// Drives the REAL chain, exactly as the game does:
//   SDL events -> InputTranslatorSDL::DrainSDLEvent   (latch into Touch's ring)
//   InputTranslatorSDL::DispatchForSimTick            (Touch::Update(0) drain)
//   Mortar::InputManager::Update                      (v1.6.1 @0x00243838)
//     -> InputDeviceBada::Update @0x00242f40
//        -> Touch::SendIndividualTouchCallbacks @0x00242bc4
//           -> InputDevice::AxisEvent / ButtonPressed -> CheckActions
//              -> InputActionMapper::ProcessEvent
//                 -> GameTaskInput.cpp's PointerMoveCallback / TouchDownCallback
//                    -> g_pSlashEntities[n]
//   SlashEntity::Update                               (decays m_BladeActive)
//
// The mappers come from Input/Input.txt via InputManager::LoadConfigFile
// @0x002442fc, so the fixture registers a FileSystem_Direct first.
//
// SEMANTICS PINNED:
//   * Per slot and per tick the poll raises TouchAxisX, TouchAxisY, then the
//     mask-2 Touch<n> "down" -- in that order. The blade therefore knows the
//     current finger position BEFORE TouchDown runs, which is what makes a new
//     stroke seed at the press point without any position on the button event.
//   * A held finger re-arms m_BladeActive every tick. A released finger raises
//     no mask-2 event, so one tick without a TouchDown is enough for
//     SlashEntity::Update's `(old << 1) & 2` shift to decay the latch to 0 --
//     and the NEXT press then Resets. That decay is the binary's only
//     new-stroke trigger; there is no press-edge flag.
//   * Consecutive taps therefore never bridge: each tap Resets and re-seeds a
//     degenerate stroke at its own position.
//   * A swipe tracks: points are appended and the stroke starts at the press
//     position.
//   * A fast flick whose DOWN and first MOTION drain within the same sim tick
//     still registers on the press frame (the ring holds both; the drain
//     applies them in order and the slot's live position is the moved one).
//
// FIXTURE NOTE: every test wires ALL 16 action channels to its one SlashEntity.
// Touch hands out states1 slots from a rotating cursor that no reset clears, so
// the Nth press of the process lands on slot N-1, not slot 0. See WireBlade.
//
// FIXTURE NOTE: SlashEntity::Update reaches PlaySwipe (v1.6.1
// SlashEntity::PlaySwipe @0x001e8550) once |m_BladeDir| > 35, and PlaySwipe
// derefs game_work.mHud / mGameSound unguarded, faithful to the binary, which
// relies on GameInit having created them. This fixture supplies both globals
// the way boot does -- same pattern as test_slash_collision.cpp.
//
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#define FN_TEST 1
#include "platform/InputTranslatorSDL.h"
#include "input/Touch.h"
#include "input/InputManager.h"
#include "input/InputEvent.h"
#include "util/StringHash.h"
#include "entities/SlashEntity.h"
#include "game/GameTaskInput.h"
#include "game/GameWork.h"
#include "game/FruitSaveData.h"
#include "hud/HUD.h"
#include "engine/audio/GameSound.h"
#include "engine/asset/FileManager.h"
#include "engine/asset/FileSystem_Direct.h"
#include "config.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_NEAR(a, b, eps) \
    do { \
        float _a = (float)(a); float _b = (float)(b); \
        float _d = _a - _b; if (_d < 0.0f) _d = -_d; \
        if (_d > (float)(eps)) { \
            std::printf("FAIL (%s:%d): |%.4f - %.4f| = %.4f > %.4f\n", \
                        __FILE__, __LINE__, _a, _b, _d, (float)(eps)); \
            ::exit(1); \
        } \
    } while(0)

static const float SIM_DT = 1.0f / 60.0f;

// --- synthetic SDL events (normalized coords; translator maps to game coords:
//     gx = nx*480 - 240, gy = 160 - ny*320) ---

static SDL_Event MakeFingerDown(SDL_FingerID fid, float nx, float ny) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type             = SDL_FINGERDOWN;
    ev.tfinger.fingerId = fid;
    ev.tfinger.x        = nx;
    ev.tfinger.y        = ny;
    ev.tfinger.pressure = 1.0f;
    return ev;
}

static SDL_Event MakeFingerMotion(SDL_FingerID fid, float nx, float ny) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type             = SDL_FINGERMOTION;
    ev.tfinger.fingerId = fid;
    ev.tfinger.x        = nx;
    ev.tfinger.y        = ny;
    ev.tfinger.dx       = 0.01f;
    ev.tfinger.dy       = 0.01f;
    return ev;
}

static SDL_Event MakeFingerUp(SDL_FingerID fid, float nx, float ny) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type             = SDL_FINGERUP;
    ev.tfinger.fingerId = fid;
    ev.tfinger.x        = nx;
    ev.tfinger.y        = ny;
    return ev;
}

// Reset the Mortar::Touch singleton between tests.
static void ResetTouch() {
    Mortar::Touch& t = Mortar::Touch::GetInstance();
    t.Update(0.0f);
    t.Clear();
    for (int i = 0; i < Mortar::Touch::MAX_SLOTS; ++i) {
        t.states1[i].extId   = 0;
        t.states1[i].touchId = 0;
        t.states1[i].phase   = 1;
        t.states1[i].currX   = 0;
        t.states1[i].currY   = 0;
        t.states1[i].prevX   = 0;
        t.states1[i].prevY   = 0;
    }
}

// One sim tick in game order: drain the ring, run the device poll (which raises
// every action event), then update the blade (which decays m_BladeActive).
static void SimTick(InputTranslatorSDL& tr, Mortar::InputManager& im, SlashEntity& se) {
    tr.DispatchForSimTick();
    im.Update(0.0f);
    se.Update(SIM_DT);
}

// One 2-tick tap: DOWN, sim tick (press frame), UP, sim tick (release frame).
static void Tap(InputTranslatorSDL& tr, Mortar::InputManager& im, SlashEntity& se,
                SDL_FingerID fid, float nx, float ny) {
    SDL_Event d = MakeFingerDown(fid, nx, ny);
    tr.DrainSDLEvent(d, NULL);
    SimTick(tr, im, se);
    SDL_Event u = MakeFingerUp(fid, nx, ny);
    tr.DrainSDLEvent(u, NULL);
    SimTick(tr, im, se);
}

// ---------------------------------------------------------------------------
// Event-level recorders: pin which action events fire, and in what order.
// Return false = pass-through (CheckActions discards the result anyway).
// ---------------------------------------------------------------------------
static int g_cntDown, g_cntMoveX, g_cntMoveY, g_cntUp;
static int g_seq;                 // monotonic dispatch counter
static int g_seqMoveX, g_seqMoveY, g_seqDown;
static bool RecDown (InputEvent*) { ++g_cntDown;  g_seqDown  = ++g_seq; return false; }
static bool RecMoveX(InputEvent*) { ++g_cntMoveX; g_seqMoveX = ++g_seq; return false; }
static bool RecMoveY(InputEvent*) { ++g_cntMoveY; g_seqMoveY = ++g_seq; return false; }
static bool RecUp   (InputEvent*) { ++g_cntUp;    return false; }
static void ResetCounters() {
    g_cntDown = g_cntMoveX = g_cntMoveY = g_cntUp = 0;
    g_seq = g_seqMoveX = g_seqMoveY = g_seqDown = 0;
}

// ---------------------------------------------------------------------------
// Boot the globals the chain derefs unguarded, plus the filesystem
// LoadConfigFile reads Input/Input.txt through (mirrors GameInitialise Step 3).
// ---------------------------------------------------------------------------
static void BootFixture() {
    Mortar::FileSystem_Direct* fs = new Mortar::FileSystem_Direct();
    fs->Initialise(FN_DATA_DIR, /*writable=*/false);
    FileManager::GetInstance().AddSystem(fs, /*id=*/0, /*priority=*/0);

    game_work.mHud       = new HUD();
    game_work.mGameSound = new GameSound();
    static FruitSaveData s_saveData;
    game_work.m_SaveData = &s_saveData;
}

// Build the mapper list the way GameTaskInitInput @0x001cae0c does: parse
// Input/Input.txt FIRST. Nothing binds without it -- InputDevice::
// RegisterInputCallback @0x002759f4 walks the mapper list and never inserts on
// a miss.
static void LoadMappers(Mortar::InputManager& im) {
    if (!im.LoadConfigFile("Input/Input.txt")) {
        std::printf("FAIL: LoadConfigFile(\"Input/Input.txt\") returned 0 "
                    "(data dir = %s)\n", FN_DATA_DIR);
        ::exit(1);
    }
}

// ---------------------------------------------------------------------------
// Wire the blade under test the way GameTaskInitInput @0x001cae0c does: the
// free callbacks in GameTaskInput.cpp own the per-finger actions and dispatch
// into g_pSlashEntities[n]. SlashEntity itself subscribes to nothing.
//
// ALL 16 channels are wired to the ONE entity, and that is load-bearing, not
// laziness. The action channel is the Mortar::Touch::states1 SLOT the finger
// claimed, and Touch::___UpdateInternal @0x00242868 hands out slots from a
// ROTATING cursor (binary BSS @GOT+0x80798; the port's file-static s_slotCursor
// in Touch.cpp). That cursor is process-global and survives Touch::Clear -- the
// binary never resets it either -- so consecutive presses land on slot 0, then
// 1, then 2, ... even for the same SDL finger id and even across ResetTouch().
// Wiring channel 0 alone would send every press after the first to an unbound
// mapper and the blade would see nothing at all.
//
// Funnelling all channels into one entity is also what these tests want to
// measure: it puts consecutive strokes on the SAME blade, which is the only
// arrangement in which a stroke could bridge from the previous one's tail.
// ---------------------------------------------------------------------------
static void WireBlade(Mortar::InputManager& im, SlashEntity& se, int channel) {
    se.Init(channel);

    char buf[20];
    for (int i = 0; i < 16; ++i) {
        g_pSlashEntities[i] = &se;
        sprintf(buf, "TouchMove_X%d", i);
        im.RegisterInputCallback(StringHash(buf), PointerMoveCallback);
        sprintf(buf, "TouchMove_Y%d", i);
        im.RegisterInputCallback(StringHash(buf), PointerMoveCallback);
        sprintf(buf, "TouchDown_%d", i);
        im.RegisterInputCallback(StringHash(buf), TouchDownCallback);
    }
}

// The SlashEntity is a stack local in every test below -- drop the global
// before it dies or the next test dispatches into a dangling pointer.
static void UnwireBlade(int /*channel*/) {
    for (int i = 0; i < 16; ++i) g_pSlashEntities[i] = NULL;
}

// ---------------------------------------------------------------------------
// Event order: per active slot the poll raises TouchAxisX, TouchAxisY, then the
// mask-2 Touch<n> "down" -- every tick, press frame included. That ordering is
// what lets TouchDown seed a new stroke at the press position without any
// port-side position side channel on the button event.
// ---------------------------------------------------------------------------
static void test_axis_events_precede_down() {
    printf("  test_axis_events_precede_down...\n");
    ResetTouch();

    Mortar::InputManager im;
    im.Init(0);
    LoadMappers(im);
    // Every channel, for the rotating-slot reason spelled out on WireBlade:
    // the finger does not necessarily land on slot 0.
    {
        char buf[24];
        for (int i = 0; i < 16; ++i) {
            sprintf(buf, "TouchDown_%d", i);
            im.RegisterInputCallback(StringHash(buf), RecDown);
            sprintf(buf, "TouchMove_X%d", i);
            im.RegisterInputCallback(StringHash(buf), RecMoveX);
            sprintf(buf, "TouchMove_Y%d", i);
            im.RegisterInputCallback(StringHash(buf), RecMoveY);
            sprintf(buf, "TouchReleased_%d", i);
            im.RegisterInputCallback(StringHash(buf), RecUp);
        }
    }

    InputTranslatorSDL tr;
    tr.Init();
    SlashEntity se;
    se.Init(0);

    // --- press frame ---
    ResetCounters();
    SDL_Event d = MakeFingerDown((SDL_FingerID)1, 0.5f, 0.5f);
    tr.DrainSDLEvent(d, NULL);
    SimTick(tr, im, se);
    printf("    press frame: moveX=%d moveY=%d down=%d (seq %d/%d/%d)\n",
           g_cntMoveX, g_cntMoveY, g_cntDown, g_seqMoveX, g_seqMoveY, g_seqDown);
    CHECK(g_cntMoveX == 1);
    CHECK(g_cntMoveY == 1);
    CHECK(g_cntDown  == 1);
    CHECK(g_seqMoveX < g_seqDown);
    CHECK(g_seqMoveY < g_seqDown);

    // --- held frame: same three events again, no new SDL input needed ---
    ResetCounters();
    SimTick(tr, im, se);
    printf("    held frame:  moveX=%d moveY=%d down=%d\n",
           g_cntMoveX, g_cntMoveY, g_cntDown);
    CHECK(g_cntMoveX == 1);
    CHECK(g_cntMoveY == 1);
    CHECK(g_cntDown  == 1);

    // --- release frame: no mask-2 down; the "up" action fires instead ---
    ResetCounters();
    SDL_Event u = MakeFingerUp((SDL_FingerID)1, 0.5f, 0.5f);
    tr.DrainSDLEvent(u, NULL);
    SimTick(tr, im, se);
    printf("    release frame: down=%d up=%d\n", g_cntDown, g_cntUp);
    CHECK(g_cntDown == 0);
    CHECK(g_cntUp   >= 1);

    printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// 3 discrete taps: each one Resets (the release tick decays m_BladeActive to 0)
// and re-seeds a degenerate stroke at the tap position -- no segment ever spans
// two taps.
// ---------------------------------------------------------------------------
static void test_taps_do_not_bridge() {
    printf("  test_taps_do_not_bridge...\n");
    ResetTouch();

    Mortar::InputManager im;
    im.Init(0);
    LoadMappers(im);
    InputTranslatorSDL tr;
    tr.Init();
    SlashEntity se;
    WireBlade(im, se, 0);

    CHECK(se.TestGetBombHitEdge() == 0);

    const float taps[3][2] = {
        { 0.2f, 0.2f },   // A = (-144,  96)
        { 0.5f, 0.5f },   // B = (   0,   0)
        { 0.8f, 0.8f },   // C = ( 144, -96)
    };
    for (int i = 0; i < 3; ++i) {
        Tap(tr, im, se, (SDL_FingerID)(10 + i), taps[i][0], taps[i][1]);

        const float gx = taps[i][0] * 480.0f - 240.0f;
        const float gy = 160.0f - taps[i][1] * 320.0f;

        // A bridge interpolates points across the ~277-unit tap-to-tap span
        // (one pair every 64 units) -> pointCount >= 6.
        printf("    tap %d: pointCount=%d tail=(%.1f,%.1f)\n",
               i, se.GetPointCount(), se.GetTailPos().x, se.GetTailPos().y);
        CHECK(se.GetPointCount() <= 2);

        // Degenerate stroke AT the tap position: head == tail == tap point.
        CHECK_NEAR(se.GetTailPos().x, gx, 1.0f);
        CHECK_NEAR(se.GetTailPos().y, gy, 1.0f);
        CHECK_NEAR(se.GetHeadPos().x, se.GetTailPos().x, 0.01f);
        CHECK_NEAR(se.GetHeadPos().y, se.GetTailPos().y, 0.01f);
    }

    UnwireBlade(0);
    printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// A real swipe tracks, and the stroke STARTS at the press position (not at the
// previous stroke's stale tail).
// ---------------------------------------------------------------------------
static void test_swipe_tracks_and_starts_at_press() {
    printf("  test_swipe_tracks_and_starts_at_press...\n");
    ResetTouch();

    Mortar::InputManager im;
    im.Init(0);
    LoadMappers(im);
    InputTranslatorSDL tr;
    tr.Init();
    SlashEntity se;
    WireBlade(im, se, 0);

    // Pollute the blade with a tap at D = (0, -128) so a stale-tail bug would
    // be visible as a vertex far off the swipe row.
    Tap(tr, im, se, (SDL_FingerID)5, 0.5f, 0.9f);

    // Swipe: press at (-192, 0), drag right.
    SDL_Event d = MakeFingerDown((SDL_FingerID)6, 0.1f, 0.5f);
    tr.DrainSDLEvent(d, NULL);
    SimTick(tr, im, se);
    printf("    press frame: pointCount=%d tail=(%.1f,%.1f)\n",
           se.GetPointCount(), se.GetTailPos().x, se.GetTailPos().y);
    CHECK_NEAR(se.GetTailPos().x, -192.0f, 1.0f);
    CHECK_NEAR(se.GetTailPos().y,    0.0f, 1.0f);

    SDL_Event m1 = MakeFingerMotion((SDL_FingerID)6, 0.45f, 0.5f);
    tr.DrainSDLEvent(m1, NULL);
    SimTick(tr, im, se);
    const int afterM1 = se.GetPointCount();
    printf("    after motion 1: pointCount=%d tail=(%.1f,%.1f)\n",
           afterM1, se.GetTailPos().x, se.GetTailPos().y);
    CHECK(afterM1 > 2);                      // blade tracked the drag
    CHECK_NEAR(se.GetTailPos().x, -24.0f, 1.0f);

    SDL_Event m2 = MakeFingerMotion((SDL_FingerID)6, 0.7f, 0.5f);
    tr.DrainSDLEvent(m2, NULL);
    SimTick(tr, im, se);
    const int afterM2 = se.GetPointCount();
    printf("    after motion 2: pointCount=%d tail=(%.1f,%.1f)\n",
           afterM2, se.GetTailPos().x, se.GetTailPos().y);
    CHECK(afterM2 > afterM1);
    CHECK_NEAR(se.GetTailPos().x, 96.0f, 1.0f);
    CHECK_NEAR(se.GetTailPos().y,  0.0f, 1.0f);

    // No vertex anywhere near the polluting tap D (y=-128): the whole stroke
    // sits on the y=0 swipe row (+/- ribbon half-width).
    for (int i = 0; i < afterM2; ++i) {
        const float vy = se.GetVertexY(i);
        CHECK(vy > -60.0f && vy < 60.0f);
    }

    SDL_Event u = MakeFingerUp((SDL_FingerID)6, 0.7f, 0.5f);
    tr.DrainSDLEvent(u, NULL);
    SimTick(tr, im, se);

    UnwireBlade(0);
    printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// Fast flick: DOWN and first MOTION drain within the SAME sim tick (120Hz /
// same-frame flick). Both edges sit in the Touch ring and the drain applies
// them in order, so the press frame already sees the moved position.
// ---------------------------------------------------------------------------
static void test_fast_flick_same_tick_registers() {
    printf("  test_fast_flick_same_tick_registers...\n");
    ResetTouch();

    Mortar::InputManager im;
    im.Init(0);
    LoadMappers(im);
    InputTranslatorSDL tr;
    tr.Init();
    SlashEntity se;
    WireBlade(im, se, 0);

    SDL_Event d = MakeFingerDown((SDL_FingerID)9, 0.3f, 0.5f);   // (-96, 0)
    SDL_Event m = MakeFingerMotion((SDL_FingerID)9, 0.6f, 0.5f); // ( 48, 0)
    tr.DrainSDLEvent(d, NULL);
    tr.DrainSDLEvent(m, NULL);
    SimTick(tr, im, se);                     // single press frame sees both

    printf("    flick press frame: pointCount=%d tail=(%.1f,%.1f)\n",
           se.GetPointCount(), se.GetTailPos().x, se.GetTailPos().y);
    CHECK(se.GetPointCount() >= 2);          // blade seeded -- flick not lost
    CHECK_NEAR(se.GetTailPos().x, 48.0f, 1.0f);
    CHECK_NEAR(se.GetTailPos().y,  0.0f, 1.0f);

    SDL_Event u = MakeFingerUp((SDL_FingerID)9, 0.6f, 0.5f);
    tr.DrainSDLEvent(u, NULL);
    SimTick(tr, im, se);

    UnwireBlade(0);
    printf("  PASS\n");
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    // SDL event types only -- no video, no audio, no GL context.
    if (SDL_Init(SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init(EVENTS) failed: %s\n", SDL_GetError());
        return 1;
    }

    printf("test_slash_input: start\n");

    BootFixture();

    test_axis_events_precede_down();
    test_taps_do_not_bridge();
    test_swipe_tracks_and_starts_at_press();
    test_fast_flick_same_tick_registers();

    printf("test_slash_input: PASS\n");

    SDL_Quit();
    return 0;
}
