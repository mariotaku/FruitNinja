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
#include "entities/BombBlast.h"
#include "entities/EntityFactory.h"
#include "entities/SplatEntity.h"
#include "entities/SlashEntity.h"
#include "FruitSaveData.h"
#include "audio/GameSound.h"
#include "hud/MissControl.h"
#include "screens/GameOverScreen.h"
#include "screens/PowerUpShop.h"
#include "screens/DojoScreen.h"
#include "screens/AboutScreen.h"
#include "screens/GameModeScreen.h"
#include "screens/ShopScreen.h"
#include "ItemManager.h"
#include "screens/LeaderboardScreen.h"
#include "hud/FruitFactControl.h"
#include "hud/TutorialControl.h"
#include "entities/Coin.h"
#include "entities/SlashEntity.h"
#include "BonusManager.h"
#include "SetupGameWork.h"
#include "PreloadSounds.h"
#include "PreloadRings.h"
#include "PowerUpManager.h"
// Analysed: 2026-04-25T12:00
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "asset/MeshManager.h"
#include "asset/AnimationManager.h"
#include "core/SystemManager.h"
#include "particle/PSPParticleManager.h"
#include "asset/File.h"
#include "asset/FileManager.h"
#include "asset/FileSystem_Direct.h"
#include "render/Font.h"
#include "hud/IngamePopup.h"
#include "util/StringTable.h"
#include "util/Localisation.h"
#include "util/StringHash.h"
#include "debug/Logger.h"
#include <cstdlib>
#include <ctime>
#include <string>
#include "game/GameWork.h"

// Matches GamePreInitialise (0x10b588) — zero the Game singleton
void GamePreInitialise() {

    // Original: CpuFill8(game, 0, 0x608)
    // For port: zero the gameplay fields (not the SDL/port fields)
    game_work.taskStateIndex = 0;
    game_work.bM_Mode = false;
    game_work.gameMode = 0;
    game_work.bM_bPaused = 0;
    game_work.m_GameDt = 0;
    game_work.m_BombHitTimer = 0;
    game_work.dt = 0;
    game_work.mHud = nullptr;
    game_work.mMainScreen = nullptr;
    game_work.pGameOverScreen = nullptr;
    game_work.mCountDown = nullptr;
    game_work.m_FrameTimer = 0;
    game_work.mGameSound = nullptr;
}

