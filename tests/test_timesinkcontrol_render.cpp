// test_timesinkcontrol_render.cpp -- TimeSinkControl ("Berry Blast" time-award
// board) render test. TimeSinkControl::Update/DrawOrder were a no-op stub
// until the v1.6.1 ASM-spec port (see src/hud/TimeSinkControl.cpp); this test
// renders the real "+M:SS" fly-in label the RELEASE phase draws.
//
// Drives the REAL activation path rather than hand-constructing a
// TimeSinkControl: PowerUpManager::ActivatePower(StringHash("time_sink"), ...)
// (the "time_sink" XML power in poweruplist.xml is the Berry-Blast/freeze-clock
// power; its <effect> has a texture="" defer="time" image, which
// ScreenEffect::Activate turns into a TimeSinkControl -- see ScreenEffect.cpp
// kind==2 branch). This is load-bearing: TimeSinkControl::Update's ACTIVE
// branch ratchets m_DrawColour.a up to a running max held in m_QuantumFlag
// (see header); skipping the real ACTIVE window would leave m_QuantumFlag at
// its ctor default of 0, so the RELEASE phase's `m_DrawColour.a =
// m_QuantumFlag;` would freeze the label at alpha=0 (invisible) regardless of
// anything else. Driving the real ScreenEffect fade-in (image transitionTime
// ="0.5") for the ACTIVE settle window latches m_QuantumFlag to full alpha
// exactly like a real Berry-Blast activation.
//
// The award value (m_TargetScore, mirrored from the TimeSinkModifier's
// m_Accumulator while ACTIVE) is set directly to 0.30f -- a representative
// "+0:30" payout -- rather than simulating ~120 individual fruit-slice events
// (XML timePerFruit="0.25"/fruit) to reach that value; this is a deterministic
// test INPUT, not a fudge of TimeSinkControl's own math.
//
// Expiry -> RELEASE transition is driven through the real
// PowerUpManager::Update/PowerUp::Update expiry cascade (set each modifier's
// m_BonusAccum to a tiny positive epsilon so the next GameModifier::Update
// decrements it across zero -- binary's countdown-expiry path @0x0013fdc4
// GameModifier::Update only expires when m_BonusAccum is strictly positive
// and the dt decrement brings it <=0; setting it to exactly 0 hits the
// `> 0.0f` guard as false and the modifier never expires -- then drain the
// ~1/12s m_BarRamp rampdown, then PowerUp::Deactivate -> ScreenEffect::
// Deactivate -> sink->m_pPowerUp = 0) rather than poking m_pPowerUp directly,
// so ScreenEffect::Deactivate's "did the window run to completion" check
// (GetCurrentTimeProgress() <= 0.01f) sees a genuine natural expiry and does
// NOT zero the payout.
//
// Captures 3 PNGs by polling the real m_TimeElapsed field (RELEASE-phase
// self-animation clock) to the targets suggested by the task -- these sample
// the ease-in-then-held-peak/fly-in/pre-bank windows of DrawOrder's animated
// "+M:SS" label, not literal mathematical extrema of the two InverseSquare
// eases (see TimeSinkControl.cpp Update() -- AnimScale actually peaks at 80
// around 0.33s and HOLDS there until 0.63s, so 0.1s is mid-ease, not the peak):
//   peak       (m_TimeElapsed ~0.10s) -- font-pop ease in progress
//   mid_flyin  (m_TimeElapsed ~0.70s) -- board flying toward the on-screen clock
//   near_award (m_TimeElapsed ~1.00s) -- settling just before the 1.08s bank
//
// Assertions:
//   ACTIVATE: PowerUpManager::ActivatePower("time_sink") succeeds and a
//             TimeSinkControl appears in game_work.mHud.
//   RELEASE:  sink->m_pPowerUp becomes NULL via the real expiry cascade.
//   DRAW:     each of the 3 captures has >= MIN_DRAWN_PIXELS non-background
//             pixels (glyphs were actually drawn).
//
// Port specific: standalone TimeSinkControl render test.
//
// Run:
//   ctest -R timesinkcontrol_render --output-on-failure
//   ./build/host/tests/test_timesinkcontrol_render.exe --interactive
//
// Screenshots: tmp/test/screenshots/timesinkcontrol/{peak,mid_flyin,near_award}.png

#include "test_harness.h"
#include "hud/TimeSinkControl.h"
#include "hud/HUD.h"
#include "game/PowerUpManager.h"
#include "game/PowerUp.h"
#include "game/GameModifier.h"
#include "game/TimeSinkModifier.h"
#include "game/GameMode.h"
#include "game/GameWork.h"
#include "engine/util/StringHash.h"
#include "math/Vec3.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>

