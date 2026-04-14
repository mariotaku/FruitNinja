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
#include "screens/MainScreen.h"
#include "hud/HUD.h"
#include "entities/ActorManager.h"
#include "entities/SlashEntity.h"
#include "entities/SplatEntity.h"
#include "entities/BombBlast.h"
#include "hud/SliceEffect.h"
#include "particle/PSPParticleManager.h"
#include "input/InputManager.h"
#include "input/Touch.h"
#include "util/StringHash.h"
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

    // Load background texture → task state +0xfc (matches original GameInit lines 159-170)
    // Only if not already loaded (original: SmartPtr cast-to-bool guard)
    // Fast hw: "gb_game.tex" (DAT_0016c9f0 → 0x001BC923)
    // Slow hw: "gb_game_sml.tex" (DAT_0016c9f4 → 0x001BC92F)
    GameTaskState* ts = GetTaskState();
    if (!ts->pBackgroundTexture.IsValid()) {
        const char* bgTex = game->IsFastHardware() ? "gb_game.tex" : "gb_game_sml.tex";
        ts->pBackgroundTexture = Mortar::TextureManager::LoadLocalisedTexture(bgTex);
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

    // TODO: MissControl ×3, ScoreControl, CoinCounter, TimeControl
    // TODO: Entity::HeapCreate, ActorManager::Initialise
    // TODO: Pre-spawn 30× entities, SplatEntity/WaveManager/BombFlash pools
    // TODO: SoundManager::Initialise + SetSFXVolume
}

