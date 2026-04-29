//
// State 2 handlers: GameInit, GameUpdate, GameDraw, GameExit
// Original: GameInit 0x16c644 (274 lines), GameUpdate 0x16bed0 (359 lines),
//           GameDraw 0x16b888 (211 lines), GameExit 0x16cf74 (98 lines)
//
// Currently: simplified versions that create HUD + MainScreen.
// Will grow to match full 274/359/211/98 line originals as port progresses.
//

#include "GameTaskState.h"
#include "Game.h"
#include "FruitCamera.h"
#include "BombHit.h"
#include "WaveManager.h"
#include "FruitSaveData.h"
#include "screens/MainScreen.h"
#include "hud/HUD.h"
#include "hud/TimeControl.h"
#include "hud/ScoreControl.h"
#include "hud/CoinCounter.h"
#include "hud/MissControl.h"
#include "entities/ActorManager.h"
#include "entities/SlashEntity.h"
#include "entities/BombFlash.h"
#include "engine/MenuBackground.h"
#include "entities/SplatEntity.h"
#include "entities/BombBlast.h"
#include "hud/SliceEffect.h"
#include "particle/PSPParticleManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "input/InputManager.h"
#include "input/Touch.h"
#include "util/StringHash.h"
#include "debug/DebugFlags.h"
#include "UpdateMusic.h"
#include "audio/GameSound.h"
#include "GameOver.h"
#include "audio/SoundManager.h"
#include <cstdio>

// Matches GameInit (0x16c644, 274 lines) — per-session setup.
// Call order matches binary 23-step sequence (see inline step comments).
void GameInit(unsigned long) {
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("GameInit: enter\n");

    // step 1: HUD allocation (Game+0x3c)
    if (!game->hud) {
        game->hud = new HUD();
    }

    // step 2: HUD::Release post-construction housekeeping
    // Binary calls HUD::Release(hud) immediately after ctor — no-op in port
    // (HUD ctor already initialises cleanly).

    // step 3: 3x MissControl instances + AddControl + MissControl::CreatePool(0xc, hud).
    // Binary allocates 3 MissControl objects (operator new(0x94) x3) then registers
    // them with the HUD, then calls a pool-creation helper with count=0xc.
    // Port: AllocatePool() wraps the ctor loop + HUD registration in one call.
    // TODO: real MissControl positions come from GOT+0x30 table — using Vec3(0,0,0) placeholder.
    MissControl::AllocatePool();

    // step 4: ScoreControl (size 0x100) + AddControl
    ScoreControl* sc = new ScoreControl();
    game->hud->AddControl(sc);

    // step 5: CoinCounter (size 0xD4) + AddControl -> game.pCoinCounter (Game+0x178)
    CoinCounter* cc = new CoinCounter();
    game->hud->AddControl(cc);
    game->pCoinCounter = cc;

    // step 6: TimeControl (size 0x108) + CountDown(90.9f) + AddControl -> game.pTimeCtrl (Game+0x180)
    // DAT_0016c9cc = 90.9 (Zen mode initial countdown time)
    TimeControl* tc = new TimeControl();
    tc->CountDown(90.9f);
    game->pTimeCtrl = tc;
    game->hud->AddControl(tc);

    // step 7: Background load (gb_game.tex / gb_game_sml.tex IsFastHardware branch)
    // Binary GameInit @ 0x..: same call shape (ChangeBackground(NULL) -> default "gb_game" + suffix).
    if (GetCurrentBackground() == nullptr) {
        ChangeBackground(nullptr);
    }

    // step 8: MeshManager loads
    // TODO: MeshManager::Load calls — not yet ported.

    // step 9: SliceEffect list/pool init
    // TODO: SliceEffect pool creation — not yet ported.

    // step 10: Flag init at +0x112/+0x114 (Game struct fields)
    // TODO: game field_0x112 / field_0x114 not yet mapped in port Game struct.

    // step 11: MainScreen allocation
    printf("GameInit: about to new MainScreen\n");
    MainScreen* mainScreen = new MainScreen(*game);
    printf("GameInit: MainScreen ctor returned, ptr=%p\n", (void*)mainScreen);
    game->mainScreen = mainScreen;
    printf("GameInit: mainScreen field set\n");

    // step 12: PauseScreen allocation
    // TODO: PauseScreen not yet ported — skip.

    // step 13: TutorialControl allocation
    // (game->pTutorialCtrl already allocated in GameInitialise; binary re-allocs here)
    // TODO: confirm whether binary re-allocs or reuses from GameInitialise.

    // step 14: AddControl x3 batch (MainScreen + PauseScreen + TutorialControl)
    game->hud->AddControl(mainScreen);
    printf("GameInit: AddControl(mainScreen) done\n");
    // TODO: AddControl(pauseScreen) when PauseScreen is ported.
    // TODO: AddControl(tutorialControl) — pTutorialCtrl already wired in GameInitialise.

    // step 15: Entity::HeapCreate
    // TODO: Entity::HeapCreate — not yet ported.

    // step 16: ActorManager::Initialise + RegisterFactory + RegisterHashConverter
    // TODO: ActorManager full init — not yet ported.

    // step 17: WaveManager::Init()
    WaveManager::GetInstance()->Init();

    // step 18: GameTaskInitInput()
    // TODO: GameTaskInitInput — not yet ported.

    // step 19: 30x prespawn loop: do { Add(0); Add(1); Add(4); flags|=0x11 } x30
    // TODO: prespawn loop — not yet ported.

    // step 20: SplatEntity::CreatePool(0x80)
    // TODO: SplatEntity::CreatePool(0x80) — not yet ported.

    // step 21: WaveManager::Resume() — MUST come AFTER prespawn + SplatEntity pool
    // Binary: WaveManager::Resume (0x00124b1c) — restores wave state from save.
    WaveManager::GetInstance()->Resume();

    // step 22: BombFlash::CreatePool(0x20)
    BombFlash::CreatePool(0x20);

    // step 23: SoundManager::Initialise + SetSFXVolume
    // TODO: SoundManager::Initialise + SetSFXVolume — not yet ported.

    printf("GameInit: complete\n");

    // Port specific: SlashEntity for touch-trail rendering.
    // Binary uses a 2-player array in ActorManager; port keeps one global for single-touch.
    if (!g_pSlashEntity) {
        printf("GameInit: about to new SlashEntity\n");
        g_pSlashEntity = new SlashEntity();
        printf("GameInit: SlashEntity ctor returned\n");
        g_pSlashEntity->Init();
        printf("GameInit: SlashEntity Init done\n");
    }
}

