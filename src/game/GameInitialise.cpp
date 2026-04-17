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
#include "audio/GameSound.h"
#include "hud/SliceEffect.h"
#include "screens/GameOverScreen.h"
#include "screens/PowerUpShop.h"
#include "screens/DojoScreen.h"
#include "screens/AboutScreen.h"
#include "screens/GameModeScreen.h"
#include "screens/ShopScreen.h"
#include "screens/LeaderboardScreen.h"
#include "hud/FruitFactControl.h"
#include "hud/TutorialControl.h"
#include "entities/Coin.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "asset/MeshManager.h"
#include "core/SystemManager.h"
#include "particle/PSPParticleManager.h"
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
    game->pGameSound = NULL;
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

    // GameSound — 32-slot pool. Backend is currently stubbed
    // (SoundManager is no-op) but the call sites exercise the real
    // pooling / IsPlaying / Release machinery so wiring a proper
    // SDL audio backend later is a drop-in change.
    game->pGameSound = new Mortar::GameSound();

    // TODO: Step 5: InitialiseData()

    // Step 11: PSPParticleManager — load particle XML templates
    {
        Mortar::PSPParticleManager& pm = Mortar::PSPParticleManager::GetInstance();
        std::string partDir = game->data_dir + "/particles";
        std::string partXml = partDir + "/particles_fast.xml";
        pm.LoadFile(partXml.c_str());
    }

    // Step 13: TutorialControl (matches binary: operator_new(0xa0), Init, AddControl)
    game->pTutorialCtrl = new TutorialControl();
    game->pTutorialCtrl->Init();
    if (game->hud) game->hud->AddControl(game->pTutorialCtrl);

    // TODO: Steps 12-13: PowerUpManager, LeaderboardManager
    // TODO: Steps 16-21: Font::Load ×8
    // TODO: Step 22: LoadLocalisedTexture → g_GameData+0x17c (fruit atlas)
    // Step 23: MenuButton::LoadContent()
    MenuButton::LoadContent();

    // Step 24: Fruit::LoadInfo (0x17987c) — parses Data/xml/fruitlist.xml
    Fruit::LoadInfo();

    // Fruit::LoadFruitModels (0x1794e0) — load per-type half meshes
    // (<name>_<c>_piece_{1,2}.mmd) so sliced fruit renders with real
    // half geometry. Must run after LoadInfo so FRUIT_INFO is populated.
    Fruit::LoadFruitModels();

    // Step 25: LoadContent calls (binary: 0x10c41a region)
    SplatEntity::LoadContent();
    SplatEntity::CreatePool(48);     // binary calls CreatePool(30-50)
    FN::SliceEffect_CreatePool(32);  // binary uses MemoryPool<Node>(32)
    SlashEntity::LoadContent();     // TODO: blade trail textures
    Bomb::LoadContent();            // loads bomb models + textures
    GameOverScreen::LoadContent();  // TODO: game-over UI textures
    PowerUpShop::LoadContent();     // TODO: power-up shop textures
    GameModeScreen::LoadContent();  // mode select screen textures (7 textures)
    // TODO: PreloadSounds

    printf("GameInitialise: done\n");
}

// Matches GameDestroy (0x10b7ec, 174 lines) — full engine teardown.
// Order follows the binary exactly; unported steps are stubbed with TODO.
void GameDestroy() {
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("GameDestroy: shutting down\n");

    // --- 1. Online services (defunct — skipped) ---
    // TODO: LeaderboardManager::Destroy
    // TODO: NetworkManager::Destroy

    // --- 2. Static texture teardown ---
    FruitFactControl::UnLoadContent();
    AboutScreen::UnLoadContent();
    GameOverScreen::UnLoadContent();
    GameModeScreen::UnLoadContent();
    MenuButton::UnLoadContent();
    Coin::UnLoadContent();
    DojoScreen::UnLoadContent();
    ShopScreen::UnLoadContent();
    PowerUpShop::UnLoadContent();
    LeaderboardScreen::UnLoadContent();

    // --- 3. Data managers (not ported) ---
    // TODO: AchievementManager::UnLoadAchievementInfo
    // TODO: ItemManager::UnLoadItemData

    // --- 4. HUD ---
    if (game->hud) {
        delete game->hud;
        game->hud = NULL;
    }
    game->mainScreen = NULL;

    // --- 5. FruitCamera ---
    if (game->pCamera) { delete game->pCamera; game->pCamera = NULL; }
    // TutorialControl is a HUDControl — destroyed by HUD teardown above.
    game->pTutorialCtrl = NULL;

    // --- 6. Fonts (field_0x50..0x80, ~10 Font* slots) ---
    // TODO: delete game->pFont* slots (0x50, 0x54, 0x58, 0x5c,
    //       loop 0x70..0x0c, 0x6c, 0x64, 0x80, 0x68)

    // --- 7. FruitSaveData ---
    // TODO: delete game->pSaveData

    // --- 8. GameSound ---
    if (game->pGameSound) { delete game->pGameSound; game->pGameSound = NULL; }

    // --- 9. SmartPtr clear (field_0x17c) ---
    // TODO: SmartPtr::SetNull(&game->field_0x17c)

    // --- 10. Engine subsystem teardown ---
    // TODO: FileManager::ClearSystems
    // TODO: PSPParticleManager::Destroy
    // TODO: StringTableUtilUnload
    // TODO: CleanupBomb, CleanupFruit, CleanUpSplat, CleanupSlash

    // --- 11. Port-specific cleanup (SDL replacements) ---
    if (game->inputManager) { delete game->inputManager; game->inputManager = NULL; }
    if (game->actorManager) { delete game->actorManager; game->actorManager = NULL; }

    // --- 12. Engine singletons ---
    // TODO: Mortar::InputManager::Destroy
    // TODO: Mortar::TextureManager::Destroy
    // TODO: Mortar::AnimationManager::Destroy
    // TODO: Mortar::MeshManager::Destroy
    // TODO: Mortar::TextureManager::Destroy (binary calls twice)
    // TODO: Mortar::DisplayManager::Destroy
    // TODO: Mortar::SoundManager::Destroy
    // TODO: SystemManager::Destroy
}
