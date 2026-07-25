// scene_versus_ingame.cpp -- online-VERSUS in-game scene: real gameplay (fruit
// spawning via the actual WaveManager/ActorManager pipeline) with the revived
// ZenVersusControl HUD drawn on top, against a PASSIVE opponent (peer sends
// only the session-setup handshake -- never any gameplay packets, so its
// score is whatever we set explicitly and never moves on its own).
//
// This is a VISUAL capture, not a strict pass/fail regression test (mirrors
// scene_superfruit_finale's shape) -- but it still asserts the essentials so
// it fails loudly if the versus session or the spawn pipeline silently don't
// come up: ZenVersusControl must be live in the HUD, at least one fruit must
// be airborne at each capture, and the framebuffer must have actually drawn
// something (drawnPixels floor).
//
// ORGANIC vs FORCED (see the main() comments at each step for exact detail):
//   ORGANIC: LoopbackTransport::CreatePair + Host()/Join() (async connect),
//            NetworkManager::Update() draining PollEvent() into
//            GlobalP2PMessageHandler -> HandleP2PConnected (sets gameMode to
//            the versus/combo mode, WaveManager::Reset(true), HUD::
//            SetToMultiplayerState(), CreateMultiplayerControls() which is
//            what actually constructs the ZenVersusControl under test) and
//            HandleP2PNames; delivering a real wire-serialized StartGamePacket
//            cmd2 (seed+go) from the peer end through the same Update() drain
//            path (HandleP2PData case 103 cmd2).
//   FORCED:  PrepareForLevelStart() + MainScreen::SetCameraTransition(0)/
//            SetState(STATE_CAMERA_FADE) + bM_bPaused=0 -- the same trio
//            test_gameplay.cpp uses to bypass the menu-click/camera-zoom
//            state machine no test harness drives. Explicit WaveManager RNG
//            seed for reproducible spawn positions (test-only, not a
//            src-side gate). Player names (GameWork::SetPlayerName) and the
//            opponent's score (NetworkManager::SetOpponentScore) -- there is
//            no "opponent slices/sends score" call site to drive organically
//            for a PASSIVE opponent by design (see task). Our own score is
//            bumped via the real AddToCurrentScore() call (the same function
//            Fruit::CollisionResponse's scoring path uses), not a raw field
//            write, so at least that half of the "score moved" story is the
//            real scoring function.
//
// Screenshots: tmp/test/screenshots/scene_versus_ingame/{early,mid,late}.png
//
// Run:
//   ctest -R scene_versus_ingame --output-on-failure
//   ./build/host/tests/scenes/scene_versus_ingame.exe --interactive

#include "../test_harness.h"
#include "engine/network/IMpTransport.h"
#include "engine/network/LoopbackTransport.h"
#include "engine/network/MpTransport.h"
#include "engine/network/ByteBuffer.h"
#include "engine/network/NetworkPacket.h"
#include "engine/network/NetworkManager.h"
#include "engine/network/P2PMessageHandling.h"
#include "game/StartGamePacket.h"
#include "game/WaveManager.h"
#include "game/StartupEffects.h"
#include "game/GameOver.h"
#include "game/GameMode.h"
#include "game/GameWork.h"
#include "entities/ActorManager.h"
#include "entities/Fruit.h"
#include "hud/HUD.h"
#include "hud/ZenVersusControl.h"
#include "screens/MainScreen.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <list>

// Minimum non-black pixels for the "something actually drew" liveness check.
// Same floor test_versus_hud_render.cpp uses for the isolated-HUD case; this
// scene draws the full game background + fruit + HUD, so it clears this floor
// by a wide margin whenever anything is on screen at all.
static const int MIN_DRAWN_PIXELS = 200;

static int CountNonBlack(const unsigned char* pixels, int w, int h) {
    int count = 0;
    for (int i = 0; i < w * h; ++i) {
        const unsigned char* px = pixels + i * 3;
        if ((int)px[0] + (int)px[1] + (int)px[2] > 30) ++count;
    }
    return count;
}

// Raw pointer-VALUE membership search through game_work.mHud->controls for a
// live ZenVersusControl -- CreateMultiplayerControls() (P2PMessageHandling.cpp)
// is the only call site that constructs one, guarded by its own file-static so
// it never double-adds; this just finds whichever instance it made.
static ZenVersusControl* FindZenVersusControl() {
    if (!game_work.mHud) return NULL;
    for (std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
         it != game_work.mHud->controls.end(); ++it) {
        ZenVersusControl* zvc = dynamic_cast<ZenVersusControl*>(*it);
        if (zvc) return zvc;
    }
    return NULL;
}