// Background clear colour: pure black. TimeSinkControl's XML image has
// texture="" (no background art -- see poweruplist.xml "time_sink" power),
// so the only thing that draws is the "+M:SS" label (white/light digits) --
// black gives maximum contrast.
static const unsigned char BG_R         = 0;
static const unsigned char BG_G         = 0;
static const unsigned char BG_B         = 0;
static const int           BG_THRESHOLD = 30;

// Minimum non-background pixels to consider a capture "drawn".
static const int MIN_DRAWN_PIXELS = 30;

static const float TICK_DT = 1.0f / 60.0f;

static bool IsBackground(unsigned char r, unsigned char g, unsigned char b) {
    int d = abs((int)r - (int)BG_R) + abs((int)g - (int)BG_G) + abs((int)b - (int)BG_B);
    return d <= BG_THRESHOLD;
}

static int CountNonBackground(const unsigned char* pixels, int w, int h) {
    int count = 0;
    for (int i = 0; i < w * h; ++i) {
        const unsigned char* px = pixels + i * 3;
        if (!IsBackground(px[0], px[1], px[2])) ++count;
    }
    return count;
}

// ACTIVE-window settle hook: pins every active power's bonus accumulator well
// above the "time_sink" power's XML length (7s) so it cannot naturally expire
// during the settle window, and holds m_BarRamp at 1.0. Mirrors
// test_powerup_hud.cpp's PowerUpPreFrame pattern.
static void HoldPreFrame(void* ud, float dt) {
    PowerUpManager* pum = static_cast<PowerUpManager*>(ud);
    for (std::list<PowerUp*>::iterator it = pum->m_ActivePowerUps.begin();
         it != pum->m_ActivePowerUps.end(); ++it) {
        PowerUp* p = *it;
        p->m_BarRamp = 1.0f;
        for (std::list<GameModifier*>::iterator mit = p->m_ModList.begin();
             mit != p->m_ModList.end(); ++mit) {
            (*mit)->m_BonusAccum = 999.0f;
        }
    }
    pum->Update(dt);
}

// Post-expiry-trigger hook: plain, un-pinned PowerUpManager::Update so the
// real ramp-down -> Deactivate cascade runs to completion.
static void ExpirePreFrame(void* ud, float dt) {
    static_cast<PowerUpManager*>(ud)->Update(dt);
}

static TimeSinkControl* FindSink() {
    if (!game_work.mHud) return NULL;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        TimeSinkControl* s = dynamic_cast<TimeSinkControl*>(*it);
        if (s) return s;
    }
    return NULL;
}

struct Capture { float targetElapsed; const char* name; };

