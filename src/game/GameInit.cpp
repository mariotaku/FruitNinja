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
#include "screens/MainScreen.h"
#include "hud/HUD.h"
#include "entities/ActorManager.h"
#include "entities/SlashEntity.h"
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

    // Update entities — ONLY when the gameplay is active. Matches binary
    // GameUpdate's `if (active)` branch: ActorManager::Update is gated on
    // `active`, so menu entities (e.g. the bomb in the Quit button) don't
    // accumulate per-frame rotation while the player is on the menu.
    // See docs/engine/rendering-pipeline.md / GameUpdate RE (0x16bed0).
    if (active && game->actorManager)
        game->actorManager->Update(dt);

    // Tick particle system (spawn, physics, emitter lifetime) — always runs
    Mortar::PSPParticleManager::GetInstance().Update(dt);

    // SlashEntity runs in every state (menu + gameplay) so the blade trail
    // is visible everywhere. The binary gates this on `active` too, but the
    // port keeps it unconditional for testing.
    if (g_pSlashEntity) g_pSlashEntity->Update(dt);

    // Update all HUD controls (MainScreen state machine, buttons) — always runs
    if (game->hud)
        game->hud->Update(dt);

    // TODO: Full 359-line GameUpdate: time scaling, bomb hit, wave speed,
    //       SlashEntity::PreUpdate, SplatEntity, WaveManager,
    //       FruitCamera::UpdateShake
}

// Matches GameDraw (0x16b888, 211 lines) — full render frame
// See docs/structs/game.md "GameDraw" and Ghidra decompilation.
// Original draw order (simplified for current port state):
//   1. FruitCamera::SetupPerspective
//   2. Background texture quad (g_TaskState+0xfc)
//   3. ActorManager::Draw (3D entities)
//   4. HUD::BeginDraw
//   5. HUD::Draw(0x40) — layer 0x40
//   6. HUD::Draw(0x01) — MainScreen (blurry_backing + logos)
//   7. HUD::Draw(0x08) — buttons
//   8. HUD::Draw(0x100..0x400) — overlays
void GameDraw(float dt, bool active) {
    Game* game = Game::GetInstance();
    if (!game) return;

    GameTaskState* ts = GetTaskState();

    // 1. Camera projection
    if (game->pCamera)
        game->pCamera->SetupPerspective(0, false);

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();

    // 2. Background texture quad
    // Matches binary: Scale(481, 321, 1) Translate(0, 0, -5599) DrawQuad(cropped UVs)
    // The 481×321 scale covers the centred 480×320 ortho viewport with a
    // one-pixel fudge. Position (0, 0) is the ortho centre.
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

    // Particles are layered by template `<useDepth>` (three values appear in
    // particles_fast.xml: 1 = background, 0 = default/mid, -1 = foreground).
    // Binary's PSPParticleManager::Draw filters each pass by an exact match.
    // Without RE'ing the original caller order, we approximate: background
    // behind 3D, default between 3D and HUD, foreground over HUD.
    Mortar::PSPParticleManager& pm = Mortar::PSPParticleManager::GetInstance();

    // 3a. Background particles (useDepth=1)
    pm.Draw(1);

    // 3b. 3D entities
    if (game->actorManager)
        game->actorManager->Draw(game->renderer);

    // 4. Mid particles (useDepth=0) — most juice/splat/smoke FX.
    pm.Draw(0);

    // 5. HUD::BeginDraw
    if (game->hud)
        game->hud->BeginDraw(dt);

    // 6. HUD layers (original draws 0x40, splats, slashes, 0x80, particles,
    //    then 0x01 for MainScreen, then 0x08 for buttons, then overlays)
    if (game->hud) {
        game->hud->Draw(0x40);    // layer 0x40
        game->hud->Draw(0x01);    // MainScreen (blurry_backing + logos)
        game->hud->Draw(0x08);    // buttons
        game->hud->Draw(0x100);   // overlays
        game->hud->Draw(0x200);   // bomb hit overlay
        game->hud->Draw(0x400);   // top layer
    }

    // 7. Foreground particles (useDepth=-1) — rim_spark, trails, sparkles.
    pm.Draw(-1);

    // 8. Blade trail — drawn last so it's always on top.
    if (g_pSlashEntity) g_pSlashEntity->Draw();
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
