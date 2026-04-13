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
    MainScreen* mainScreen = new MainScreen(*game);
    game->hud->AddControl(mainScreen);
    game->mainScreen = mainScreen;

    // Create the single SlashEntity for touch-trail rendering. The binary
    // uses a 2-player array; the port keeps one for single-touch.
    if (!g_pSlashEntity) {
        g_pSlashEntity = new SlashEntity();
        g_pSlashEntity->Init();
    }

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
    Game* game = Game::GetInstance();
    if (!game) return;

    // Advance touch state machine (phase -1 → 0 edge transition). Called
    // before any polling consumers (MenuButton, SlashEntity) so they see
    // fresh state this frame. Matches binary's Mortar::Touch::Update position
    // in the early-frame path.
    Mortar::Touch::GetInstance().Update();

    // Tick + update the post-explosion hit timer BEFORE ActorManager
    // updates bombs, so bombs spawned this frame see the freshly ticked
    // value. Matches binary UpdateBombHit (0x16a1a8) call order inside
    // GameUpdate at 0x16bed0.
    const float prevBombTimer = game->bombHitTimer;
    if (game->bombHitTimer > 0.0f) {
        game->bombHitTimer -= dt;
        if (game->bombHitTimer < 0.0f) game->bombHitTimer = 0.0f;
    }
    FN::UpdateBombHit(prevBombTimer);

    // Update entities — ONLY when the gameplay is active. Matches binary
    // GameUpdate's `if (active)` branch: ActorManager::Update is gated on
    // `active`, so menu entities (e.g. the bomb in the Quit button) don't
    // accumulate per-frame rotation while the player is on the menu.
    // See docs/engine/rendering-pipeline.md / GameUpdate RE (0x16bed0).
    if (active && game->actorManager)
        game->actorManager->Update(dt);

    // Tick particle system (spawn, physics, emitter lifetime) — always runs
    Mortar::PSPParticleManager::GetInstance().Update(dt);

    // Tick splat pool — gated on gameplay active, matches
    // UpdateActiveSplats call site inside GameUpdate (0x16bed0).
    if (active) SplatEntity::UpdateActive(dt);

    // SlashEntity runs in every state (menu + gameplay) so the blade trail
    // is visible everywhere. The binary gates this on `active` too, but the
    // port keeps it unconditional for testing.
    if (g_pSlashEntity) g_pSlashEntity->Update(dt);

    // Update all HUD controls (MainScreen state machine, buttons) — always runs
    if (game->hud)
        game->hud->Update(dt);

    // Tick the camera — decays the bomb-hit shake intensity and ramps
    // the m_Target offset used by SetupPerspective each frame. Without
    // this call, CreateCameraShake just latches state that nothing
    // ever reads. Matches binary GameUpdate 0x16bed0 where the camera
    // update is part of the per-frame entity loop.
    if (game->pCamera)
        game->pCamera->UpdateCamera(dt);

    // TODO: Full 359-line GameUpdate: time scaling, bomb hit, wave speed,
    //       SlashEntity::PreUpdate, SplatEntity, WaveManager
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
    Game* game = Game::GetInstance();
    if (!game) return;

    GameTaskState* ts = GetTaskState();

    // 1. Camera projection
    if (game->pCamera)
        game->pCamera->SetupPerspective(0, false);

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();

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

    // 2. 3D entities (ActorManager::Draw — fruit, bomb meshes)
    if (game->actorManager)
        game->actorManager->Draw(game->renderer);

    // 3. HUD::BeginDraw
    if (game->hud)
        game->hud->BeginDraw(dt);

    // 4-6. HUD layers drawn before the logo/particles block
    if (game->hud) {
        game->hud->Draw(0x40);
        // SplatEntity::DrawActiveSplats (0x180344) — juice splats on
        // the background plane, drawn behind particles and fruit.
        SplatEntity::DrawActive();
        // [TODO: Fruit::DrawShadows / SlashEntity::PreDraw (ghost) /
        //  BombBlast / BombFlash — not yet ported]
        // [TODO: HUD::Draw(0x80) — layer not in port yet]
    }

    // 7. Background particles (binary calls pm.Draw(-1) here — earliest
    //    visible layer, drawn behind the logo and shade)
    pm.Draw(-1);

    // 8. [vtable loop over 16 objects — not yet ported]

    // 9. Mid particles (pm.Draw(0))
    pm.Draw(0);

    // 10. SlashEntity blade ribbon — drawn BEFORE MainScreen so the logo
    //     and shade appear in front of the blade. Matches binary DrawSlices
    //     call site at GameDraw 0x16b888.
    if (g_pSlashEntity) g_pSlashEntity->Draw();

    // Slice-line effect pool (DrawSlices @ 0x169ac8). Draws the white
    // streak lines spawned by Fruit::CollisionResponse + Fruit::Slice.
    // Ticked + drawn in the same pass — returns expired slices to the
    // pool automatically.
    FN::SliceEffect_Draw(dt);

    // 11. MainScreen (HUD layer 0x01) — logo + shade on top of the blade
    if (game->hud) game->hud->Draw(0x01);

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