// Matches GameInitialise (0x10bdfc, 305 lines) — boot all singletons
// See docs/functions/game-loop.md for full 25-step init order.
// DIFFERS: original passes the Bada window/config (v1.6.1 GameInitialise @0x0011d22c); SDL port owns its window, args unused.
void GameInitialise(void* window, const char* config) {
    (void)window;
    (void)config;
    Game* game = Game::GetInstance();

    LOG_INFO("GAMEINIT", "GameInitialise: booting engine");

    // _GLOBAL__I_EngineMathBada.cpp @ 0x001952bc: Math::Random ctor calls
    // time(NULL)-equivalent seed before OspMain. Port seeds here (first call
    // in GameInitialise) to ensure srand runs before any rand() consumer
    // (WaveManager, Fruit::Init, MenuButton random angles etc.).
    srand((unsigned int)time(nullptr));

    // Step 1: Mortar::SystemManager::Init() — 0x0018b024: m_reserved50=0, m_bRunning=1, clock base (skipped)
    SystemManager::GetInstance().Init();

    // Step 2: MatrixManager::Init() — 0x0019e2ac: just calls ResetAllStacks
    MatrixManager::GetInstance().Init();

    // Step 3: FileSystem — Game::CreateFileSystems @ 0x0010dca8
    // new FileSystem_Direct(); fs->Initialise(dataRoot, false); FileManager::AddSystem(fs, 0, 0)
    {
        Mortar::FileSystem_Direct* fs = new Mortar::FileSystem_Direct();
        fs->Initialise(game->data_dir.c_str(), /*writable=*/false);
        FileManager::GetInstance().AddSystem(fs, /*id=*/0, /*priority=*/0);
    }

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
        // DIFFERS: port cache is std::map; size advisory only (binary literal = 0x26C00 = 158720 bytes)
        meshMgr.Initialise(0x26C00);
    }

    // Binary @0x0011d41c (GameInitialise): AnimationManager::Initialise(0x7d000=512000), no-op body (v1.6.1 @0x00236314).
    Mortar::AnimationManager::GetInstance().Initialise(0x7d000);

    // Step 10: InputManager
    game->inputManager = new Mortar::InputManager();
    // Binary @ 0x00196cc8 InputManager::Init -- alloc InputDeviceBada and
    // push onto m_inputDevices. Without this, RegisterInputCallback has no
    // device to broadcast to and event-driven dispatch silently no-ops
    // (the SlashEntity event handlers fail to fire).
    game->inputManager->Init(0);
    // Note: inputTranslator (SDL-bound) is allocated + Init'd in GameSDL.cpp::init().

    // Step 15: FruitCamera (matches original: operator_new(0x16c))
    game_work.m_FruitCamera = new FruitCamera();
    game_work.m_FruitCamera->Init(1.0f, 10000.0f, 16.95f, 11.3f);  // fovOrNear, farClip, fovX, fovY

    // Zero g_GameData fields (matches step 15 continued)
    game_work.worldPos = Vec3(0.0f, 0.0f, 0.0f);

    // Mortar::ActorManager (needed for entity creation). Binary uses Meyers
    // singleton in GetInstance @ 0x001705f0; port also lazy-inits via GetInstance.
    // Factory is the free function CreateEntity (binary 0x0017421c) — see EntityFactory.h.
    game->actorManager = Mortar::ActorManager::GetInstance();
    game->actorManager->Initialise(5, 0x2000);
    game->actorManager->RegisterFactory(&CreateEntity);

    // Step 5 (binary): operator_new(0x238) + FruitSaveData ctor. Binary
    // InitialiseData @ 0x0010b66c follows the ctor with a LoadGame call
    // so persistent state is restored before the rest of init runs.
    game_work.m_SaveData = new FruitSaveData();
    LoadGame(game_work.m_SaveData);

    // InitialiseData step 7: restore last-used game mode from save
    game_work.gameMode = (uint8_t)game_work.m_SaveData->m_GameMode;

    // InitialiseData step 8: SetupGameWork (0x0010b4e8)
    SetupGameWork();

    // InitialiseData steps 9-11: sound/music on/off from save totals, then reset flags
    {
        const unsigned int hSoundOff = StringHash("sound_off");
        const unsigned int hMusicOff = StringHash("music_off");
        game_work.m_bSoundOn = (game_work.m_SaveData->GetTotal(hSoundOff) == 0);
        game_work.m_bMusicOn = (game_work.m_SaveData->GetTotal(hMusicOff) == 0);
        const int soundOffCount = game_work.m_SaveData->GetTotal(hSoundOff);
        const int musicOffCount = game_work.m_SaveData->GetTotal(hMusicOff);
        if (soundOffCount != 0)
            game_work.m_SaveData->AddToTotal("sound_off", hSoundOff, -soundOffCount, false, true);
        if (musicOffCount != 0)
            game_work.m_SaveData->AddToTotal("music_off", hMusicOff, -musicOffCount, false, true);
    }
#ifdef __EMSCRIPTEN__
    // Port specific: web audio init (#73) -- SFX shows ON from boot so the sound
    // button renders in the ON state immediately; no visible flip on first touch.
    // The AudioContext is suspended by the browser until first user gesture, but
    // Emscripten's SDL2 backend resumes it automatically on that gesture -- no
    // manual ctx.resume() or gesture hook needed.  GameInit step 23 calls
    // SoundManager::Initialise + SetSFXVolume(0.5f) (because m_bSoundOn=true
    // here), so SFX volume is already correct when the AudioContext resumes.
    // Music stays OFF / opt-in; the user enables it via the in-game music toggle.
    game_work.m_bSoundOn = true;
    game_work.m_bMusicOn = false;
