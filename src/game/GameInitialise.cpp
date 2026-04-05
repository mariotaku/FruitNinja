//
// GameInitialise — one-time engine bootstrap
// Original: 0x10bdfc (305 lines)
//
// Called once from Game::init(). Creates all engine singletons and loads shared data.
// NOT the same as GameInit which is the per-session State 2 handler.
//

#include "Game.h"
#include "FruitCamera.h"
#include "hud/HUD.h"
#include "entities/ActorManager.h"
#include <cstdio>

// Matches GamePreInitialise (0x10b588) — zero the Game singleton
void GamePreInitialise() {
    Game* game = Game::GetInstance();
    if (!game) return;

    // Original: CpuFill8(game, 0, 0x608)
    // For port: zero the gameplay fields (not the SDL/port fields)
    game->taskStateIndex = 0;
    game->gameActiveFlag = 0;
    game->gameMode = 0;
    game->pauseFlag = 0;
    game->m_TransitionTimer = 0;
    game->bombHitTimer = 0;
    game->dt = 0;
    game->hud = NULL;
    game->mainScreen = NULL;
    game->m_FrameTimer = 0;
}

// Matches GameInitialise (0x10bdfc, 305 lines) — boot all singletons
void GameInitialise() {
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("GameInitialise: booting engine\n");

    // 1. InputManager
    game->inputManager = new InputManager();
    game->inputTranslator.Init();

    // 2. FruitCamera (matches original: operator_new(0x16c))
    game->pCamera = new FruitCamera();

    // 3. ActorManager
    game->actorManager = new ActorManager();

    // TODO: Font::Load ×8 (multiple fonts)
    // TODO: LoadLocalisedTexture → Game+0x17c (fruit atlas)
    // TODO: MenuButton::LoadContent, Fruit::LoadInfo
    // TODO: SplatEntity/SlashEntity/Bomb/GameOverScreen/PowerUpShop::LoadContent
    // TODO: PreloadSounds

    printf("GameInitialise: done\n");
}

// Matches GameDestroy (0x10b7ec, 174 lines) — full engine teardown
void GameDestroy() {
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("GameDestroy: shutting down\n");

    // Cleanup per-session first (in case still active)
    if (game->hud) {
        game->hud->Release();
        delete game->hud;
        game->hud = NULL;
    }
    game->mainScreen = NULL;

    // Destroy singletons
    if (game->pCamera) { delete game->pCamera; game->pCamera = NULL; }
    if (game->inputManager) { delete game->inputManager; game->inputManager = NULL; }
    if (game->actorManager) { delete game->actorManager; game->actorManager = NULL; }

    // TODO: UnLoadContent for all screens/entities
    // TODO: Delete fonts, FruitSaveData, GameSound
    // TODO: Destroy: TextureManager, MeshManager, etc.
}
