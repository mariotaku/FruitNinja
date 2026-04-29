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
#include "entities/ActorManager.h"
#include "entities/SlashEntity.h"
#include "engine/MenuBackground.h"
#include "entities/SplatEntity.h"
#include "entities/BombBlast.h"
#include "hud/SliceEffect.h"
#include "hud/MissControl.h"
#include "particle/PSPParticleManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "input/InputManager.h"
#include "input/Touch.h"
#include "util/StringHash.h"
#include "debug/DebugFlags.h"
#include "UpdateMusic.h"
#include "audio/GameSound.h"
#include "audio/SoundManager.h"
#include <cstdio>

// Matches GameInit (0x16c644, 274 lines) — per-session setup
void GameInit(unsigned long) {
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("GameInit: creating HUD + MainScreen\n");

    // Create HUD (matches original: Game+0x3c)
    if (!game->hud) {
        game->hud = new HUD();
    }

    // Pool allocation requires the HUD to exist. MissControl was
    // deferred in GameInitialise because HUD wasn't ready there.
    MissControl::AllocatePool();

    // Load default background via ChangeBackground (writes to the shared
    // file-static MenuBackground slot). This is the same slot the
    // renderer reads via GetCurrentBackground() and the same slot
    // ItemManager::SetEquippedItem(BACKGROUND, ...) updates -- so the
    // shop's Equip-background flow now actually swaps the visible bg.
    // Binary GameInit @ 0x..: same call shape (ChangeBackground(NULL)
    // -> default "gb_game" + platform suffix).
    if (GetCurrentBackground() == nullptr) {
        ChangeBackground(nullptr);
    }

    // Create MainScreen and add to HUD (matches original GameInit lines 190-200)
    printf("GameInit: about to new MainScreen\n");
    MainScreen* mainScreen = new MainScreen(*game);
    printf("GameInit: MainScreen ctor returned, ptr=%p\n", (void*)mainScreen);
    game->hud->AddControl(mainScreen);
    printf("GameInit: AddControl done\n");
    game->mainScreen = mainScreen;
    printf("GameInit: mainScreen field set\n");

    // Create the single SlashEntity for touch-trail rendering. The binary
    // uses a 2-player array; the port keeps one for single-touch.
    if (!g_pSlashEntity) {
        printf("GameInit: about to new SlashEntity\n");
        g_pSlashEntity = new SlashEntity();
        printf("GameInit: SlashEntity ctor returned\n");
        g_pSlashEntity->Init();
        printf("GameInit: SlashEntity Init done\n");
    }
    printf("GameInit: complete\n");

    // Touch input is now polled from Mortar::Touch inside MenuButton::Update
    // and SlashEntity::Update — no InputManager callbacks needed. The
    // TouchDown_0 / TouchMove_X0 / TouchUp_0 action hashes are still fired
    // from SDLInputTranslator (for hypothetical future keyboard-style
    // bindings) but have no subscribers.

    // TODO: MissControl x3, ScoreControl, CoinCounter, TimeControl
    // TODO: Entity::HeapCreate, ActorManager::Initialise
    // TODO: Pre-spawn 30x entities, SplatEntity/BombFlash pools
    WaveManager::GetInstance()->Init();  // stub (parses wave XML in binary)
    // TODO: SoundManager::Initialise + SetSFXVolume
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
