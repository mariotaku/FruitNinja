// scene_superfruit_finale.cpp -- full super-fruit finale render/screenshot test.
//
// Drives the REAL super-fruit finale end-to-end through the exact call chain
// tests/test_superfruit_finale_tick.cpp proved live (Fruit::CollisionResponse
// -> Fruit::FruitWasSlicedEvent -> SuperFruitControl::SuperFruitSliced ->
// HUD::Update ticking SuperFruitControl::Update every frame), and captures
// the finale VISUALLY at three stages via the real GameTaskDraw pipeline
// (Game::runFrames drives stepUpdate + renderFrame + SDL_GL_SwapWindow every
// tick, so no hand-rolled render loop is needed -- see Game::runFrames /
// Game::renderFrame in src/GameSDL.cpp).
//
// This is the visual counterpart to test_superfruit_finale_tick: that test
// proves the phase ladder TICKS to completion (headless, no screenshot); this
// one proves the phase ladder DRAWS -- the combo text 3-stop gradient
// (SuperFruitControl::ChangeText @0x001b9ee4), the jiblets fanning out
// (SpawnJibs @0x001bc748 + the ExplodeSuperFruit @0x001baa20 fragment loop),
// and the camera zoom / shockwave rings (DrawExplosion @0x001bd4d8).
//
// Stage timing (all relative to ctrl->m_Timer, read once right after the
// controller is created so the random m_Lifetime in [2,3) is known):
//   early -- m_Timer = -1.4 (~0.6s after creation): fade-in (0.15s window
//            starting at -2.0) is long done, so the "SLICE!" combo popup is
//            fully visible with its 3-stop vertical gradient.
//   blast -- m_Timer = Lifetime+0.5, i.e. the EXACT frame
//            SuperFruitControl::Update @0x001bca10 phase (b) fires
//            ExplodeSuperFruit()+SpawnJibs() (both unconditionally spawn 8
//            type-5 Jiblet actors each = ~16 at the peak). This is a COUNT
//            check only (no screenshot) -- jiblets launch at 500-900 u/s and
//            the bounds-kill box is only +/-288x+/-192 (Jiblet::Update), so
//            sampling even 0.25s later already misses most of them to
//            off-screen reaping. Not the same instant as "mid" below.
//   mid   -- m_Timer = Lifetime+0.5+0.25: just past the blast, screenshot
//            captured for the visual progression -- jiblets fanning outward
//            and DrawExplosion's inner ring ramping (UpdateExplosion, phase (c)).
//   late  -- m_Timer = tEnd-0.3, i.e. ~0.3s before the finale teardown
//            (m_bPendingRemoval=1 branch) -- the controller is still alive
//            and its combo/score text is still fully visible (the DrawOrder
//            fade-out window is [tOut, tOut+0.15], which starts just AFTER
//            this capture point), but close to self-removal. Both shockwave
//            rings have already faded out completely by this point (their
//            0.25s fade windows end at Lifetime+0.85 / Lifetime+2.05, well
//            before tEnd) -- the "rings" visual belongs to the mid capture.
//
// The host super fruit is spawned dead-centre with gravity zeroed (test-only
// fixture choice -- see the fruit->m_Gravity comment at the spawn site below)
// so the combo/score text, anchored to the host/explosion position, renders
// fully on-screen instead of drifting toward an edge and clipping.
//
// Screenshots: tmp/test/screenshots/superfruit_finale/{early,mid,late}.png
//
// Run:
//   ctest -R scene_superfruit_finale --output-on-failure
//   ./build/host/tests/scenes/scene_superfruit_finale.exe --interactive

#include "../test_harness.h"
#include "entities/Entity.h"
#include "entities/Fruit.h"
#include "entities/SlashEntity.h"
#include "entities/SuperFruitControl.h"
#include "entities/ActorManager.h"
#include "game/GameWork.h"
#include "hud/HUD.h"
#include "math/_Vector3.h"
#include "Game.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>

// Raw pointer-VALUE membership check against game_work.mHud->controls. Never
// dereferences ctrl -- safe to call even after HUD::Update has freed it.
// (Copied verbatim from test_superfruit_finale_tick.cpp -- same safety contract.)
static bool ControlStillInHud(HUDControl* ctrl) {
    if (!game_work.mHud) return false;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        if (*it == ctrl) return true;
    }
    return false;
}