#endif

    // InitialiseData step 12: per-power-up slash colour table
    SlashEntity::InitModColours();

    // InitialiseData step 15: BonusManager (combo/streak tracker)
    BonusManager::GetInstance()->Init();

    // SDL2 audio backend init. Opens audio device at 16kHz mono S16LE
    // (matches MAMAudioThread sampleRate=16000). Must be called before GameSound.
    Mortar::SoundManager::GetInstance().Init();

    // GameSound — 32-slot pool backed by SDL2 audio.
    game_work.mGameSound = new GameSound();

    // Music: no boot-time SongPlay. The binary has no separate boot call;
    // UpdateMusic (0x0016a68c) is the sole issuer of SongPlay. On the first
    // frame after loading completes, UpdateMusic's slow-ramp path fires
    // SongPlay("background").

    // TODO: Step 5: InitialiseData() — full InitialiseData call chain.
    // Step 14 of InitialiseData: ItemManager::GetInstance() + LoadItemData()
    // Binary: InitialiseData @ 0x0010b7ca, step 14 of 15.
    // Called after AchievementManager::LoadAchievementInfo (step 13).

    // Step 1 of InitialiseData (must run before any XML parser that calls
    // GETSTRING_CAST_0_STR -- i.e. before LoadItemData, LoadAchievementInfo,
    // etc.): load the localisation tables. Binary:
    //   StringTableUtilLoadStrings @ 0x0011fb20 -> LoadStringsTable(language)
    Localisation::Load(game->data_dir.c_str(), (int)game_work.languageFlag);

    ItemManager::GetInstance()->LoadItemData();

    // Step 11 (call #22): PowerUpManager::Load — parse poweruplist.xml
    PowerUpManager::GetInstance()->Load();

    // Step 12: PSPParticleManager — load particle XML templates.
    // texCategory feeds TextureManager::Load (IFile chain — must be logical;
    // FileSystem_Direct prepends data_dir). xmlPath feeds tinyxml2 raw fopen
    // and needs the absolute path.
    {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        std::string partXml = game->data_dir + "/particles/particles_fast.xml";
        pm.LoadFile("particles", partXml.c_str());
    }

    // Step 13: TutorialControl (matches binary: operator_new(0xa0), Init, AddControl)
    game_work.m_TutorialControl = new TutorialControl();
    game_work.m_TutorialControl->Init();
    if (game_work.mHud) game_work.mHud->AddControl(game_work.m_TutorialControl);

    // Binary GameInit @ 0x0016cb2a: writes -1.0f to g_GameData+0x0c
    // (m_TransitionTimer) immediately after the TutorialControl block.
    // This is the seed value that puts UpdateMusic into the transition
    // branch on its first eligible frame, so SongPlay("Music-menu") fires
    // first instead of SongPlay("background").
    game_work.m_GameDt = -1.0f;

    // Note: PowerUpManager::Load is called above (step 11). LeaderboardManager is defunct.

    // Steps 16-21: Font::Load ×8
    // Matches GameInitialise @ 0x0010bdfc font-loading region.
    // Reserved slots (+0x50, +0x5C, +0x64) are left null (never loaded by binary).
    // +0x60 is a gap, not a Font slot.
    // Slots +0x58, +0x68, +0x6C, +0x80 are null-guarded (only loaded if not already set).
    // Slots +0x70, +0x74, +0x78 are File::Exists-guarded AND pre-aliased to pFontArcade.
    // +0x7C is a non-owning alias of +0x6C (pFontArcade).
    //
    // DIFFERS: binary calls DisplayManager::ShouldUseHDFonts() and branches on the result
    // to pick HD vs SD paths for +0x54 and +0x58. Port always uses SD paths since HD
    // asset check is not replicated. See docs/engine/font.md "HD vs SD Selection".
    //
    // Font load order matches binary: +0x54, +0x58, +0x6C, +0x70/+0x74/+0x78 (guarded),
    //   +0x7C alias, +0x80, +0x68. Binary addresses: 0x0010bf3a, 0x0010bf6e, 0x0010bfa4,
    //   0x0010bfcc, 0x0010bff0, 0x0010c014, alias write, 0x0010c038, 0x0010c082.
    {
        // Font::Load now routes through Mortar::File / IFile chain;
        // FileSystem_Direct prepends data_dir, so pass logical paths.
        // File::Exists likewise takes a logical path.
        const std::string fontDir = "fonts/";

        // +0x54 pFontMain: fonts/font_fruit_ninja.fnt (0x0010bf3a)
        // DIFFERS: binary loads HD path if ShouldUseHDFonts(); port uses SD only.
        game_work.pFontMain = Mortar::Font::Create((fontDir + "font_fruit_ninja.fnt").c_str());

        // +0x58 pFontNumbers: fonts/fruit_ninja_numbers.fnt (0x0010bf6e, null-guarded)
        // DIFFERS: binary loads HD path if ShouldUseHDFonts(); port uses SD only.
        if (!game_work.pFontNumbers.IsValid()) {
            game_work.pFontNumbers = Mortar::Font::Create((fontDir + "fruit_ninja_numbers.fnt").c_str());
        }

        // +0x6C pFontArcade: fonts/arcade_results_numbers.fnt (0x0010bfa4, null-guarded)
        if (!game_work.pFontArcade.IsValid()) {
            game_work.pFontArcade = Mortar::Font::Create((fontDir + "arcade_results_numbers.fnt").c_str());
        }

        // Binary immediately aliases +0x6C into +0x70, +0x74, +0x78, +0x7C as fallback
        // before the File::Exists-guarded overwrites. (0x0010bfc8 region)
        game_work.pFontGold         = game_work.pFontArcade;
        game_work.pFontSilver       = game_work.pFontArcade;
        game_work.pFontBronze       = game_work.pFontArcade;
        game_work.pFontArcadeAlias  = game_work.pFontArcade;

        // +0x70 pFontGold: fonts/gold_numbers.fnt (0x0010bfcc, File::Exists guarded)
        // Not present in shipped FruitNinjaBada/Data/fonts/ — slot stays alias.
        {
            std::string path = fontDir + "gold_numbers.fnt";
            if (Mortar::File::Exists(path.c_str(), 0)) {
                game_work.pFontGold = Mortar::Font::Create(path.c_str());
            }
        }

        // +0x74 pFontSilver: fonts/silver_numbers.fnt (0x0010bff0, File::Exists guarded)
        {
            std::string path = fontDir + "silver_numbers.fnt";
            if (Mortar::File::Exists(path.c_str(), 0)) {
                game_work.pFontSilver = Mortar::Font::Create(path.c_str());
            }
        }

        // +0x78 pFontBronze: fonts/bronze_numbers.fnt (0x0010c014, File::Exists guarded)
        {
            std::string path = fontDir + "bronze_numbers.fnt";
            if (Mortar::File::Exists(path.c_str(), 0)) {
                game_work.pFontBronze = Mortar::Font::Create(path.c_str());
            }
        }

        // +0x80 pFontBlue2: fonts/fruit_ninja_numbers_blue2.fnt (0x0010c038, null-guarded)
        if (!game_work.pFontBlue2.IsValid()) {
            game_work.pFontBlue2 = Mortar::Font::Create((fontDir + "fruit_ninja_numbers_blue2.fnt").c_str());
        }

        // +0x68 pFontGreen: fonts/fruit_ninja_numbers_green.fnt (0x0010c082, null-guarded)
        if (!game_work.pFontGreen.IsValid()) {
            game_work.pFontGreen = Mortar::Font::Create((fontDir + "fruit_ninja_numbers_green.fnt").c_str());
        }
    }

    // TODO: Step 22: LoadLocalisedTexture → g_GameData+0x17c (fruit atlas SmartPtr slot).
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
    // SliceEffect pool (capacity=100) is created in Fruit::LoadFruitModels (v1.6.1 @0x001e10c4)
    SlashEntity::LoadContent();     // TODO: blade trail textures
    Bomb::LoadContent();            // loads bomb models + textures
    MenuButton::LoadContent();      // loads new_item.tex (star indicator)
    MissControl::LoadContent();     // load critical / rare / cross overlays
    // Pool allocation + HUD registration happens in GameInit (which
    // runs AFTER the HUD is created).
    GameOverScreen::LoadContent();  // TODO: game-over UI textures
    PowerUpShop::LoadContent();     // binary @ 0x00155b50 — empty body
    GameModeScreen::LoadContent();  // mode select screen textures (7 textures)
    // Binary call #48: PreloadSounds (0x00101cac) — 25 named WAVs + per-fruit + arcade variants
    PreloadSounds();   // 0x0010b204 — preload WAVs (implemented + ASM-verified, see PreloadSounds.cpp)

    // Binary @ 0x11d22c: PreloadRings — loads ring textures + colour table into game_work.
    PreloadRings();

    LOG_INFO("GAMEINIT", "GameInitialise: done");
}