// Matches GameUpdate (0x16bed0, 359 lines) — main gameplay loop
void GameUpdate(float dt, bool active) {
    static int s_FrameCount = 0;
    const bool earlyFrame = s_FrameCount < 3;
    if (earlyFrame) printf("GameUpdate[f%d]: enter dt=%.4f active=%d\n", s_FrameCount, dt, active ? 1 : 0);
    s_FrameCount++;

    Game* game = Game::GetInstance();
    if (!game) { if (earlyFrame) printf("GameUpdate: game nullptr, return\n"); return; }

    if (earlyFrame) printf("GameUpdate: -> Touch::Update\n");
    Mortar::Touch::GetInstance().Update();

    if (earlyFrame) printf("GameUpdate: -> bombHitTimer tick\n");
    const float prevBombTimer = game->bombHitTimer;
    if (game->bombHitTimer > 0.0f) {
        game->bombHitTimer -= dt;
        if (game->bombHitTimer < 0.0f) game->bombHitTimer = 0.0f;
    }
    if (earlyFrame) printf("GameUpdate: -> UpdateBombHit\n");
    FN::UpdateBombHit(prevBombTimer);

    // Binary 0x0016c284: if bombHitTimer crossed 1.5 downward, trigger GameOver.
    // TODO: add per-bomb m_bMenuBombHit gate when Bomb state is accessible here.
    if (prevBombTimer > 1.5f && game->bombHitTimer <= 1.5f && !game->pauseFlag) {
        FN::GameOver(-1, -1.0f, -1);
    }

    if (earlyFrame) printf("GameUpdate: -> ActorManager::Update active=%d am=%p\n",
                           active ? 1 : 0, (void*)game->actorManager);
    if (active && game->actorManager)
        game->actorManager->Update(dt);

    // WaveManager::Update @ 0x001259d8 — stubbed; binary pumps wave
    // spawners + blitz combo + PowerUpManager from here.
    if (active) WaveManager::GetInstance()->Update(dt);

    // FruitSaveData::Update @ 0x0012b3dc — ticks achievement in-progress
    // timers so unlock popups fade in/out. Stubbed; wired so the call
    // site lights up as soon as the stub becomes real.
    if (game->pSaveData) game->pSaveData->Update(dt, game->hud);

    // Binary GameUpdate (0x16bed0) call order inside LoadingJob::IsLoaded() guard:
    //   SoundManager::Update(sm, scaledDt)
    //   GameSound::Update()
    //   UpdateMusic(scaledDt)     <- spec: docs/systems/music-state.md, Callers section
    //   ItemManager::Update(im, scaledDt)
    // LoadingJob not yet ported; calls run unconditionally here until it is.
    // SoundManager::Update is a no-op stub in the port (not yet RE'd).
    if (game->pGameSound) game->pGameSound->Update(dt);
    UpdateMusic(dt);

    if (earlyFrame) printf("GameUpdate: -> PSPParticleManager::Update\n");
    Mortar::PSPParticleManager::GetInstance().Update(dt);

    // CriticalFlash fade-out timer — single static state in BombHit.cpp.
    FN::UpdateCriticalFlash(dt);

    if (earlyFrame) printf("GameUpdate: -> SplatEntity::UpdateActiveSplats (active=%d)\n", active ? 1 : 0);
    if (active) SplatEntity::UpdateActiveSplats(dt);

    if (earlyFrame) printf("GameUpdate: -> SlashEntity::Update slash=%p\n", (void*)g_pSlashEntity);
    if (g_pSlashEntity) g_pSlashEntity->Update(dt);

    if (earlyFrame) printf("GameUpdate: -> HUD::Update hud=%p\n", (void*)game->hud);
    if (game->hud)
        game->hud->Update(dt);

    if (earlyFrame) printf("GameUpdate: -> FruitCamera::UpdateCamera cam=%p\n", (void*)game->pCamera);
    if (game->pCamera)
        game->pCamera->UpdateCamera(dt);

    if (earlyFrame) printf("GameUpdate[f%d]: exit\n", s_FrameCount - 1);
}