// Minimum count of pixels that must differ from the pre-slice reference frame
// for a stage capture to count as "something new was drawn". Loose on purpose
// (per-project guidance: prefer solid liveness checks over brittle exact-pixel
// counts) -- this only needs to catch "nothing rendered at all", not verify
// exact shape/colour.
static const int MIN_DIFF_PIXELS = 150;
// A pixel counts as "different" if it differs from the reference frame by
// more than DIFF_TOLERANCE in ANY single channel (max, not summed, across
// R/G/B). See scene_fruit_splat.cpp for why summed-over-3-channels-then-
// thresholded is wrong: it lets a real per-channel shift hide under a
// combined threshold, silently undercounting faint-but-real draws. This
// scene's content (combo text, explosion rings, jiblets) is high-contrast so
// it rarely bit here, but the metric should be consistent project-wide. 8
// absorbs float->8-bit blend rounding (glReadPixels returns raw framebuffer
// bytes, no PNG/JPEG requantisation in this path).
static const int DIFF_TOLERANCE = 8;

static int CountDiffPixels(const unsigned char* a, const unsigned char* b, int w, int h) {
    int count = 0;
    for (int i = 0; i < w * h; ++i) {
        const unsigned char* pa = a + i * 3;
        const unsigned char* pb = b + i * 3;
        int dr = std::abs((int)pa[0] - (int)pb[0]);
        int dg = std::abs((int)pa[1] - (int)pb[1]);
        int db = std::abs((int)pa[2] - (int)pb[2]);
        int maxDiff = dr > dg ? dr : dg;
        if (db > maxDiff) maxDiff = db;
        if (maxDiff > DIFF_TOLERANCE) ++count;
    }
    return count;
}

// Redraws the CURRENT (already-simulated) frame into both GL buffers without
// advancing simulation (steps=0 zeroes the draw-path dt per Game::renderFrame's
// contract), so glReadPixels's default GL_BACK read target is guaranteed to
// hold this frame's content regardless of the buffer parity left over from the
// preceding runFrames() tick loop. Mirrors scene_jiblet's CaptureFrame
// "draw twice" idiom, adapted to the full GameTaskDraw pipeline (which owns
// its own swap inside Game::renderFrame, unlike scene_jiblet's hand-rolled
// RenderJibletFrame).
static void SettleFrame(fn::TestHarness& h) {
    h.game.renderFrame(0.0f, 0);
    h.game.renderFrame(0.0f, 0);
}

// Ticks the REAL frame loop (Game::runFrames -> stepUpdate -> GameTaskUpdate
// -> game_work.mHud->Update, the same chain test_superfruit_finale_tick
// verifies) until ctrl->m_Timer reaches targetTimer, or the controller
// self-removes, or maxFrames elapses. Never dereferences ctrl once it may
// have left the HUD -- checks ControlStillInHud before every read.
static bool TickUntilTimer(fn::TestHarness& h, SuperFruitControl* ctrl,
                           float targetTimer, int maxFrames) {
    for (int i = 0; i < maxFrames; ++i) {
        if (!ControlStillInHud(ctrl)) return false;
        if (ctrl->m_Timer >= targetTimer) return true;
        h.game.runFrames(1);
    }
    return ControlStillInHud(ctrl) && ctrl->m_Timer >= targetTimer;
}

