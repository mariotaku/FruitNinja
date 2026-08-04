// test_slash_collision.cpp
//
// Regression guard for SlashEntity::CollideWithEntity -- the faithful single-segment
// blade collision (ColLine mid->tail + m_SegLenSq bound, binary @0x001e6420).
//
// The test builds a ribbon via the public Touch API.  m_TrailShiftA/B are now
// set on every real touch-move add-path (UpdateTouchDown @0x1ea214 / @0x1ea3bc),
// so after Update(0.0f) the ColLine and m_SegLenSq are valid.
//
// Three assertions:
//   1. CROSSING HIT: a ColLine anchor (mid-point) that falls INSIDE the sphere
//      triggers the early-return true path.  Proves m_SegLenSq is now valid on
//      the real Touch path.
//   2. BLANK MISS:   a stroke at y=150 is well outside r=40; ColSphereLine
//      returns 0 -> MISS.  Guards against over-trigger.
//   3. NEAR-MISS / NEAR-HIT boundary using the ColLine model:
//        near-miss: mid=(45,0) is 45 units from origin, > r=40  -> MISS.
//        near-hit:  mid=(35,0) is 35 units from origin, < r=40  -> HIT (early-return).
//
// Geometry notes (binary ColLine):
//   ColLine.a = mid = (m_HeadPos + m_TailPos) / 2
//   ColLine.b = m_TailPos
//   m_SegLenSq = |mid - tail|^2
//   CollideWithEntity: early-returns true when |anchor - eCenter|^2 < r^2.
//
// No SDL_Init, no GL, no GameInit, no audio. The globals the fixture must still
// provide are game_work.mHud and game_work.mGameSound -- PlaySwipe derefs both
// unguarded, as the binary does. The HUD ctor allocates nothing but the object
// itself, and GameSound without an initialised SoundManager is inert.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "entities/SlashEntity.h"
#include "entities/Entity.h"
#include "collision/ColSphere.h"
#include "engine/input/InputEvent.h"
#include "game/GameWork.h"
#include "game/FruitSaveData.h"
#include "hud/HUD.h"
#include "engine/audio/GameSound.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// Minimal concrete Entity subclass for test-only use.
// Provides empty bodies for the three pure virtuals; m_Col is set externally.
// Entity() zeroes all fields (including m_Col) so ent.m_Col = &sphere is safe.
// ~Entity() does NOT call Release(), so a stack-local ColSphere m_Col is safe.
// ---------------------------------------------------------------------------
struct Renderer;  // forward-declared in Entity.h; needed by Draw signature

namespace {
struct TestEntity : public Mortar::Entity {
    void Update(float) {}
    void Draw(Renderer&) {}
    void PostUpdate(float) {}
};
} // namespace

// ---------------------------------------------------------------------------
// Touch helpers (identical pattern to test_slash_reset.cpp).
// The binary Touch path is: TouchMoveX writes m_RawTouchPos.x, TouchMoveY
// writes m_RawTouchPos.y, then TouchDown -> UpdateTouchDown ->
// OnTouchActive(x, y) to build the ribbon.
// ---------------------------------------------------------------------------

// TouchAxisX0 / TouchAxisY0 axis events -- one axis value per event
// (InputEvent +0x08), matching InputDevice::AxisEvent @0x0027582c.
static InputEvent MakeMove(int channel, bool yAxis, float x, float y) {
    InputEvent ev;
    FN_MakeTouchAxisEvent(ev, channel, yAxis, yAxis ? y : x);
    return ev;
}

// Touch<channel+1> button event, as ButtonPressed(0x89 + channel, 2, ...) packs
// it -- the "held" event Touch::SendIndividualTouchCallbacks raises every tick a
// finger is down. It carries no position: the two axis events above deliver that.
static InputEvent MakeDown(int channel) {
    InputEvent ev;
    FN_MakeTouchButtonEvent(ev, INPUT_ACTION_DOWN, channel);
    return ev;
}

// Feed one (x,y) touch point into the SlashEntity, in the order the binary's
// poll raises them for one slot: TouchAxisX, TouchAxisY, then Touch<n> down.
// A fresh (or just-Reset) blade has m_BladeActive == 0, so the FIRST call after
// construction starts a new stroke; later calls extend it.
static void Touch(SlashEntity& se, float x, float y) {
    InputEvent moveX = MakeMove(0, false, x, y);
    se.TouchMoveX(&moveX);
    InputEvent moveY = MakeMove(0, true, x, y);
    se.TouchMoveY(&moveY);
    InputEvent down = MakeDown(0);
    se.TouchDown(&down);
}