// Matches GameUpdate (0x16bed0, 359 lines) — main gameplay loop
void GameUpdate(float dt, bool active) {
    static int s_FrameCount = 0;
    const bool earlyFrame = s_FrameCount < 3;
    if (earlyFrame) printf("GameUpdate[f%d]: enter dt=%.4f active=%d\n", s_FrameCount, dt, active ? 1 : 0);
    s_FrameCount++;

    Game* game = Game::GetInstance();
    if (!game) { if (earlyFrame) printf("GameUpdate: game NULL, return\n"); return; }

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

    if (earlyFrame) printf("GameUpdate: -> PSPParticleManager::Update\n");
    Mortar::PSPParticleManager::GetInstance().Update(dt);

    // CriticalFlash fade-out timer — single static state in BombHit.cpp.
    FN::UpdateCriticalFlash(dt);

    if (earlyFrame) printf("GameUpdate: -> SplatEntity::UpdateActive (active=%d)\n", active ? 1 : 0);
    if (active) SplatEntity::UpdateActive(dt);

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
    if (!game) { if (earlyFrame) printf("GameDraw: game NULL, return\n"); return; }

    GameTaskState* ts = GetTaskState();
    if (earlyFrame) printf("GameDraw: -> SetupPerspective cam=%p\n", (void*)game->pCamera);

    // 1. Camera projection
    if (game->pCamera)
        game->pCamera->SetupPerspective(0, false);

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();

    if (earlyFrame) printf("GameDraw: -> background quad bgTex_valid=%d\n",
                           ts ? ts->pBackgroundTexture.IsValid() : -1);

    // Background texture quad
    // Matches binary: Scale(481, 321, 1) Translate(0, 0, -5599) DrawQuad(cropped UVs)
    if (ts->pBackgroundTexture.IsValid()) {
        ts->pBackgroundTexture->Set();

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(481.0f, 321.0f, 1.0f);
        mat.GlobalTranslate44(Vec3(0.0f, 0.0f, -5599.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        Colour white(255, 255, 255, 255);
        game->renderer.DrawQuad(white, 0.03125f, 0.1875f, 0.96875f, 0.8125f);

        ts->pBackgroundTexture->UnSet();
    }

    Mortar::PSPParticleManager& pm = Mortar::PSPParticleManager::GetInstance();

    if (earlyFrame) printf("GameDraw: -> ActorManager::Draw am=%p\n", (void*)game->actorManager);
    // 2. 3D entities (ActorManager::Draw — fruit, bomb meshes)
    if (game->actorManager)
        game->actorManager->Draw(game->renderer);

    if (earlyFrame) printf("GameDraw: -> HUD::BeginDraw hud=%p\n", (void*)game->hud);
    // 3. HUD::BeginDraw
    if (game->hud)
        game->hud->BeginDraw(dt);

    if (earlyFrame) printf("GameDraw: -> HUD::Draw(0x40)\n");
    // 4-6. HUD layers drawn before the logo/particles block
    if (game->hud) {
        game->hud->Draw(0x40);
        if (earlyFrame) printf("GameDraw: -> SplatEntity::DrawActive\n");
        // SplatEntity::DrawActiveSplats (0x180344) — juice splats on
        // the background plane, drawn behind particles and fruit.
        SplatEntity::DrawActive();
        if (earlyFrame) printf("GameDraw: -> HUD::Draw(0x80)\n");
        // Layer 0x80 — DojoScreen + AboutScreen draw here. Binary's
        // LayerFlags for both screens is 0x80 (verified in RE notes).
        game->hud->Draw(0x80);
    }

    if (earlyFrame) printf("GameDraw: -> pm.Draw(-1)\n");
    // 7. Background particles (binary calls pm.Draw(-1) here — earliest
    //    visible layer, drawn behind the logo and shade)
    pm.Draw(-1);

    // 8. [vtable loop over 16 objects — not yet ported]

    if (earlyFrame) printf("GameDraw: -> pm.Draw(0)\n");
    // 9. Mid particles (pm.Draw(0))
    pm.Draw(0);

    if (earlyFrame) printf("GameDraw: -> SlashEntity::Draw\n");
    // 10. SlashEntity blade ribbon — drawn BEFORE MainScreen so the logo
    //     and shade appear in front of the blade. Matches binary DrawSlices
    //     call site at GameDraw 0x16b888.
    if (g_pSlashEntity) g_pSlashEntity->Draw();

    if (earlyFrame) printf("GameDraw: -> SliceEffect_Draw\n");
    // Slice-line effect pool (DrawSlices @ 0x169ac8). Draws the white
    // streak lines spawned by Fruit::CollisionResponse + Fruit::Slice.
    FN::SliceEffect_Draw(dt);

    // Critical / rare-fruit full-screen tint — fades over ~0.3s.
    // Sits between the slice lines and the MainScreen logo so it
    // reads as a "flash behind the UI".
    FN::DrawCriticalFlash();

    if (earlyFrame) printf("GameDraw: -> HUD::Draw(0x01)\n");
    // 11. MainScreen (HUD layer 0x01) — logo + shade on top of the blade
    if (game->hud) game->hud->Draw(0x01);

    if (earlyFrame) printf("GameDraw: -> pm.Draw(1)\n");
    // 12. Foreground particles (pm.Draw(1)) — over logo, under buttons
    pm.Draw(1);

    // 13. [WaveManager::Draw — not yet ported]

    // 14-16. HUD overlay layers
    if (game->hud) {
        game->hud->Draw(0x08);    // buttons
        game->hud->Draw(0x100);   // overlays
        // BombBlast shockwave rings + white flash overlay sit inside the
        // 0x200 "bomb hit" layer in the binary (DrawBombHit @ 0x16b73c
        // and DrawActiveBlasts @ 0x171aa0 are called here).
        BombBlast::DrawActiveBlasts();
        FN::DrawBombHit();
        game->hud->Draw(0x200);   // bomb hit overlay
        game->hud->Draw(0x400);   // top layer
    }
    if (earlyFrame) printf("GameDraw[f%d]: exit\n", s_DrawCount - 1);
}

// Matches GameExit (0x16cf74, 98 lines) — per-session cleanup
void GameExit_Handler() {
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("GameExit: cleaning up\n");

    // Release background texture
    GameTaskState* ts = GetTaskState();
    ts->pBackgroundTexture.Clear();

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
        game->hud = NULL;
    }
    game->mainScreen = NULL;

    // TODO: Coin::ClearCoins, SaveCurrentData
    // TODO: WaveManager::Destroy, PSPParticleManager::ClearEmitters
    // TODO: ActorManager::Clear + Destroy, Entity::HeapDestroy
}
