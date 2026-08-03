// test_slash_input -- press-vs-motion blade-gate regression guard (tap-bridge fix).
//
// SEMANTICS PINNED (v1.6.1: a tap alone never moves the blade):
//   * A stationary TAP (FINGERDOWN then FINGERUP, no FINGERMOTION) emits
//     TouchScreen + TouchDown_N (+ TouchUp_N) and NO TouchMove_XN/YN.
//     The blade therefore never receives a tap's position, so consecutive
//     taps can never bridge into a slash -- in BOTH bomb-latch states:
//       - m_BombHitEdge == 0: each press-edge Reset() re-seeds a degenerate
//         stroke at the press position (TouchDown syncs m_RawTouchPos from
//         the event on the stroke-reset branch); nothing spans tap-to-tap.
//       - m_BombHitEdge != 0 (post-bomb game-over): Reset() is blocked AND no
//         position reaches the blade (no TouchMove, no press sync), so
//         OnTouchActive stays in its below-threshold skip path -- point count
//         does not change at all.
//   * A SWIPE (FINGERDOWN then FINGERMOTION...) DOES track: TouchMove fires on
//     motion ticks, points are appended, and the stroke STARTS at the press
//     position (not at the previous stroke's stale position).
//   * A fast flick whose DOWN and first MOTION drain within the same sim tick
//     still registers on the press frame (motionSinceDown already true).
//
// OLD BUGGY BEHAVIOUR THIS MUST FAIL ON: DispatchForSimTick gated the press
// frame TouchMove on states1 currX/Y != prevX/Y. prevX/Y holds the PREVIOUS
// stroke's position on a fresh press, so every tap at a new location emitted
// a TouchMove -> with the bomb latch set, TouchDown appended a segment from
// the old tail to the tap point -> taps bridged into slashes.
//
// Drives the REAL chain: SDL events -> InputTranslatorSDL::DrainSDLEvent ->
// DispatchForSimTick -> Mortar::InputManager -> InputDeviceBada bindings ->
// GameTaskInput.cpp's TouchDownCallback / PointerMoveCallback ->
// g_pSlashEntities[0]. SDL_INIT_EVENTS only: no GL, no window, no audio
// (SlashEntity Touch paths never reach SoundManager; trail emitter is null).
//
// FIXTURE INVARIANT: the dispatch chain here stops at the Touch callbacks --
// nothing calls SlashEntity::Update(). That matters: Update's swipe-sound step
// calls PlaySwipe (v1.6.1 SlashEntity::PlaySwipe @0x001e8550) once
// |m_BladeDir| > 35, and PlaySwipe derefs game_work.mHud unguarded
// (SlashEntity.cpp:552), faithful to the binary, which relies on GameInit
// having created the HUD. If an Update() call is ever added here, the fixture
// must first do what boot does -- game_work.mHud = new HUD() -- as
// test_slash_collision.cpp already does.
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

// Reset the Mortar::Touch singleton between tests (same as test_input_tick_invariant).
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

// One 2-tick tap: DOWN, sim tick (press frame), UP, sim tick (release frame).
static void Tap(InputTranslatorSDL& tr, SDL_FingerID fid, float nx, float ny) {
    SDL_Event d = MakeFingerDown(fid, nx, ny);
    tr.DrainSDLEvent(d, NULL);
    tr.DispatchForSimTick();
    SDL_Event u = MakeFingerUp(fid, nx, ny);
    tr.DrainSDLEvent(u, NULL);
    tr.DispatchForSimTick();
}

// ---------------------------------------------------------------------------
// Event-level recorders: pin exactly which hash events fire per frame kind.
// Return false = pass-through (do not consume the dispatch).
// ---------------------------------------------------------------------------
static int g_cntScreen, g_cntDown, g_cntMoveX, g_cntMoveY, g_cntUp;
static bool RecScreen(InputEvent*) { ++g_cntScreen; return false; }
static bool RecDown  (InputEvent*) { ++g_cntDown;   return false; }
static bool RecMoveX (InputEvent*) { ++g_cntMoveX;  return false; }
static bool RecMoveY (InputEvent*) { ++g_cntMoveY;  return false; }
static bool RecUp    (InputEvent*) { ++g_cntUp;     return false; }
static void ResetCounters() { g_cntScreen = g_cntDown = g_cntMoveX = g_cntMoveY = g_cntUp = 0; }