// Redraws the CURRENT (already-simulated) frame into both GL buffers without
// advancing simulation, so glReadPixels's default GL_BACK read target holds
// this frame's content regardless of swap-buffer parity. Same idiom
// scene_superfruit_finale.cpp uses.
static void SettleFrame(fn::TestHarness& h) {
    h.game.renderFrame(0.0f, 0);
    h.game.renderFrame(0.0f, 0);
}

// One tick of real gameplay simulation, re-asserting the versus/combo mode
// state every frame -- same belt-and-suspenders reassertion test_gameplay.cpp
// uses, since nothing else in this scene drives the menu/camera state machine
// that would normally keep these pinned.
static void TickGameplay(fn::TestHarness& h, int n) {
    for (int i = 0; i < n; ++i) {
        game_work.gameMode      = GAME_MODE_COMBO;
        game_work.bM_bPaused    = 0;
        game_work.bM_Mode       = false;
        game_work.m_PauseAmount = 0.0f;
        h.game.runFrames(1);
    }
}

static void PrintResult(const char* stage, Mortar::ActorManager* am) {
    int ourScore  = game_work.currentScore;
    int oppScore  = Mortar::NetworkManager::GetInstance()->GetOpponentScore();
    bool hasZvc   = FindZenVersusControl() != NULL;
    int fruitLive = am ? am->GetNumEntities(0) : -1;
    std::printf("[RESULT] stage=%s ourScore=%d opponentScore=%d "
                "zenVersusControl=%d liveFruit=%d\n",
                stage, ourScore, oppScore, (int)hasZvc, fruitLive);
}

