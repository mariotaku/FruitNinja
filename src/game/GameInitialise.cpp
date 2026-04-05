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
#include "entities/FruitInfo.h"
#include <cstdio>
#include <string>

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
// See docs/functions/game-loop.md for full 25-step init order.
void GameInitialise() {
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("GameInitialise: booting engine\n");

    // Steps 1-4: SystemManager, MatrixManager, FileSystem, DisplayManager
    // Port specific: these are Meyers singletons, auto-initialized on first GetInstance()

    // Step 10: InputManager
    game->inputManager = new InputManager();
    game->inputTranslator.Init();

    // Step 15: FruitCamera (matches original: operator_new(0x16c))
    game->pCamera = new FruitCamera();
    game->pCamera->Init(1.0f, 10000.0f, 16.95f, 11.3f);  // fovOrNear, farClip, fovX, fovY

    // Zero g_GameData fields (matches step 15 continued)
    game->worldPos = Vec3(0.0f, 0.0f, 0.0f);

    // Step 24: Fruit::LoadInfo (parses Data/xml/fruitlist.xml)
    {
        std::string xmlPath = game->data_dir + "/xml/fruitlist.xml";
        FruitInfo_Load(xmlPath.c_str());
    }

    // ActorManager (needed for entity creation)
    game->actorManager = new ActorManager();

    // TODO: Step 5: InitialiseData()
    // TODO: Steps 11-13: PSPParticleManager, PowerUpManager, LeaderboardManager
    // TODO: Steps 16-21: Font::Load ×8
    // TODO: Step 22: LoadLocalisedTexture → g_GameData+0x17c (fruit atlas)
    // TODO: Step 23: MenuButton::LoadContent()
    // TODO: Step 25: SplatEntity/SlashEntity/Bomb/GameOverScreen/PowerUpShop::LoadContent
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