// ---------------------------------------------------------------------------
// TEST 1 -- crossing_hit
//
// Three touches, each 60 units apart along the x-axis.  After the anchor
// shuffle in Touch 3: m_HeadPos=(0,0), m_TailPos=(60,0).
// After Update(0.0f): ColLine.a = mid=(30,0,0), ColLine.b = (60,0,0),
//   m_SegLenSq = 900.
// Sphere at (0,0,0), r=40.
// CollideWithEntity: ColSphereLine non-zero (line y=0 crosses sphere).
//   anchor=(30,0), |anchor-eCenter|^2 = 900 < 1600 = r^2 -> early-return true.
// ---------------------------------------------------------------------------
static void test_crossing_hit() {
    std::printf("  test_crossing_hit...\n");

    SlashEntity se;
    se.Init(static_cast<void*>(0), 0L, static_cast<_Vector3<float>*>(0));

    Touch(se, -60.0f, 0.0f);   // seed; m_PointCount=2
    Touch(se, 0.0f,   0.0f);  // extend; m_PointCount=4
    Touch(se, 60.0f,  0.0f);  // extend; m_PointCount=6

    int pc = se.GetPointCount();
    std::printf("    m_PointCount=%d (expect >= 4)\n", pc);
    CHECK(pc >= 4);
    CHECK(se.IsBladeActive());

    // Simulate one frame: triggers UpdatePoints, sets ColLine + m_SegLenSq.
    se.Update(0.0f);

    ColSphere sphere(_Vector3<float>(0.0f, 0.0f, 0.0f), 40.0f);
    TestEntity ent;
    ent.m_Col = &sphere;

    bool hit = se.CollideWithEntity(&ent);
    std::printf("    hit=%s (expect true)\n", hit ? "true" : "false");
    CHECK(hit);

    std::printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// TEST 2 -- blank_miss (#306 segment-clamp guard)
//
// Stroke at y=150, far above sphere at (0,0,0) r=40.
// After Touch(2): m_HeadPos=(-220,150), m_TailPos=(-200,150).
// After Update(0.0f): ColLine.a=(-210,150,0), ColLine.b=(-200,150,0).
// Line y=150 has perp distance 150 from origin > r=40.
// ColSphereLine returns 0 -> CollideWithEntity returns false. MISS.
// ---------------------------------------------------------------------------
static void test_blank_miss() {
    std::printf("  test_blank_miss...\n");

    SlashEntity se;
    se.Init(static_cast<void*>(0), 0L, static_cast<_Vector3<float>*>(0));

    Touch(se, -220.0f, 150.0f);   // seed at far corner
    Touch(se, -200.0f, 150.0f);  // 20-unit step; m_PointCount=4

    int pc = se.GetPointCount();
    std::printf("    m_PointCount=%d (expect >= 4)\n", pc);
    CHECK(pc >= 4);
    CHECK(se.IsBladeActive());

    se.Update(0.0f);

    ColSphere sphere(_Vector3<float>(0.0f, 0.0f, 0.0f), 40.0f);
    TestEntity ent;
    ent.m_Col = &sphere;

    bool hit = se.CollideWithEntity(&ent);
    std::printf("    hit=%s (expect false)\n", hit ? "true" : "false");
    CHECK(!hit);

    std::printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// TEST 3 -- near_boundary (ColLine model)
//
// Both strokes are short horizontal segments on y=0 pointing right.
// The sphere is at (0,0,0) r=40.
//
// Near-miss: seed at (30,0), extend to (60,0).
//   Anchor shuffle -> m_HeadPos=(30,0), m_TailPos=(60,0), mid=(45,0).
//   ColLine.a=(45,0,0), ColLine.b=(60,0,0), m_SegLenSq=225.
//   anchor=(45,0): |anchor-center|^2 = 2025 > 1600 (not early-return).
//   hitA=hitB=(0,0,0) via degenerate chord (line passes through center,
//     Normalise of zero pen stays (0,0,0), 0*(r-half)=0).
//   (anchor-hitA)^2=2025 > m_SegLenSq=225 -> false. MISS.
//
// Near-hit: seed at (10,0), extend to (60,0).
//   Anchor shuffle -> m_HeadPos=(10,0), m_TailPos=(60,0), mid=(35,0).
//   ColLine.a=(35,0,0), ColLine.b=(60,0,0), m_SegLenSq=625.
//   anchor=(35,0): |anchor-center|^2 = 1225 < 1600 -> early-return true. HIT.
// ---------------------------------------------------------------------------
static void test_near_boundary() {
    std::printf("  test_near_boundary (near-miss, anchor=45 > r=40)...\n");
    {
        SlashEntity se;
        se.Init(static_cast<void*>(0), 0L, static_cast<_Vector3<float>*>(0));

        Touch(se, 30.0f, 0.0f);
        Touch(se, 60.0f, 0.0f);

        CHECK(se.GetPointCount() >= 4);
        CHECK(se.IsBladeActive());
        se.Update(0.0f);

        ColSphere sphere(_Vector3<float>(0.0f, 0.0f, 0.0f), 40.0f);
        TestEntity ent;
        ent.m_Col = &sphere;

        bool hit = se.CollideWithEntity(&ent);
        std::printf("    near-miss hit=%s (expect false, |mid|=45 > r=40)\n",
                    hit ? "true" : "false");
        CHECK(!hit);
    }
    std::printf("  PASS\n");

    std::printf("  test_near_boundary (near-hit, anchor=35 < r=40)...\n");
    {
        SlashEntity se;
        se.Init(static_cast<void*>(0), 0L, static_cast<_Vector3<float>*>(0));

        Touch(se, 10.0f, 0.0f);
        Touch(se, 60.0f, 0.0f);

        CHECK(se.GetPointCount() >= 4);
        CHECK(se.IsBladeActive());
        se.Update(0.0f);

        ColSphere sphere(_Vector3<float>(0.0f, 0.0f, 0.0f), 40.0f);
        TestEntity ent;
        ent.m_Col = &sphere;

        bool hit = se.CollideWithEntity(&ent);
        std::printf("    near-hit  hit=%s (expect true,  |mid|=35 < r=40)\n",
                    hit ? "true" : "false");
        CHECK(hit);
    }
    std::printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::printf("test_slash_collision: start\n");

    // The Update(0.0f) calls below reach SlashEntity::Update's swipe-sound step
    // (SlashEntity.cpp:2067-2072): |m_BladeDir| > 35 fires PlaySwipe(). The touch
    // steps here are 60 units (test_crossing_hit) and 50 units (test_near_boundary
    // near-hit), so it does fire. PlaySwipe reads
    // game_work.mHud->m_globalTimeScale UNGUARDED for its SFX gain
    // (SlashEntity.cpp:552) -- faithful to v1.6.1 SlashEntity::PlaySwipe
    // @0x001e8550, whose vmla at 0x1e85cc has no null check because boot always
    // creates the HUD first. Mirror that guarantee exactly as real boot does:
    // GameInit step 1 (GameInit.cpp:79) allocates the HUD before any SlashEntity
    // exists. The ctor leaves m_globalTimeScale at 1.0 (HUD.cpp:16), so the test
    // exercises the normal-speed gain 0.4 + 0.6*1.0 = 1.0 rather than 0.
    //
    // ItemManager::GetInstance() lazily constructs with m_DefaultItems[0] == NULL,
    // so PlayAlternateSwipeSound returns false (stock-swipe branch taken), and
    // ActorManager::GetNumEntities returns 0 with no type lists allocated.
    //
    // mGameSound must be provided too. This fixture used to rely on
    // PlaySwipe's `if (game_work.mGameSound)` guard, but that guard was
    // port-added -- v1.6.1 PlaySwipe @0x001e8550 calls SFXPlay through the
    // global unguarded -- so it was removed in the #156 sweep and a NULL here is
    // now a segfault. Per the standing rule the fixture supplies the global
    // rather than production re-growing the guard; same pattern as
    // test_powerup.cpp:77.
    //
    // Constructing GameSound does not open a device or play anything: SoundManager
    // is never initialised here, so SFXPlay resolves no sample and is inert. Tests
    // must never actually play audio.
    game_work.mHud = new HUD();
    game_work.mGameSound = new GameSound();
    // AddToCurrentScore's "all" AddToTotal branch derefs game_work.m_SaveData
    // unguarded too (v1.6.1 @0x0011a698 -- `ldr r0,[r6,#0x50]` straight into the
    // call). Same fixture-supplies-the-global rule.
    static FruitSaveData s_saveData;
    game_work.m_SaveData = &s_saveData;

    test_crossing_hit();
    test_blank_miss();
    test_near_boundary();

    std::printf("test_slash_collision: PASS\n");
    return 0;
}