// ---------------------------------------------------------------------------
// Wire one blade the way GameTaskInitInput @0x001cae0c does: the free callbacks
// in GameTaskInput.cpp own the per-finger hashes and dispatch into
// g_pSlashEntities[n]. SlashEntity itself subscribes to nothing.
// (GameTaskInitInput is not callable here -- it needs the ActorManager and a
// booted Game -- so the test does the same three registrations by hand.)
// ---------------------------------------------------------------------------
static void WireBlade(Mortar::InputManager& im, SlashEntity& se, int channel) {
    se.Init(channel);
    g_pSlashEntities[channel] = &se;

    char buf[20];
    sprintf(buf, "TouchMove_X%d", channel);
    im.RegisterInputCallback(StringHash(buf), PointerMoveCallback);
    sprintf(buf, "TouchMove_Y%d", channel);
    im.RegisterInputCallback(StringHash(buf), PointerMoveCallback);
    sprintf(buf, "TouchDown_%d", channel);
    im.RegisterInputCallback(StringHash(buf), TouchDownCallback);
}

// The SlashEntity is a stack local in every test below -- drop the global
// before it dies or the next test dispatches into a dangling pointer.
static void UnwireBlade(int channel) { g_pSlashEntities[channel] = NULL; }

// TAP: press frame emits TouchScreen + TouchDown only -- NO TouchMove.
// SWIPE: motion ticks emit TouchMove.
static void test_event_gate_press_vs_motion() {
    printf("  test_event_gate_press_vs_motion...\n");
    ResetTouch();

    Mortar::InputManager im;
    im.Init(0);
    im.RegisterInputCallback(StringHash("TouchScreen"),  RecScreen);
    im.RegisterInputCallback(StringHash("TouchDown_0"),  RecDown);
    im.RegisterInputCallback(StringHash("TouchMove_X0"), RecMoveX);
    im.RegisterInputCallback(StringHash("TouchMove_Y0"), RecMoveY);
    im.RegisterInputCallback(StringHash("TouchUp_0"),    RecUp);

    InputTranslatorSDL tr;
    tr.Init();

    // --- stationary tap ---
    ResetCounters();
    SDL_Event d = MakeFingerDown((SDL_FingerID)1, 0.5f, 0.5f);
    tr.DrainSDLEvent(d, NULL);
    tr.DispatchForSimTick();                 // press frame
    CHECK(g_cntScreen == 1);
    CHECK(g_cntDown   == 1);
    CHECK(g_cntMoveX  == 0);                 // THE GATE: no move on a motionless press
    CHECK(g_cntMoveY  == 0);

    tr.DispatchForSimTick();                 // held frame, still no motion
    CHECK(g_cntMoveX  == 0);
    CHECK(g_cntMoveY  == 0);
    CHECK(g_cntDown   == 2);                 // held re-arm TouchDown still fires

    SDL_Event u = MakeFingerUp((SDL_FingerID)1, 0.5f, 0.5f);
    tr.DrainSDLEvent(u, NULL);
    tr.DispatchForSimTick();                 // release frame
    CHECK(g_cntUp    == 1);
    CHECK(g_cntMoveX == 0);                  // a full tap never emitted a move
    printf("    tap: screen=%d down=%d moveX=%d up=%d\n",
           g_cntScreen, g_cntDown, g_cntMoveX, g_cntUp);

    // --- swipe ---
    ResetCounters();
    d = MakeFingerDown((SDL_FingerID)2, 0.2f, 0.5f);
    tr.DrainSDLEvent(d, NULL);
    tr.DispatchForSimTick();                 // press frame: no motion yet
    CHECK(g_cntMoveX == 0);
    CHECK(g_cntDown  == 1);

    SDL_Event m = MakeFingerMotion((SDL_FingerID)2, 0.5f, 0.5f);
    tr.DrainSDLEvent(m, NULL);
    tr.DispatchForSimTick();                 // motion tick: move fires
    CHECK(g_cntMoveX == 1);
    CHECK(g_cntMoveY == 1);
    CHECK(g_cntDown  == 2);

    u = MakeFingerUp((SDL_FingerID)2, 0.5f, 0.5f);
    tr.DrainSDLEvent(u, NULL);
    tr.DispatchForSimTick();
    CHECK(g_cntUp == 1);
    printf("    swipe: down=%d moveX=%d moveY=%d up=%d\n",
           g_cntDown, g_cntMoveX, g_cntMoveY, g_cntUp);

    printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// Blade-level: 3 discrete taps, bomb latch CLEAR. Each press-edge Reset()
// re-seeds a degenerate stroke at the tap position -- no segment ever spans
// two taps (pointCount stays at the 2-vert seed, head == tail).
// ---------------------------------------------------------------------------
static void test_taps_do_not_bridge_latch_clear() {
    printf("  test_taps_do_not_bridge_latch_clear...\n");
    ResetTouch();

    Mortar::InputManager im;
    im.Init(0);
    InputTranslatorSDL tr;
    tr.Init();
    SlashEntity se;
    WireBlade(im, se, 0);                    // finger 0

    CHECK(se.TestGetBombHitEdge() == 0);

    // Three taps at well-separated points (game coords in comments).
    const float taps[3][2] = {
        { 0.2f, 0.2f },   // A = (-144,  96)
        { 0.5f, 0.5f },   // B = (   0,   0)
        { 0.8f, 0.8f },   // C = ( 144, -96)
    };
    for (int i = 0; i < 3; ++i) {
        Tap(tr, (SDL_FingerID)(10 + i), taps[i][0], taps[i][1]);

        const float gx = taps[i][0] * 480.0f - 240.0f;
        const float gy = 160.0f - taps[i][1] * 320.0f;

        // Seed only: never more than one appended vertex pair per tap.
        // A bridge (old bug) interpolates points across the ~277-unit
        // tap-to-tap span (one pair every 64 units) -> pointCount >= 6.
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
// Blade-level: bomb latch SET (post-bomb game-over). Reset() is blocked, and
// since taps emit no TouchMove (and TouchDown's press-position sync lives
// inside the reset branch), NO position ever reaches the blade: point count
// and tail must not change at all. This is the exact user-visible bug: on the
// old code every tap emitted a TouchMove -> TouchDown appended a segment from
// the old tail to the tap point (ab, bc bridging slashes).
// ---------------------------------------------------------------------------
static void test_taps_do_not_bridge_bomb_latched() {
    printf("  test_taps_do_not_bridge_bomb_latched...\n");
    ResetTouch();

    Mortar::InputManager im;
    im.Init(0);
    InputTranslatorSDL tr;
    tr.Init();
    SlashEntity se;
    WireBlade(im, se, 0);

    // Prime a real stroke (latch clear): press at P0=(0,0), drag to P1=(120,0).
    SDL_Event d = MakeFingerDown((SDL_FingerID)1, 0.5f, 0.5f);
    tr.DrainSDLEvent(d, NULL);
    tr.DispatchForSimTick();
    SDL_Event m = MakeFingerMotion((SDL_FingerID)1, 0.75f, 0.5f);
    tr.DrainSDLEvent(m, NULL);
    tr.DispatchForSimTick();
    SDL_Event u = MakeFingerUp((SDL_FingerID)1, 0.75f, 0.5f);
    tr.DrainSDLEvent(u, NULL);
    tr.DispatchForSimTick();

    const int primedCount = se.GetPointCount();
    printf("    primed stroke: pointCount=%d tail=(%.1f,%.1f)\n",
           primedCount, se.GetTailPos().x, se.GetTailPos().y);
    CHECK(primedCount >= 4);                 // press seed + appended drag points
    CHECK_NEAR(se.GetTailPos().x, 120.0f, 1.0f);
    CHECK_NEAR(se.GetTailPos().y,   0.0f, 1.0f);

    // Simulate the mid-slash bomb hit: latch m_BombHitEdge.
    se.TestSetBombHitEdge(1);

    // Three discrete taps at far-apart points. Tap 0 alone is ~277 units from
    // the primed tail -- on the old code that appended >= 4 interpolated vertex
    // pairs (bridge). Point count and tail must stay EXACTLY as primed.
    Tap(tr, (SDL_FingerID)20, 0.2f, 0.2f);   // (-144,  96)
    CHECK(se.GetPointCount() == primedCount);
    Tap(tr, (SDL_FingerID)21, 0.5f, 0.8f);   // (   0, -96)
    CHECK(se.GetPointCount() == primedCount);
    Tap(tr, (SDL_FingerID)22, 0.8f, 0.2f);   // ( 144,  96)
    CHECK(se.GetPointCount() == primedCount);
    CHECK_NEAR(se.GetTailPos().x, 120.0f, 1.0f);
    CHECK_NEAR(se.GetTailPos().y,   0.0f, 1.0f);
    CHECK(se.TestGetBombHitEdge() == 1);     // latch untouched by taps

    printf("    after 3 taps: pointCount=%d (unchanged)\n", se.GetPointCount());
    UnwireBlade(0);
    printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// Blade-level: a real swipe still slices, and the stroke starts at the PRESS
// position (not at the previous stroke's stale position).
// ---------------------------------------------------------------------------
static void test_swipe_tracks_and_starts_at_press() {
    printf("  test_swipe_tracks_and_starts_at_press...\n");
    ResetTouch();

    Mortar::InputManager im;
    im.Init(0);
    InputTranslatorSDL tr;
    tr.Init();
    SlashEntity se;
    WireBlade(im, se, 0);

    // Pollute the blade's cached raw position with a prior tap far away:
    // D = (192, -128). The swipe below must NOT start there.
    Tap(tr, (SDL_FingerID)5, 0.9f, 0.9f);

    // Swipe: press A=(-144,0), drag to (-24,0) then (96,0). Horizontal at y=0.
    SDL_Event d = MakeFingerDown((SDL_FingerID)6, 0.2f, 0.5f);
    tr.DrainSDLEvent(d, NULL);
    tr.DispatchForSimTick();                 // press frame

    // Stroke seeded AT the press position (TouchDown press-position sync).
    CHECK(se.GetPointCount() == 2);
    CHECK_NEAR(se.GetTailPos().x, -144.0f, 1.0f);
    CHECK_NEAR(se.GetTailPos().y,    0.0f, 1.0f);

    SDL_Event m1 = MakeFingerMotion((SDL_FingerID)6, 0.45f, 0.5f);
    tr.DrainSDLEvent(m1, NULL);
    tr.DispatchForSimTick();                 // motion tick 1
    const int afterM1 = se.GetPointCount();
    printf("    after motion 1: pointCount=%d tail=(%.1f,%.1f)\n",
           afterM1, se.GetTailPos().x, se.GetTailPos().y);
    CHECK(afterM1 > 2);                      // blade tracked the drag
    CHECK_NEAR(se.GetTailPos().x, -24.0f, 1.0f);

    SDL_Event m2 = MakeFingerMotion((SDL_FingerID)6, 0.7f, 0.5f);
    tr.DrainSDLEvent(m2, NULL);
    tr.DispatchForSimTick();                 // motion tick 2
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
    tr.DispatchForSimTick();

    UnwireBlade(0);
    printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// Fast flick: DOWN and first MOTION drain within the SAME sim tick (120Hz /
// same-frame flick). motionSinceDown is already true at dispatch time, so the
// press frame emits the move and the blade registers the flick.
// ---------------------------------------------------------------------------
static void test_fast_flick_same_tick_registers() {
    printf("  test_fast_flick_same_tick_registers...\n");
    ResetTouch();

    Mortar::InputManager im;
    im.Init(0);
    InputTranslatorSDL tr;
    tr.Init();
    SlashEntity se;
    WireBlade(im, se, 0);

    SDL_Event d = MakeFingerDown((SDL_FingerID)9, 0.3f, 0.5f);   // (-96, 0)
    SDL_Event m = MakeFingerMotion((SDL_FingerID)9, 0.6f, 0.5f); // ( 48, 0)
    tr.DrainSDLEvent(d, NULL);
    tr.DrainSDLEvent(m, NULL);
    CHECK(tr.TestGetMotionSinceDown(0));
    tr.DispatchForSimTick();                 // single press frame sees both

    printf("    flick press frame: pointCount=%d tail=(%.1f,%.1f)\n",
           se.GetPointCount(), se.GetTailPos().x, se.GetTailPos().y);
    CHECK(se.GetPointCount() >= 2);          // blade seeded -- flick not lost
    CHECK_NEAR(se.GetTailPos().x, 48.0f, 1.0f);
    CHECK_NEAR(se.GetTailPos().y,  0.0f, 1.0f);

    SDL_Event u = MakeFingerUp((SDL_FingerID)9, 0.6f, 0.5f);
    tr.DrainSDLEvent(u, NULL);
    tr.DispatchForSimTick();

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

    test_event_gate_press_vs_motion();
    test_taps_do_not_bridge_latch_clear();
    test_taps_do_not_bridge_bomb_latched();
    test_swipe_tracks_and_starts_at_press();
    test_fast_flick_same_tick_registers();

    printf("test_slash_input: PASS\n");

    SDL_Quit();
    return 0;
}