// Matches GameDraw (0x16b888, 211 lines) — full render frame.
//
// Binary draw order (verified from decompile, see comments inline):
//   1.  Camera + background quad
//   2.  ActorManager::Draw (3D entities — fruit, bomb, SlashEntity)
//   3.  HUD::BeginDraw
//   4.  HUD::Draw(0x40)
//   5.  SplatEntity::DrawActiveSplats / Fruit::DrawShadows /
//       SlashEntity::PreDraw / BombBlast::DrawActiveBlasts /
//       BombFlash::DrawActiveFlashes            [not yet ported]
//   6.  HUD::Draw(0x80)
//   7.  pm.Draw(-1)   — "background" particles (useDepth=-1, earliest)
//   8.  (vtable loop over 16 objects)           [not yet ported]
//   9.  pm.Draw(0)    — "mid" particles
//   10. DrawSlices    — SlashEntity::DrawSlice blade ribbon
//   11. HUD::Draw(0x01) — MainScreen (logo + shade). Drawn AFTER slash
//       so logo appears in front of the blade.
//   12. pm.Draw(1)    — "foreground" particles (drawn over logo,
//       under buttons)
//   13. WaveManager::Draw                       [not yet ported]
//   14. HUD::Draw(0x08) — buttons
//   15. HUD::Draw(0x100) + DrawBombHit + HUD::Draw(0x200)
//   16. HUD::Draw(0x400)
//
// Key insight: the particle layer indices map differently from what the
// layer names imply — pm.Draw(-1) actually draws EARLIEST (background)
// and pm.Draw(1) draws LATER (foreground, over logo).
void GameDraw(float dt, bool active) {
    static int s_DrawCount = 0;
    const bool earlyFrame = s_DrawCount < 3;
    if (earlyFrame) printf("GameDraw[f%d]: enter dt=%.4f active=%d\n", s_DrawCount, dt, active ? 1 : 0);
    s_DrawCount++;

    Game* game = Game::GetInstance();
    if (!game) { if (earlyFrame) printf("GameDraw: game nullptr, return\n"); return; }

    GameTaskState* ts = GetTaskState();
    if (earlyFrame) printf("GameDraw: -> SetupPerspective cam=%p\n", (void*)game->pCamera);

    // 1. Camera projection
    if (game->pCamera)
        game->pCamera->SetupPerspective(0, false);

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();

    Mortar::Texture* bgTex = GetCurrentBackground();
    if (earlyFrame) printf("GameDraw: -> background quad bgTex=%p\n", (void*)bgTex);

    // Background texture quad. Reads from the MenuBackground file-static
    // (same slot ItemManager::SetEquippedItem(BACKGROUND, ...) updates
    // via ChangeBackground), so a shop equip swaps the visible bg
    // immediately on the next frame.
    // Matches binary: Scale(481, 321, 1) Translate(0, 0, -5599) DrawQuad(cropped UVs)
    if (bgTex) {
        bgTex->Set();

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(481.0f, 321.0f, 1.0f);
        mat.GlobalTranslate44(Vec3(0.0f, 0.0f, -5599.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        Colour white(255, 255, 255, 255);
        game->renderer.DrawQuad(white, 0.03125f, 0.1875f, 0.96875f, 0.8125f);

        bgTex->UnSet();
    }

    Mortar::PSPParticleManager& pm = Mortar::PSPParticleManager::GetInstance();
    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();

    // === 1. ActorManager::Draw — 3D fruit/bomb/slash entities ===
    // Binary @ 0x0016ba10: SetDepthBufferWrite(1) + SetDepthBuffer(1)
    // just before ActorManager::Draw. Depth func stays at GL_LESS set
    // by BeginFrame — binary never overrides it.
    dm.SetDepthBufferWrite(true);
    dm.SetDepthBuffer(true);

    if (earlyFrame) printf("GameDraw: -> ActorManager::Draw am=%p\n", (void*)game->actorManager);
    // Port specific: wireframe debug mode (F2). glPolygonMode is loaded
    // optionally by gl_funcs — stays nullptr on GLES, so the toggle is a
    // silent no-op there.
    const bool wireframe = FN::g_DebugWireframe && glPolygonMode != nullptr;
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    if (game->actorManager)
        game->actorManager->Draw(game->renderer);
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // === 2. HUD::BeginDraw + post-actor block ===
    // Binary @ 0x0016ba10 right after ActorManager::Draw:
    //   SetDepthBuffer(1)       — depth test still ON
    //   SetDepthBufferWrite(0)  — writes OFF for HUD/splats/bomb blasts
    dm.SetDepthBuffer(true);
    dm.SetDepthBufferWrite(false);
    if (game->hud) {
        if (earlyFrame) printf("GameDraw: -> HUD::BeginDraw hud=%p\n", (void*)game->hud);
        game->hud->BeginDraw(dt);

        // 2a. HUD::Draw(0x40) — menu button sprites @ 0x0016ba5a
        if (earlyFrame) printf("GameDraw: -> HUD::Draw(0x40)\n");
        game->hud->Draw(0x40);

        // 2b. SplatEntity::DrawActiveSplats @ 0x0016ba6a
        if (earlyFrame) printf("GameDraw: -> SplatEntity::DrawActiveSplats\n");
        SplatEntity::DrawActiveSplats();

        // 2c. Fruit::DrawShadows @ 0x0016ba6e — TODO: not yet ported

        // 2d. SlashEntity::PreDraw @ 0x0016ba84 — TODO: blade pre-pass
        //     not yet ported

        // 2e. BombBlast::DrawActiveBlasts @ 0x0016ba88 — drawn HERE in
        //     the binary, NOT inside the 0x200 layer. Shockwave rings
        //     belong to this post-actor block.
        if (earlyFrame) printf("GameDraw: -> BombBlast::DrawActiveBlasts\n");
        BombBlast::DrawActiveBlasts();

        // 2f. BombFlash::DrawActiveFlashes @ 0x0016baf0 — TODO: not yet ported

        // 2g. HUD::Draw(0x80) — DojoScreen / AboutScreen @ 0x0016baf8
        if (earlyFrame) printf("GameDraw: -> HUD::Draw(0x80)\n");
        game->hud->Draw(0x80);
    }

    // === 3. Background particles ===
    // Binary pm.Draw(-1) @ 0x0016bb02 — drawn BEHIND the logo/shade.
    if (earlyFrame) printf("GameDraw: -> pm.Draw(-1)\n");
    pm.Draw(-1);

    // Binary @ 0x0016ba10 after pm.Draw(-1): SetDepthBuffer(0) turns
    // depth test off before the SlashEntity loop ×16 and all later
    // 2D passes. TODO: port the multiplayer SlashEntity loop
    // (currently just one global slash entity).
    dm.SetDepthBuffer(false);
    if (g_pSlashEntity) g_pSlashEntity->Draw();

    // === 4. Mid particles + slice lines + main-screen logo ===
    // Binary pm.Draw(0) @ 0x0016bb4a
    if (earlyFrame) printf("GameDraw: -> pm.Draw(0)\n");
    pm.Draw(0);

    // DrawSlices @ 0x0016bb52 — slash-line pool
    if (earlyFrame) printf("GameDraw: -> SliceEffect_Draw\n");
    FN::SliceEffect_Draw(dt);

    // HUD::Draw(0x01) — MainScreen logo / shade @ 0x0016bb5a
    if (earlyFrame) printf("GameDraw: -> HUD::Draw(0x01)\n");
    if (game->hud) game->hud->Draw(0x01);

    // pm.Draw(1) — foreground particles @ 0x0016bb6a
    if (earlyFrame) printf("GameDraw: -> pm.Draw(1)\n");
    pm.Draw(1);

    // WaveManager::Draw(0) @ 0x0016bb98 — stubbed (wave-banner overlay).
    WaveManager::GetInstance()->Draw(0);

    // === 5. HUD overlay layers + flash effects ===
    if (game->hud) {
        // HUD::Draw(0x08) — buttons @ 0x0016bba8
        game->hud->Draw(0x08);

        // MainScreen::DrawPostEffects @ 0x0016bbb0 — TODO

        // DrawCritHit (CriticalFlash) @ 0x0016bbd2 — gated on
        // critFlash > 0 && IsFastHardware. Port has CriticalFlash
        // implemented as FN::DrawCriticalFlash.
        FN::DrawCriticalFlash();

        // HUD::Draw(0x100) — overlays @ 0x0016bbde
        game->hud->Draw(0x100);

        // DrawBombHit @ 0x0016bbe6 — bomb-hit white flash, gated on
        // bombFlash > 0
        FN::DrawBombHit();

        // HUD::Draw(0x200) — bomb-hit overlay layer @ 0x0016bbec
        game->hud->Draw(0x200);

        // DrawNews / DrawStartFade @ 0x0016bbf0..0x0016bc12 — TODO

        // Debug overlay — fruit/bomb hitboxes (F1 toggle)
        FN::DebugHitbox_Draw();

        // HUD::Draw(0x400) — top layer @ 0x0016bd7c, ALWAYS fires
        // (binary places it OUTSIDE the `active` block).
        game->hud->Draw(0x400);
    }
    if (earlyFrame) printf("GameDraw[f%d]: exit\n", s_DrawCount - 1);
}

// Matches GameExit (0x16cf74, 98 lines) — per-session cleanup
void GameExit_Handler() {
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("GameExit: cleaning up\n");

    // Release background texture (shared MenuBackground slot)
    UnloadBackground();
    // Drop any leftover GameTaskState slot too (no-op if never used)
    GameTaskState* ts = GetTaskState();
    ts->pBackgroundTexture.SetNull();

    // Release SlashEntity + input callbacks
    if (g_pSlashEntity) {
        delete g_pSlashEntity;
        g_pSlashEntity = nullptr;
    }
    if (InputManager* im = InputManager::GetInstance()) {
        im->ClearActions();
    }

    // Release HUD (destroys all controls including MainScreen)
    if (game->hud) {
        game->hud->Release();
        delete game->hud;
        game->hud = nullptr;
    }
    game->mainScreen = nullptr;

    // TODO: Coin::ClearCoins
    FruitNinja_SaveCurrentData();  // stub (writes FruitSaveData XML in binary)
    WaveManager::GetInstance()->Destroy();  // stub (frees WAVE_INFO/WaveQue)
    // TODO: PSPParticleManager::ClearEmitters
    // TODO: ActorManager::Clear + Destroy, Entity::HeapDestroy
}