// Matches GameDestroy (0x10b7ec, 174 lines) — full engine teardown.
// Order follows the binary exactly; unported steps are stubbed with TODO.
void GameDestroy() {
    Game* game = Game::GetInstance();

    LOG_INFO("GAMEINIT", "GameDestroy: shutting down");

    // --- 1. Online services (defunct — skipped per online-services-audit) ---
    // Note: LeaderboardManager::Destroy -- skipped (online-services-audit)
    // Note: NetworkManager::Destroy -- skipped (online-services-audit)

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

    // --- 2.5. Ingame popups (v1.6.1 @0x0016db38 DeleteAllPopups) ---
    DeleteAllPopups();

    // --- 3. Data managers ---
    // Note: AchievementManager::UnLoadAchievementInfo -- no-op stub (achievement UI not ported).
    ItemManager::GetInstance()->UnLoadItemData();  // Binary @ 0x0010b7ec — after UnLoadAchievementInfo

    // --- 3.5. Entity pool teardown (Port specific: before HUD so Fruit::Release() /
    //   Bomb::Release() can still reach live MenuButton objects to clear m_pOwner /
    //   m_pOwnerButton back-refs. In the binary, CleanupFruit/Bomb/Splat/Slash (step 10
    //   TODO) performed this cleanup explicitly; the port substitutes actorManager
    //   deletion, which calls vtable Release() on every live entity. Must precede HUD
    //   teardown or Fruit::Release() at line 2433 dereferences a freed MenuButton.) ---
    { delete game->actorManager; game->actorManager = nullptr; }

    // --- 4. HUD ---
    {
        delete game_work.mHud;
        game_work.mHud = nullptr;
    }
    game_work.mMainScreen = nullptr;
    game_work.pGameOverScreen = nullptr;  // owned by HUD; nulled here after HUD Release
    game_work.mCountDown = nullptr;        // owned by HUD; nulled here after HUD Release

    // --- 5. FruitCamera ---
    { delete game_work.m_FruitCamera; game_work.m_FruitCamera = nullptr; }
    // TutorialControl is a HUDControl — destroyed by HUD teardown above.
    game_work.m_TutorialControl = nullptr;

    // --- 6. Fonts (field_0x50..0x80, 11 Font* slots) ---
    // Matches GameDestroy @ 0x0010b7ec font teardown sequence.
    // Reserved slots (pFontReserved0, pFontReserved1, pFontReserved2) are already null.
    // Slots +0x70..+0x7C: binary iterates and skips deletion if ptr == pFontArcade (alias).
    // SmartPtr::SetNull handles ref-counting; aliased slots just lose one ref.
    //
    // Binary loop body (iterates +0x70, +0x74, +0x78, +0x7C):
    //   if slot_ptr == pFontArcade: clear (non-owning alias, don't delete)
    //   else if slot_ptr != null: Font::~Font + operator_delete + null
    // SmartPtr assignment to null handles this correctly for non-aliases.
    // For aliases: SmartPtr operator= already handles ref-count safely.
    game_work.pFontGold.SetNull();
    game_work.pFontSilver.SetNull();
    game_work.pFontBronze.SetNull();
    game_work.pFontArcadeAlias.SetNull();
    // Now safe to destroy the owned arcade font
    game_work.pFontArcade.SetNull();
    game_work.pFontReserved2.SetNull();   // always null, matches binary null-check delete
    game_work.pFontBlue2.SetNull();
    game_work.pFontGreen.SetNull();
    game_work.pFontNumbers.SetNull();
    game_work.pFontMain.SetNull();
    game_work.pFontReserved1.SetNull();   // always null
    game_work.pFontReserved0.SetNull();   // always null

    // --- 7. FruitSaveData ---
    { delete game_work.m_SaveData; game_work.m_SaveData = nullptr; }

    // --- 8. GameSound ---
    { delete game_work.mGameSound; game_work.mGameSound = nullptr; }

    // --- 9. SmartPtr clear (field_0x17c) ---
    // TODO: SmartPtr::SetNull(&game->field_0x17c)

    // --- 10. Engine subsystem teardown ---
    // TODO: FileManager::ClearSystems
    PSPParticleManager::GetInstance().Destroy();
    // TODO: StringTableUtilUnload
    // TODO: CleanupBomb, CleanupFruit, CleanUpSplat, CleanupSlash

    // --- 11. Port-specific cleanup (SDL replacements) ---
    { delete game->inputManager; game->inputManager = nullptr; }
    // actorManager deleted at step 3.5 (before HUD) to prevent use-after-free
    // in Fruit::Release() / Bomb::Release() against freed MenuButton objects.

    // --- 12. Engine singletons ---
    // Note: Mortar::InputManager::Destroy -- SDL2 replacement cleaned up above.
    // Note: Mortar::TextureManager::Destroy -- port uses SDL/GL teardown at process exit.
    // Binary @0x0011d1b4 (GameDestroy): AnimationManager::Destroy() (-> ReleaseAll, no-op body).
    Mortar::AnimationManager::GetInstance().Destroy();
    // Note: Mortar::MeshManager::Destroy -- not yet ported; no-op acceptable at shutdown.
    // Note: Mortar::TextureManager::Destroy (binary calls twice) -- same as above.
    // Note: Mortar::DisplayManager::Destroy -- SDL2 window/GL teardown handles this.
    // Note: Mortar::SoundManager::Destroy -- SoundManager teardown on process exit.
    // Note: Mortar::SystemManager::Destroy -- no Mortar::SystemManager class in port; Bada OS only.
}
