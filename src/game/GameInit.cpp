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
// Original layer order:
//   1. Background (MainScreen draws bg + blurry overlay + logos)
//   2. 3D entities (ActorManager::Draw — fruit/bomb meshes)
//   3. HUD overlays (buttons, score, etc.)
void GameDraw(float dt, bool active) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // Setup ortho projection
    game->renderer.SetupGameOrtho();

    // Layer 1: MainScreen background + logos (layer 0x01)
    if (game->hud) {
        game->hud->BeginDraw(dt);
        game->hud->Draw(0x01);  // MainScreen only
    }

    // Layer 2: 3D entities (fruit meshes on buttons)
    if (game->actorManager)
        game->actorManager->Draw(game->renderer);

    // Layer 3: HUD buttons + overlays (layers 0x08, 0x40, etc.)
    if (game->hud) {
        game->hud->Draw(0xFFFE);  // everything except 0x01
    }
}

// Matches GameExit (0x16cf74, 98 lines) — per-session cleanup
void GameExit_Handler() {
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("GameExit: cleaning up\n");

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