int main(int argc, char* argv[]) {
    // Port specific: standalone super-fruit finale visual capture test.
    fn::TestHarness h(argc, argv, "scene_superfruit_finale");
    h.SetInteractiveDefault(false);
    // Same boot budget as test_superfruit_finale_tick: 120 burn-in frames so
    // GameInitialise (which subscribes SuperFruitControl::SuperFruitSliced to
    // Fruit::FruitWasSlicedEvent and loads the finale visuals) and GameInit
    // have both run, and game_work.mHud is live.
    h.SetInitFrames(120);

    if (!h.ParseFlags()) return 1;
    if (!h.Init())       return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL: game_work.mHud null after 120 frames\n");
        return 1;
    }

    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL: actorManager null after boot\n");
        return 1;
    }

    // "super_pomegranate" has score="0" in fruitlist.xml -> FruitInfo::m_bIsSuperFruit
    // is set (FruitInfo.cpp: m_bIsSuperFruit = (m_Score == 0)). Same host type
    // test_superfruit_finale_tick and scene_jiblet use.
    int fruitType = Fruit::FruitType("super_pomegranate", false);
    if (fruitType < 0) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL: 'super_pomegranate' not found in FruitInfo\n");
        return 1;
    }

    Mortar::Entity* e = am->Add(0, true);
    if (!e) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL: fruit pool exhausted\n");
        return 1;
    }
    Fruit* fruit = static_cast<Fruit*>(e);
    fruit->Init(NULL, (long)fruitType, NULL);
    // Port specific: the host fruit is spawned directly (bypassing the scripted
    // SuperFruitThrown() arc, which this test never calls) so pos/vel start at
    // dead centre. Fruit::Init still leaves a live gravity vector on it though,
    // and the anticipation + explosion timeline runs ~2.5-3.5s (m_Lifetime in
    // [2,3) plus the blast window) with nothing re-centring the fruit, so an
    // un-zeroed gravity drifts the host (and therefore m_ExplodeOrigin, and the
    // combo/score text anchored to it) toward a screen edge by blast time --
    // clipping the very text this capture exists to show. Zero gravity so the
    // host stays where placed for a clean, non-clipped capture; this is a test
    // fixture choice (no scripted arc is being exercised here either way), not
    // a gameplay-fidelity deviation.
    fruit->pos = _Vector3<float>(0.0f, 0.0f, 0.0f);
    fruit->vel = _Vector3<float>(0.0f, 0.0f, 0.0f);
    fruit->m_Gravity = _Vector3<float>(0.0f, 0.0f, 0.0f);
    fruit->flags &= ~(uint32_t)(0x01 | 0x10);  // clear ENT_INACTIVE | ENT_KILLED

    // Real hitter entity: GameInit spawns 16 SlashEntity finger-slots at boot;
    // g_pSlashEntities[0] is a live, real SlashEntity. Fruit::CollisionResponse
    // only forwards it to Fruit::FruitWasSlicedEvent() (never dereferences it).
    if (!g_pSlashEntities[0]) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL: g_pSlashEntities[0] null after boot\n");
        return 1;
    }

    // Active-play state: the faithful SuperFruitSliced create-gate @0x001be630 skips
    // creation when (FailureEnabled(mode) && bM_bPaused). Booting the harness leaves
    // bM_bPaused at its GameInit default of 1; reflect real unpaused play here.
    game_work.bM_bPaused = 0;

    // -------- interactive path: slice, then let the real loop run free --------
    if (h.IsInteractive()) {
        _Vector3<float> bladeVel(15.0f, 15.0f, 0.0f);
        fruit->CollisionResponse(g_pSlashEntities[0], 0, 0, &bladeVel);
        std::printf("[scene_superfruit_finale] interactive: sliced, watch the finale (ESC/close to exit)\n");
        h.game.run();
        return h.Shutdown();
    }

    // -------- headless: capture a pre-slice reference frame for pixel-diffing --------
    SettleFrame(h);
    int fw = 0, fh = 0;
    unsigned char* refPixels = h.ReadPixels(&fw, &fh);
    if (!refPixels) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL: ReadPixels (reference) failed\n");
        return 1;
    }

    // -------- real slice path: Fruit::CollisionResponse -> FruitWasSlicedEvent
    //          -> SuperFruitControl::SuperFruitSliced (subscribed by LoadContent,
    //          called from GameInitialise during h.Init()) --------
    _Vector3<float> bladeVel(15.0f, 15.0f, 0.0f);
    fruit->CollisionResponse(g_pSlashEntities[0], 0, 0, &bladeVel);

    std::map<Fruit*, SuperFruitControl*>::iterator it =
        SuperFruitControl::SuperFruitControls.find(fruit);
    if (it == SuperFruitControl::SuperFruitControls.end() || !it->second) {
        std::fprintf(stderr,
            "[scene_superfruit_finale] FAIL: SuperFruitSliced did not create/register "
            "a SuperFruitControl for the sliced super fruit\n");
        std::free(refPixels);
        return 1;
    }
    SuperFruitControl* ctrl = it->second;

    if (!ControlStillInHud(ctrl)) {
        std::fprintf(stderr,
            "[scene_superfruit_finale] FAIL: SuperFruitControl created but never "
            "registered into game_work.mHud->controls\n");
        std::free(refPixels);
        return 1;
    }

    // Read m_Lifetime once, right after creation (m_Timer == -2.0 here), so the
    // random uniform[2,3) roll is known and every stage's target timer is derived
    // from it -- same technique test_superfruit_finale_tick uses for tEnd.
    const float lifetime = ctrl->m_Lifetime;
    const float modeBias = (game_work.gameMode == 2) ? 1.5f : 0.5f;
    const float tBlast    = lifetime + 0.5f;
    const float tEnd      = lifetime + 0.5f + 0.35f + 0.55f + 0.65f + 0.25f + modeBias + 0.15f;

    const float timerEarly = -1.4f;          // fade-in (0.15s window from -2.0) long done
    const float timerMid   = tBlast + 0.25f; // just past the blast: jibs fanning, ring ramping
    const float timerLate  = tEnd - 0.3f;    // near self-removal, still alive

    std::printf("[scene_superfruit_finale] ctrl=%p lifetime=%.3f tBlast=%.3f tEnd=%.3f "
                "targets early=%.3f mid=%.3f late=%.3f\n",
                static_cast<void*>(ctrl), lifetime, tBlast, tEnd,
                timerEarly, timerMid, timerLate);

    // Diagnostic: confirm SuperFruitControl::LoadContent (run during GameInitialise,
    // part of the 120-frame boot above) actually loaded JibletModel before the blast
    // fires. If this prints 0, the blast-edge jiblet count below will legitimately
    // come up short (ExplodeSuperFruit's 8 fragment spawns are unconditional and
    // unaffected, but SpawnJibs's 8 radial-fan spawns pass a null model SmartPtr --
    // still safe, per Jiblet::Draw's null-model gate, but invisible).
    std::printf("[scene_superfruit_finale] SuperFruitControl::HasJibletModel()=%d (checked pre-blast)\n",
                (int)SuperFruitControl::HasJibletModel());

    bool overallPass = true;

    // ---- EARLY: combo text ("SLICE!") faded in, 3-stop gradient visible ----
    if (!TickUntilTimer(h, ctrl, timerEarly, 300)) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL (EARLY): never reached "
                              "m_Timer=%.3f (or control vanished early)\n", timerEarly);
        overallPass = false;
    } else {
        bool hasComboText = (ctrl->m_pComboText != NULL);
        SettleFrame(h);
        unsigned char* px = h.ReadPixels(&fw, &fh);
        int diff = px ? CountDiffPixels(refPixels, px, fw, fh) : 0;
        std::free(px);
        h.ScreenshotPng("superfruit_finale/early");
        std::printf("[scene_superfruit_finale] EARLY: m_Timer=%.3f hasComboText=%d diffPixels=%d\n",
                    ctrl->m_Timer, (int)hasComboText, diff);
        if (!hasComboText || diff < MIN_DIFF_PIXELS) {
            std::fprintf(stderr, "[scene_superfruit_finale] FAIL (EARLY): hasComboText=%d "
                                  "diffPixels=%d (want >= %d)\n", (int)hasComboText, diff, MIN_DIFF_PIXELS);
            overallPass = false;
        }
    }

    // ---- BLAST EDGE: sample the jiblet count at its PEAK, i.e. the same frame
    //      the blast fires (SuperFruitControl::Update's one-shot at
    //      Timer>=Lifetime+0.5 calls ExplodeSuperFruit() [8 unconditional
    //      am->Add(5) fragments] then SpawnJibs() [8 more, radial fan]). Sample
    //      here, NOT after further ticking -- jiblets launch at 500-900 u/s and
    //      the bounds-kill box is only +/-288x+/-192 (Jiblet::Update), so a
    //      count taken even 0.25s later (~15 frames) already misses most of
    //      them to off-screen reaping. TickUntilTimer's check-before-tick loop
    //      guarantees the blast has already fired by the time it returns true
    //      (the crossing tick's Update() call is what both advances m_Timer
    //      past tBlast AND fires the blast, in that order, in the same call).
    int jibletsBeforeBlast = am->GetNumEntities(5);
    if (!TickUntilTimer(h, ctrl, tBlast, 500)) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL (BLAST): never reached "
                              "m_Timer=%.3f (or control vanished)\n", tBlast);
        overallPass = false;
    } else {
        int jibletsAtBlast = am->GetNumEntities(5);
        int jibletDelta = jibletsAtBlast - jibletsBeforeBlast;
        std::printf("[scene_superfruit_finale] BLAST: m_Timer=%.3f jibletsBefore=%d jibletsAtBlast=%d "
                    "delta=%d\n", ctrl->m_Timer, jibletsBeforeBlast, jibletsAtBlast, jibletDelta);
        // ExplodeSuperFruit's 8-fragment loop and SpawnJibs's 8-jib radial fan are
        // BOTH unconditional in the binary (SuperFruitControl.cpp SpawnJibs no longer
        // gates on JibletModel.Get() -- see SuperFruitControl::HasJibletModel() log
        // above), so the peak count should be ~16. Allow a few immediate off-screen
        // reaps / pool contention rather than asserting the exact figure.
        bool jibletsSpawned = jibletDelta >= 12;
        if (!jibletsSpawned) {
            std::fprintf(stderr, "[scene_superfruit_finale] FAIL (BLAST): jibletDelta=%d (want >= 12)\n",
                          jibletDelta);
            overallPass = false;
        }
    }

    // ---- MID: continue past the blast -- jiblets fanning, shockwave ring ramping ----
    if (!TickUntilTimer(h, ctrl, timerMid, 100)) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL (MID): never reached "
                              "m_Timer=%.3f (or control vanished)\n", timerMid);
        overallPass = false;
    } else {
        SettleFrame(h);
        unsigned char* px = h.ReadPixels(&fw, &fh);
        int diff = px ? CountDiffPixels(refPixels, px, fw, fh) : 0;
        std::free(px);
        h.ScreenshotPng("superfruit_finale/mid");
        std::printf("[scene_superfruit_finale] MID: m_Timer=%.3f diffPixels=%d\n", ctrl->m_Timer, diff);
        if (diff < MIN_DIFF_PIXELS) {
            std::fprintf(stderr, "[scene_superfruit_finale] FAIL (MID): diffPixels=%d (want >= %d)\n",
                          diff, MIN_DIFF_PIXELS);
            overallPass = false;
        }
    }

    // ---- LATE: near self-removal, still alive and drawing ----
    if (!TickUntilTimer(h, ctrl, timerLate, 400)) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL (LATE): never reached "
                              "m_Timer=%.3f (or control vanished early)\n", timerLate);
        overallPass = false;
    } else {
        bool stillAlive = ControlStillInHud(ctrl);
        SettleFrame(h);
        unsigned char* px = h.ReadPixels(&fw, &fh);
        int diff = px ? CountDiffPixels(refPixels, px, fw, fh) : 0;
        std::free(px);
        h.ScreenshotPng("superfruit_finale/late");
        std::printf("[scene_superfruit_finale] LATE: m_Timer=%.3f stillAlive=%d diffPixels=%d\n",
                    ctrl->m_Timer, (int)stillAlive, diff);
        if (!stillAlive || diff < MIN_DIFF_PIXELS) {
            std::fprintf(stderr, "[scene_superfruit_finale] FAIL (LATE): stillAlive=%d "
                                  "diffPixels=%d (want >= %d)\n", (int)stillAlive, diff, MIN_DIFF_PIXELS);
            overallPass = false;
        }
    }

    // ---- bonus sanity: the finale still completes (self-removes) shortly after,
    //      same completion proof as test_superfruit_finale_tick -- cheap to re-check
    //      here and catches a regression that only shows up after the "late" stage.
    bool sawSelfRemoval = false;
    for (int i = 0; i < 120; ++i) {
        if (!ControlStillInHud(ctrl)) { sawSelfRemoval = true; break; }
        h.game.runFrames(1);
    }
    std::printf("[scene_superfruit_finale] completion: sawSelfRemoval=%d\n", (int)sawSelfRemoval);
    if (!sawSelfRemoval) {
        std::fprintf(stderr, "[scene_superfruit_finale] FAIL (COMPLETION): controller never "
                              "self-removed within 120 frames of the LATE stage\n");
        overallPass = false;
    }

    std::free(refPixels);

    std::printf("[scene_superfruit_finale] %s\n", overallPass ? "PASS" : "FAIL");
    h.Shutdown();
    return overallPass ? 0 : 1;
}
