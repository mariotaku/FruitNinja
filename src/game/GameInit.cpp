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

    // Load background texture → task state +0xfc (matches original GameInit step 7)
    GameTaskState* ts = GetTaskState();
    ts->pBackgroundTexture = Mortar::TextureManager::LoadLocalisedTexture("bg_fruit_ninja.tex");

    // Create MainScreen and add to HUD (matches original GameInit lines 190-200)
    MainScreen* mainScreen = new MainScreen(*game);
    game->hud->AddControl(mainScreen);
    game->mainScreen = mainScreen;

    // TODO: MissControl ×3, ScoreControl, CoinCounter, TimeControl
    // TODO: Entity::HeapCreate, ActorManager::Initialise
    // TODO: Pre-spawn 30× entities, SplatEntity/WaveManager/BombFlash pools
    // TODO: SoundManager::Initialise + SetSFXVolume
}

// Matches GameUpdate (0x16bed0, 359 lines) — main gameplay loop
void GameUpdate(float dt, bool active) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // Update entities
    if (game->actorManager)
        game->actorManager->Update(dt);

    // Update all HUD controls (MainScreen state machine, buttons)
    if (game->hud)
        game->hud->Update(dt);

    // TODO: Full 359-line GameUpdate: time scaling, bomb hit, wave speed,
    //       SlashEntity::PreUpdate, SplatEntity, WaveManager, ActorManager,
    //       PSPParticleManager, FruitCamera::UpdateShake, HUD::Update
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

    // 2. Background texture quad (matches original: Texture::Set(taskState+0xfc))
    // Original: Scale(481, 321, 0) Translate(0, 0, -5599) DrawQuad(UV: 0.03125, 0.96875, 0.1875, 0.8125)
    // Port: translate to ortho center (480, 320), same cropped UVs
    if (ts->pBackgroundTexture.IsValid()) {
        ts->pBackgroundTexture->Set();

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(481.0f, 321.0f, 1.0f);
        mat.GlobalTranslate44(Vec3(480.0f, 320.0f, -5599.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        Colour white(255, 255, 255, 255);
        game->renderer.DrawQuad(white, 0.03125f, 0.1875f, 0.96875f, 0.8125f);

        ts->pBackgroundTexture->UnSet();
    }

    // 3. 3D entities
    if (game->actorManager)
        game->actorManager->Draw(game->renderer);

    // 4. HUD::BeginDraw
    if (game->hud)
        game->hud->BeginDraw(dt);

    // 5-6. HUD layers (original draws 0x40, splats, slashes, 0x80, particles,
    //       then 0x01 for MainScreen, then 0x08 for buttons, then overlays)
    if (game->hud) {
        game->hud->Draw(0x40);    // layer 0x40
        game->hud->Draw(0x01);    // MainScreen (blurry_backing + logos)
        game->hud->Draw(0x08);    // buttons
        game->hud->Draw(0x100);   // overlays
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