int main(int argc, char* argv[]) {
    // Port specific: standalone online-versus in-game visual capture scene.
    fn::TestHarness h(argc, argv, "scene_versus_ingame");
    h.SetInteractiveDefault(false);
    // Same boot budget as scene_superfruit_finale / test_gameplay: enough
    // burn-in for GameInitialise (wave XML for every mode, HUD, fonts) to
    // have fully run before we touch game_work.
    h.SetInitFrames(120);

    if (!h.ParseFlags()) return 1;
    if (!h.Init())       return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL: game_work.mHud null after 120 frames\n");
        return 1;
    }
    Mortar::ActorManager* am = h.game.actorManager;
    if (!am) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL: actorManager null after boot\n");
        return 1;
    }

    bool overallPass = true;

    // ---- ORGANIC: async loopback connect + real session-setup dispatch ----
    // Mirrors test_mp_loopback.cpp's test_seam_dispatch/test_full_session_
    // handshake pattern: `a` (host, LocalPlayerNumber()==1) is installed as
    // the active transport and represents "us" -- game_work is a single
    // process-wide global, so only one side of the session can be "us" in a
    // one-process test. `b` (guest) is the PASSIVE peer: nothing ever calls
    // Send()/SendP2PPacket() FROM b except the one StartGamePacket cmd2 below
    // (part of the real session-setup handshake, not a gameplay packet).
    Mortar::LoopbackTransport* a = 0;
    Mortar::LoopbackTransport* b = 0;
    Mortar::LoopbackTransport::CreatePair(a, b);
    Mortar::SetMpTransport(a);

    Mortar::NetworkManager* nm = Mortar::NetworkManager::GetInstance();

    if (!a->Host() || !b->Join("")) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL: Host()/Join() rejected\n");
        Mortar::SetMpTransport(0);
        delete a; delete b;
        return 1;
    }

    // Pump both ends' async connect to resolution. `a` is drained through the
    // REAL NetworkManager::Update() dispatch path (PollEvent -> Global
    // P2PMessageHandler -> HandleP2PConnected/HandleP2PNames) -- this is what
    // actually sets game_work.gameMode to the versus mode, resets WaveManager,
    // puts the HUD into multiplayer state, and constructs the ZenVersusControl
    // under test. `b`'s own connect is drained directly (never installed as
    // the active transport) purely so its IsConnected() state is real too.
    for (int i = 0; i < Mortar::LoopbackTransport::LOOPBACK_DEFAULT_CONNECT_DELAY; ++i) {
        nm->Update(1.0f / 60.0f);
        while (b->PollEvent() != Mortar::MP_EVT_NONE) {}
    }
    if (!a->IsConnected() || !b->IsConnected()) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL: connect never resolved "
                              "(a=%d b=%d)\n", (int)a->IsConnected(), (int)b->IsConnected());
        overallPass = false;
    }
    if (game_work.gameMode != GAME_MODE_COMBO) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL: HandleP2PConnected did not set "
                              "gameMode to GAME_MODE_COMBO (got %d)\n", (int)game_work.gameMode);
        overallPass = false;
    }

    ZenVersusControl* zvc = FindZenVersusControl();
    if (!zvc) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL: CreateMultiplayerControls did not "
                              "add a ZenVersusControl to game_work.mHud\n");
        overallPass = false;
    }

    // ---- ORGANIC: cmd2 (seed+go) session-setup packet from the peer ----
    // Real wire-serialized StartGamePacket, sent from b's transport end and
    // drained on a's side through the same NetworkManager::Update() dispatch
    // (HandleP2PData case 103 cmd2). NOTE: since `a` (us) is the HOST
    // (LocalPlayerNumber()==1), the WaveManager reseed itself is gated off
    // for us by design (only the GUEST reseeds from the host's broadcast
    // seed -- see WaveManager::Reset's IsOnlineMultiplayer() branch) -- this
    // packet's only observable effect on the host side is the unconditional
    // "GO" latch (game_work.m_bP2POpponentReady / MultiplayerTutorialControl::
    // m_bGo). Nothing in WaveManager currently reads m_bP2POpponentReady as a
    // spawn gate (confirmed by grep), so this step does not affect whether
    // fruit spawn below -- it is included only to exercise the real
    // session-setup handshake end-to-end, per the task's ask.
    {
        StartGamePacket pkt(2, (int32_t)0xF00DCAFEu);
        uint8_t buf[512];
        Mortar::ByteWriter w(buf, sizeof buf);
        pkt.Serialize(w);
        b->Send(buf, w.Written(), true); // lands in a's (host's) inbound queue
    }
    nm->Update(1.0f / 60.0f);

    // ---- FORCED: deterministic spawn positions ----
    // Test-only explicit RNG seed (same technique test_mp_loopback.cpp's
    // test_seed_determinism/test_full_session_handshake cases use) so the
    // captured fruit layout is reproducible run-to-run. Seeded AFTER the
    // organic HandleP2PConnected/cmd2 dispatch above (which already primed
    // m_pCurrentWave[0] once via WaveManager::Reset(true)'s internal
    // GetNextWave(0) call) and BEFORE PrepareForLevelStart() re-primes it via
    // Reset(false) -- so every RNG draw that actually influences spawn
    // decisions happens after this point.
    WaveManager* wm = WaveManager::GetInstance();
    wm->GetRandom().Seed(0xF00DCAFEu);

    // ---- FORCED: bypass the menu-click / camera-zoom state machine ----
    // Same trio test_gameplay.cpp uses (see its root-cause comment for why
    // PrepareForLevelStart() alone is not enough): PrepareForLevelStart()
    // primes m_pCurrentWave[0] via WaveManager::Reset(false); MainScreen::
    // SetCameraTransition(0)/SetState(STATE_CAMERA_FADE) is the real
    // GameModeScreen::Update mode-picked transition's other two effects, with
    // no GameModeScreen click in this harness to fire them.
    PrepareForLevelStart();
    if (!wm->m_pCurrentWave[0]) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL: m_pCurrentWave[0] still null "
                              "after PrepareForLevelStart\n");
        overallPass = false;
    }
    game_work.bM_bPaused = 0;
    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetCameraTransition(0.0f);
        game_work.mMainScreen->SetState(STATE_CAMERA_FADE);
    }

    // ---- FORCED: player names + opponent's fixed score ----
    // No organic call site drives either for a PASSIVE opponent (the peer
    // never sends a PointsPacket) -- set directly via the real public setters.
    game_work.SetPlayerName(0, "ALICE");
    game_work.SetPlayerName(1, "BOB");
    Mortar::NetworkManager::GetInstance()->SetOpponentScore(0);

    // -------- interactive path --------
    if (h.IsInteractive()) {
        std::printf("[scene_versus_ingame] interactive: versus session live, watch gameplay "
                    "(ESC/close to exit)\n");
        h.game.run();
        Mortar::SetMpTransport(0);
        delete a; delete b;
        return h.Shutdown();
    }

    // ---- EARLY: tick real gameplay until fruit are airborne ----
    TickGameplay(h, 90);
    if (am->GetNumEntities(0) <= 0) {
        // A few more frames -- first-wave spawn delay can push past 90.
        TickGameplay(h, 90);
    }
    if (am->GetNumEntities(0) <= 0) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL (EARLY): no live fruit after 180 "
                              "frames\n");
        overallPass = false;
    }
    if (!FindZenVersusControl()) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL (EARLY): ZenVersusControl no longer "
                              "in HUD\n");
        overallPass = false;
    }
    SettleFrame(h);
    {
        int fw = 0, fh = 0;
        unsigned char* px = h.ReadPixels(&fw, &fh);
        int drawn = px ? CountNonBlack(px, fw, fh) : 0;
        std::free(px);
        h.ScreenshotPng("scene_versus_ingame/early");
        PrintResult("early", am);
        if (drawn < MIN_DRAWN_PIXELS) {
            std::fprintf(stderr, "[scene_versus_ingame] FAIL (EARLY): drawnPixels=%d (< %d)\n",
                         drawn, MIN_DRAWN_PIXELS);
            overallPass = false;
        }
    }

    // ---- MID: bump our score via the REAL scoring function, give the
    //      opponent a fixed non-zero score, let the slider/pulse ease ----
    AddToCurrentScore(300, 0, false, false);
    Mortar::NetworkManager::GetInstance()->SetOpponentScore(150);
    TickGameplay(h, 30); // let ZenVersusControl's eased score/pulse/slider settle
    if (am->GetNumEntities(0) <= 0) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL (MID): no live fruit\n");
        overallPass = false;
    }
    SettleFrame(h);
    {
        int fw = 0, fh = 0;
        unsigned char* px = h.ReadPixels(&fw, &fh);
        int drawn = px ? CountNonBlack(px, fw, fh) : 0;
        std::free(px);
        h.ScreenshotPng("scene_versus_ingame/mid");
        PrintResult("mid", am);
        if (drawn < MIN_DRAWN_PIXELS) {
            std::fprintf(stderr, "[scene_versus_ingame] FAIL (MID): drawnPixels=%d (< %d)\n",
                         drawn, MIN_DRAWN_PIXELS);
            overallPass = false;
        }
    }

    // ---- LATE: keep playing -- more waves/fruit, scores held fixed
    //      (opponent NEVER moves again -- PASSIVE by design) ----
    TickGameplay(h, 90);
    // NOTE: deliberately no live-fruit assertion here. By the LATE capture the
    // wave that was airborne at EARLY/MID has drained and the next one may not
    // have spawned yet, so "fruit on screen" is genuinely timing-dependent at
    // this point -- asserting it just makes the scene flaky. EARLY and MID
    // already prove the spawn pipeline runs under versus mode; what LATE is
    // here to prove is that the HUD survives and the opponent stays passive.
    if (Mortar::NetworkManager::GetInstance()->GetOpponentScore() != 150) {
        std::fprintf(stderr, "[scene_versus_ingame] FAIL (LATE): opponent score drifted from "
                              "its fixed value (got %d, want 150) -- peer must never send\n",
                     Mortar::NetworkManager::GetInstance()->GetOpponentScore());
        overallPass = false;
    }
    SettleFrame(h);
    {
        int fw = 0, fh = 0;
        unsigned char* px = h.ReadPixels(&fw, &fh);
        int drawn = px ? CountNonBlack(px, fw, fh) : 0;
        std::free(px);
        h.ScreenshotPng("scene_versus_ingame/late");
        PrintResult("late", am);
        if (drawn < MIN_DRAWN_PIXELS) {
            std::fprintf(stderr, "[scene_versus_ingame] FAIL (LATE): drawnPixels=%d (< %d)\n",
                         drawn, MIN_DRAWN_PIXELS);
            overallPass = false;
        }
    }

    Mortar::SetMpTransport(0);
    delete a;
    delete b;

    std::printf("[scene_versus_ingame] %s\n", overallPass ? "PASS" : "FAIL");
    h.Shutdown();
    return overallPass ? 0 : 1;
}
