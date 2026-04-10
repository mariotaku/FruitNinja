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
#include "hud/MenuButton.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "entities/SplatEntity.h"
#include "entities/SlashEntity.h"
#include "screens/GameOverScreen.h"
#include "screens/PowerUpShop.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "asset/MeshManager.h"
#include "core/SystemManager.h"
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

    // Step 1: SystemManager::Init() — 0x0018b024: m_field50=0, m_bRunning=1, clock base (skipped)
    Mortar::SystemManager::GetInstance().Init();

    // Step 2: MatrixManager::Init() — 0x0019e2ac: just calls ResetAllStacks
    Mortar::MatrixManager::GetInstance().Init();

    // Step 3: FileSystem — skipped, port uses direct filesystem

    // Step 4: DisplayManager::GetInstance() → SetWindowSize, SetClearColour, SetLightDirection
    {
        Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
        // Port specific: original is SetWindowSize(0, 320, 0, 480) for portrait Bada.
        // Port uses landscape 960×640; FruitCamera reads these for ortho extent.
        dm.SetWindowSize(0, 0, FN_SCREEN_W, FN_SCREEN_H);
        dm.SetClearColour(Colour(0, 0, 0, 255));
        // DIFFERS: first component unknown (DAT in docs). GameDraw overwrites with worldPos anyway.
        dm.SetLightDirection(Vec3(0.0f, -10.0f, -5.0f));
    }

    // Step 8: MeshManager::Initialise(0x26C00) — mesh cache
    {
        static Mortar::MeshManager meshMgr;
        meshMgr.Initialise(32);
    }

    // Step 10: InputManager
    game->inputManager = new InputManager();
    game->inputTranslator.Init();

    // Step 15: FruitCamera (matches original: operator_new(0x16c))
    game->pCamera = new FruitCamera();
    game->pCamera->Init(1.0f, 10000.0f, 16.95f, 11.3f);  // fovOrNear, farClip, fovX, fovY

    // Zero g_GameData fields (matches step 15 continued)
    game->worldPos = Vec3(0.0f, 0.0f, 0.0f);

    // ActorManager (needed for entity creation)
    game->actorManager = new ActorManager();

    // TODO: Step 5: InitialiseData()
    // TODO: Steps 11-13: PSPParticleManager, PowerUpManager, LeaderboardManager
    // TODO: Steps 16-21: Font::Load ×8
    // TODO: Step 22: LoadLocalisedTexture → g_GameData+0x17c (fruit atlas)
    // Step 23: MenuButton::LoadContent()
    MenuButton::LoadContent();

    // Step 24: Fruit::LoadInfo (0x17987c) — parses Data/xml/fruitlist.xml
    Fruit::LoadInfo();

    // Step 25: LoadContent calls (binary: 0x10c41a region)
    SplatEntity::LoadContent();     // TODO: splat textures
    SlashEntity::LoadContent();     // TODO: blade trail textures
    Bomb::LoadContent();            // loads bomb models + textures
    GameOverScreen::LoadContent();  // TODO: game-over UI textures
    PowerUpShop::LoadContent();     // TODO: power-up shop textures
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