int main(int argc, char* argv[]) {
    // Port specific: standalone TimeSinkControl render test.

    fn::TestHarness h(argc, argv, "timesinkcontrol");
    // 120 burn-in frames: lets GameInitialise -> PowerUpManager::Load parse
    // poweruplist.xml (templates + screen effects) before we activate.
    h.SetInitFrames(120);
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        fprintf(stderr, "[timesinkcontrol] FAIL: mHud null after boot\n");
        return 1;
    }

    // "time_sink" (Berry Blast) is an Arcade-only power, like freeze/speed/score_mult.
    game_work.gameMode = Mortar::GAME_MODE_ARCADE;

    PowerUpManager* pum = PowerUpManager::GetInstance();
    // WaveManager::LoadTextures normally primes powerup icon/bar textures on
    // wave start; the isolated boot may not have reached a wave.
    pum->LoadTextures();

    Vec3 origin(0.0f, 0.0f, 0.0f);
    PowerUp* p = pum->ActivatePower(StringHash("time_sink"), origin, NULL);
    printf("[timesinkcontrol] activate \"time_sink\" -> %p\n", (void*)p);
    if (!p) {
        fprintf(stderr, "[timesinkcontrol] FAIL: ActivatePower(\"time_sink\") returned null "
                "(hash mismatch or template not loaded)\n");
        return 1;
    }

    // Set the award value directly on the real TimeSinkModifier (GetType()==4)
    // -- a deterministic test input, not a fudge of TimeSinkControl's math.
    TimeSinkModifier* sinkMod = NULL;
    for (std::list<GameModifier*>::iterator it = p->m_ModList.begin();
         it != p->m_ModList.end(); ++it) {
        if ((*it)->GetType() == 4) { sinkMod = static_cast<TimeSinkModifier*>(*it); break; }
    }
    if (!sinkMod) {
        fprintf(stderr, "[timesinkcontrol] FAIL: no TimeSinkModifier (GetType()==4) in "
                "\"time_sink\" power's m_ModList\n");
        return 1;
    }
    sinkMod->m_Accumulator = 0.30f; // representative "+0:30" award

    // --- ACTIVE settle: 30 frames (0.5s) lands past the image's
    //     transitionTime="0.5" fade-in, latching TimeSinkControl's
    //     m_QuantumFlag alpha ratchet to full via the real ScreenEffect path.
    h.RunComponentHeadlessHooked(30, HoldPreFrame, NULL, pum);

    TimeSinkControl* sink = FindSink();
    if (!sink) {
        fprintf(stderr, "[timesinkcontrol] FAIL: no TimeSinkControl found in game_work.mHud "
                "after activating \"time_sink\"\n");
        return 1;
    }
    printf("[timesinkcontrol] ACTIVE settle: m_TargetScore=%.3f m_DrawColour.a=%u "
           "m_QuantumFlag=%u\n",
           sink->m_TargetScore, (unsigned)sink->m_DrawColour.a, (unsigned)sink->m_QuantumFlag);
    bool activatePass = (sink->m_QuantumFlag > 0);
    if (!activatePass) {
        fprintf(stderr, "[timesinkcontrol] FAIL (ACTIVATE): m_QuantumFlag never ratcheted up "
                "-- label would render at alpha=0\n");
    }

    // --- Trigger natural expiry: set each modifier's m_BonusAccum to a tiny
    //     positive epsilon so the next GameModifier::Update decrements it
    //     across zero (binary's countdown-expiry path @0x0013fdc4
    //     GameModifier::Update: `if (m_BonusAccum > 0.0f) { m_BonusAccum -=
    //     dt; if (m_BonusAccum <= 0.0f) return 1; }`). Setting it to exactly
    //     0 hits the `> 0.0f` guard as false, skips the decrement, and the
    //     modifier reports "still alive" forever. PowerUp::Update then ramps
    //     m_BarRamp down over ~1/12s before itself returning expired, at
    //     which point PowerUpManager::Update calls PowerUp::Deactivate ->
    //     ScreenEffect::Deactivate -> sink->m_pPowerUp = 0 (RELEASE entry).
    //     p is deleted once this completes -- do not touch it afterward.
    for (std::list<GameModifier*>::iterator it = p->m_ModList.begin();
         it != p->m_ModList.end(); ++it) {
        (*it)->m_BonusAccum = 0.001f; // <= dt (1/60); decrements across zero next tick
    }
    bool releasePass = false;
    for (int i = 0; i < 30; ++i) {
        h.RunComponentHeadlessHooked(1, ExpirePreFrame, NULL, pum);
        if (sink->m_pPowerUp == NULL) { releasePass = true; break; }
    }
    p = NULL; // may be deleted by now; never dereference again
    printf("[timesinkcontrol] RELEASE entry: %s (m_TargetScore=%.3f)\n",
           releasePass ? "reached" : "NOT reached", sink->m_TargetScore);
    if (!releasePass) {
        fprintf(stderr, "[timesinkcontrol] FAIL (RELEASE): sink->m_pPowerUp never cleared "
                "within 30 ticks of the expiry trigger\n");
    }

    // --- Interactive path: hold at the near_award point and spin ---
    if (h.IsInteractive()) {
        SDL_GL_SetSwapInterval(1);
        h.RunComponentInteractive(NULL, NULL, -1);
        return h.Shutdown();
    }

    // --- Headless: capture the 3 RELEASE-phase animation points ---
    static const Capture kCaptures[3] = {
        { 0.10f, "timesinkcontrol/peak"       },
        { 0.70f, "timesinkcontrol/mid_flyin"  },
        { 1.00f, "timesinkcontrol/near_award" },
    };
    static const int MAX_TICKS_PER_CAPTURE = 300;

    bool allDrawn = true;
    for (int ci = 0; ci < 3 && releasePass; ++ci) {
        int guard = 0;
        while (sink->m_TimeElapsed < kCaptures[ci].targetElapsed && guard < MAX_TICKS_PER_CAPTURE) {
            h.RunComponentHeadless(1);
            ++guard;
        }

        int fw = 0, fh = 0;
        unsigned char* pixels = h.ReadPixels(&fw, &fh);
        int drawnPixels = pixels ? CountNonBackground(pixels, fw, fh) : 0;
        free(pixels);

        h.ScreenshotPng(kCaptures[ci].name);

        printf("[timesinkcontrol] %-24s m_TimeElapsed=%.3f m_DisplayScore=%.3f "
               "m_AnimScale=%.2f pos=(%.1f,%.1f,%.1f) drawnPixels=%d\n",
               kCaptures[ci].name, sink->m_TimeElapsed, sink->m_DisplayScore,
               sink->m_AnimScale, sink->pos.x, sink->pos.y, sink->pos.z, drawnPixels);

        if (drawnPixels < MIN_DRAWN_PIXELS) allDrawn = false;
    }

    bool overallPass = activatePass && releasePass && allDrawn;
    printf("[timesinkcontrol] ACTIVATE=%s RELEASE=%s DRAW=%s\n",
           activatePass ? "PASS" : "FAIL",
           releasePass  ? "PASS" : "FAIL",
           allDrawn     ? "PASS" : "FAIL");

    h.Shutdown();
    return overallPass ? 0 : 1;
}
