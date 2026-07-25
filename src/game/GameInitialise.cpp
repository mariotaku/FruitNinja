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
#include "entities/SuperFruitControl.h"
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
#include "hud/FruitFactControl.h"   // FruitFactControl::UnLoadContent
#include "hud/TutorialControl.h"
#include "entities/Coin.h"
#include "entities/SlashEntity.h"
#include "BonusManager.h"
#include "SetupGameWork.h"
#include "PreloadSounds.h"
#include "PreloadRings.h"
#include "PowerUpManager.h"
#include "AchievementManager.h"
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
#include "PreloadFontsTTF.h"
#include "hud/IngamePopup.h"
#include "util/StringTable.h"
#include "util/Localisation.h"
#include "util/StringHash.h"
#include "debug/Logger.h"
#include <cstdlib>
#include <ctime>
#include <string>
#include "game/GameWork.h"
#include "engine/render/FontCacheObjectTTF.h"  // GetAtlas()->InitialiseData for #282 lang scale

#if defined(FRUIT_PLATFORM_WII)
#include <ogc/conf.h>
#endif
#if defined(FN_BLOCK_PRELOAD)
#include "resource/BlockLoader.h"  // LogHeapUsage (task #36/#59 residency diagnostic)
#endif

// Port specific: Wii has no in-game language chooser (SettingsScreen hides
// m_LangDrop there -- see SettingsScreen.cpp), so the console's own system
// language (CONF_GetLanguage()) is the sole source of game_work.languageFlag
// on this platform. Maps CONF_LANG_* to the same languageFlag values
// StringTableUtilLoadStringsTable's kLanguageSuffix table indexes (see
// src/engine/util/StringTable.cpp:71-94). Only the languages with shipped
// .str data on Wii are mapped; anything else (CONF_LANG_DUTCH, which has no
// English-mapped Wii system-menu language code either) falls back to
// languageFlag 0 (english_us).
static uint8_t GetWiiSystemLanguageFlag() {
#if defined(FRUIT_PLATFORM_WII)
    switch (CONF_GetLanguage()) {
    case CONF_LANG_JAPANESE:      return 12; // japanese
    case CONF_LANG_ENGLISH:       return 0;  // english_us
    case CONF_LANG_GERMAN:        return 4;  // german
    case CONF_LANG_FRENCH:        return 2;  // french
    case CONF_LANG_SPANISH:       return 3;  // spanish
    case CONF_LANG_ITALIAN:       return 5;  // italian
    case CONF_LANG_SIMP_CHINESE:  return 13; // chinese
    case CONF_LANG_TRAD_CHINESE:  return 14; // traditional chinese
    case CONF_LANG_KOREAN:        return 11; // korean
    default:                      return 0;  // english_us fallback (incl. CONF_LANG_DUTCH)
    }
#else
    return 0;
#endif
}

// Matches GamePreInitialise (0x10b588) — zero the Game singleton
void GamePreInitialise() {

    // Original: CpuFill8(game, 0, 0x608)
    // For port: zero the gameplay fields (not the SDL/port fields)
    game_work.taskStateIndex = 0;
    game_work.bM_Mode = false;
    game_work.gameMode = 0;
    game_work.bM_bPaused = 0;
    game_work.m_PauseAmount = 0;
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
        dm.SetWindowSize(0, FN_SCREEN_H, 0, FN_SCREEN_W);
        dm.SetClearColour(Colour(0, 0, 0, 255));
        // DIFFERS: first component unknown (DAT in docs). GameDraw overwrites with worldPos anyway.
        dm.SetLightDirection(_Vector3<float>(0.0f, -10.0f, -5.0f));
    }

    // ASM-spec v1.6.1 GameInitialise @0x0011d22c: strings loaded before managers that
    // parse localized data (InitialiseStrings @0x0011c1c8 position). Binary loads the
    // string table right after DisplayManager, BEFORE ParticleManager/PowerUpManager/
    // InitialiseData -- so BonusManager::Init (inside InitialiseData) and every XML
    // parser that calls GETSTRING_CAST_0_STR (LoadAchievementInfo/LoadItemData/
    // Bonus::Parse) bake real strings instead of the "STRING NOT FOUND" fallback.
    // Needs FileSystem (added above) + game_work.languageFlag (set by main before
    // game.init()); both are ready here.
#if defined(FRUIT_PLATFORM_WII)
    // Port specific: Wii has no in-game language chooser (hidden in Settings);
    // the console system language (CONF_GetLanguage) is authoritative each boot,
    // overriding any saved languageFlag. Unmapped Wii languages fall back to English.
    game_work.languageFlag = GetWiiSystemLanguageFlag();
#endif
    Localisation::Load(game->data_dir.c_str(), (int)game_work.languageFlag);

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
    game_work.worldPos = _Vector3<float>(0.0f, 0.0f, 0.0f);

    // Mortar::ActorManager (needed for entity creation). Binary uses Meyers
    // singleton in GetInstance @ 0x001705f0; port also lazy-inits via GetInstance.
    // Factory is the free function CreateEntity (binary 0x0017421c) — see EntityFactory.h.
    //
    // DIFFERS (residue fix): numTypes MUST match GameInit.cpp step 7's
    // am->Initialise(7, 0x2000) (v1.6.1 GameInit @ 0x001ce1c0 -- the binary's actual
    // ActorManager::Initialise call site). ActorManager::Initialise() is idempotent
    // (`if (m_pHeap != nullptr) return;`), and this GameInitialise() call runs BEFORE
    // GameInit(), so whichever numTypes lands here wins for the whole session --
    // GameInit's later Initialise(7,...) silently no-ops. This call used to pass 5
    // (stale, pre-dating the 7-type fix in GameInit.cpp), which permanently capped
    // m_NumTypes at 5 and made entity types 5 (Jiblet) and 6 (FruitRay) unreachable
    // via Add() in every real session -- e.g. SuperFruitControl::SpawnJibs /
    // ExplodeSuperFruit's am->Add(5, true) silently returned null, so the super-fruit
    // finale never spawned jiblet fragments. Found via tests/scenes/scene_jiblet.cpp.
    game->actorManager = Mortar::ActorManager::GetInstance();
    game->actorManager->Initialise(7, 0x2000);
    game->actorManager->RegisterFactory(&CreateEntity);

    // Step 5 (binary): operator_new(0x238) + FruitSaveData ctor. Binary
    // InitialiseData @ 0x0011c3f0 follows the ctor with a LoadGame call
    // so persistent state is restored before the rest of init runs.
    game_work.m_SaveData = new FruitSaveData();
    LoadGame(game_work.m_SaveData);

    // InitialiseData step 7: restore last-used game mode from save
    game_work.gameMode = (uint8_t)game_work.m_SaveData->m_GameMode;

    // InitialiseData step 8: SetupGameWork (0x0010b4e8)
    SetupGameWork();

    // InitialiseData steps 9-11: sound/music on/off from save totals, then reset flags
    {
        const unsigned int hSoundOff = StringHash("soundOff");
        const unsigned int hMusicOff = StringHash("musicOff");
        game_work.m_bSoundOn = (game_work.m_SaveData->GetTotal(hSoundOff) == 0);
        game_work.m_bMusicOn = (game_work.m_SaveData->GetTotal(hMusicOff) == 0);
        const int soundOffCount = game_work.m_SaveData->GetTotal(hSoundOff);
        const int musicOffCount = game_work.m_SaveData->GetTotal(hMusicOff);
        if (soundOffCount != 0)
            game_work.m_SaveData->AddToTotal("soundOff", hSoundOff, -soundOffCount, false, true);
        if (musicOffCount != 0)
            game_work.m_SaveData->AddToTotal("musicOff", hMusicOff, -musicOffCount, false, true);
    }
#ifdef __EMSCRIPTEN__
    // Port specific: web audio init -- SFX shows ON from boot so the sound
    // button renders in the ON state immediately; no visible flip on first
    // touch. The AudioContext is created suspended-or-running depending on
    // browser state (SoundManager::Init -> fnaudio_init,
    // SoundManagerWebAudio.cpp); if born suspended, it's unlocked by the
    // splash-time audio-consent overlay tap (shell.html
    // #audio-consent-overlay, wired through mainEmscripten.cpp's
    // fn_set_audio_enabled / ctx.resume()) -- no detection beyond that born
    // state, no persistence at all (see mainEmscripten.cpp's g_gameInited
    // comment block). GameInit step 23 calls SoundManager::Initialise +
    // SetSFXVolume(0.5f) (because m_bSoundOn=true here), so SFX volume is
    // already correct once the AudioContext actually unlocks (via the tap,
    // or because it was already running). Music stays OFF / opt-in; the
    // user enables it via the in-game music toggle. Nothing here is
    // remembered across loads -- fn_set_audio_enabled/fn_audio_consent_skip
    // overwrite both flags for THIS SESSION ONLY on every single load.
    game_work.m_bSoundOn = true;
    game_work.m_bMusicOn = false;
#endif

    // InitialiseData step 12: per-power-up slash colour table
    SlashEntity::InitModColours();

    // BonusManager::Init moved DOWN to after LoadAchievementInfo + strings load
    // (v1.6.1 GameInitialise @0x0011d22c: BonusManager::Init runs inside InitialiseData
    // AFTER LoadAchievementInfo). Its Bonus::Parse bakes localized award names, so it
    // must run after the string table is loaded.

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

    // Step 1 of InitialiseData (localisation-table load) now happens EARLY, right after
    // DisplayManager setup (see the Localisation::Load call there). Loading it here --
    // after BonusManager::Init used to run -- baked "STRING NOT FOUND" into every bonus
    // award name (v1.6.1 GameInitialise @0x0011d22c loads strings first).

    // Step 13 of InitialiseData: AchievementManager::LoadAchievementInfo
    // Binary: InitialiseData @ 0x0010b7ca, step 13 of 15.
    // Populates m_All keyed by StringHash(id); AchievementExists gates
    // ItemManager cost>0 shop items (items with cost require an unlocked achievement).
    // Must run before LoadItemData (step 14).
    AchievementManager::GetInstance()->LoadAchievementInfo();

    // InitialiseData step 15: BonusManager (combo/streak tracker). Binary runs this
    // inside InitialiseData AFTER LoadAchievementInfo and after strings load
    // (v1.6.1 GameInitialise @0x0011d22c), so Bonus::Parse bakes localized award names.
    BonusManager::GetInstance()->Init();

    // Step 14 of InitialiseData: ItemManager::GetInstance() + LoadItemData()
    // Binary: InitialiseData @ 0x0010b7ca, step 14 of 15.
    ItemManager::GetInstance()->LoadItemData();

    // Step 11 (call #22): PowerUpManager::Load — parse poweruplist.xml
    PowerUpManager::GetInstance()->Load();

    // Step 12: PSPParticleManager — load particle XML templates.
    // texCategory feeds TextureManager::Load (IFile chain — logical path).
    // xmlPath is a bare relative path; FileManager prepends data-root centrally.
    {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        pm.LoadFile("particles", "particles/particles_fast.xml");
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
    game_work.m_PauseAmount = -1.0f;

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

    // PreloadFontsTTF (binary @0x0011c1fc, called from InitialiseData @0x0011c3f0):
    // populates game_work.m_pTTFFontMain with the localized TTF face.
    // Must run before MenuButton::LoadContent so GetSharedTTFFont() finds it.
    PreloadFontsTTF();

    // ASM-spec v1.6.1 InitialiseData @0x0011c3f0: per-language globalSizeScale.
    // Only langId 0x13 (russian) gets 0.9; all others are 1.0.
    if (game_work.m_pTTFFontMain) {
        float scale = (game_work.languageFlag == 0x13) ? 0.9f : 1.0f;
        game_work.m_pTTFFontMain->GetAtlas()->InitialiseData(1.0f, scale);
    }

    // Port specific: task #28 first-screen-open frame-spike mitigation -- warm the
    // TTF glyph cache (ASCII + known menu-label strings) and flush the atlas here,
    // at boot (masked by the load screen), instead of on first Dojo/ModeSelect/Shop
    // open. Must run after the InitialiseData scale override above (the glyph cache
    // key depends on m_GlobalSizeScale/m_FontScale). See PreloadFontsTTF.h.
    WarmTTFGlyphCache();

    // Step 22: LoadLocalisedTexture -> game_work.m_CountdownTex (+0x180).
    // Binary order @0x0011da48-0x0011da6c: LoadLocalisedTexture -> assign -> MenuButton::LoadContent.
    // TODO: v1.6.1 0x0011da48 (GameInitialise::InitialiseData) -- resolve exact .tex filename
    // (GOT/GOTOFF chase to ~0x2CBCE2 did not yield readable ASCII this pass). Left unassigned
    // (null-safe: ShopScreen's state-3 back button just draws without a texture) until resolved.
    // game_work.m_CountdownTex = Mortar::TextureManager::LoadLocalisedTexture("<TBD>.tex");

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
    // SplatEntity::CreatePool is NOT called here in the binary -- the earlier
    // CreatePool(48) here was a stale v1.5.x fabrication. The binary allocates
    // the splat pool exactly once, from GameInit (v1.6.1 @0x001ce1c0, capacity
    // 0x100=256; see GameInit.cpp step 15), which runs on first gameplay entry
    // and unconditionally replaces any pre-existing pool (CreatePool deletes
    // the old array before allocating). Allocating a smaller pool here would
    // only matter if splats could spawn before that first GameInit call, which
    // they cannot (splats are gameplay-only).
    // SliceEffect pool (capacity=100) is created in Fruit::LoadFruitModels (v1.6.1 @0x001e10c4)
    SlashEntity::LoadContent();     // TODO: blade trail textures
    Bomb::LoadContent();            // loads bomb models + textures
    MenuButton::LoadContent();      // loads new_item.tex (star indicator)
#if defined(FN_BLOCK_PRELOAD)
    // Deferred to BlockLoader::PreloadBlock(RES_BLOCK_INGAME) -- task #59
    // (gameplay-only: combo/critical/rare overlays, never shown at menu time)
#else
    MissControl::LoadContent();     // load critical / rare / cross overlays -- fidelity: host/web/binary load at boot
#endif
    // Pool allocation + HUD registration happens in GameInit (which
    // runs AFTER the HUD is created).
#if defined(FN_BLOCK_PRELOAD)
    // Deferred to BlockLoader::PreloadBlock(RES_BLOCK_INGAME) -- task #59
    // (game-over UI is never drawn from the menu or during play)
#else
    GameOverScreen::LoadContent();  // TODO: game-over UI textures -- fidelity: host/web/binary load at boot
#endif
    PowerUpShop::LoadContent();     // binary @ 0x00155b50 — empty body
    // v1.6.1 GameInitialise @0x0011daa8: after PowerUpShop::LoadContent
#if defined(FN_BLOCK_PRELOAD)
    // Deferred to BlockLoader::PreloadBlock(RES_BLOCK_INGAME) -- task #59
    // (gameplay-only: super-fruit lightning overlay, never shown at menu time)
#else
    SuperFruitControl::LoadContent();   // fidelity: host/web/binary load at boot
#endif
    GameModeScreen::LoadContent();  // mode select screen textures (7 textures)
    // Port specific: task #28 first-screen-open frame-spike mitigation -- decode+upload
    // Dojo (6 .tex) and Shop (10 .tex) textures at boot instead of on first open.
    // Idempotent: TextureManager::Load caches by StringHash(path), so the later
    // ctor-triggered LoadContent() call (DojoScreen ctor / ShopScreen ctor guard) is a
    // cache hit, not a re-decode. No binary counterpart -- v1.6.1 loads these lazily.
#if defined(FN_BLOCK_PRELOAD)
    // Deferred to BlockLoader::PreloadBlock(RES_BLOCK_SHOP) -- task #59
    // (dojo/shop screen chrome, only reachable via menu -> dojo -> shop)
#else
    DojoScreen::LoadContent();
    ShopScreen::LoadContent();
#endif
    // Binary call #48: PreloadSounds (0x00101cac) — 25 named WAVs + per-fruit + arcade variants
    PreloadSounds();   // 0x0010b204 — preload WAVs (implemented + ASM-verified, see PreloadSounds.cpp)

    // Binary @ 0x11d22c: PreloadRings — loads ring textures + colour table into game_work.
    PreloadRings();

    LOG_INFO("GAMEINIT", "GameInitialise: done");
#if defined(FN_BLOCK_PRELOAD)
    fn::wii::LogHeapUsage("boot-done");
#endif
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

    // --- 6. Fonts (field_0x50..0x80, 11 Font* slots + TTF slot at +0x614) ---
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
    //
    // +0x614 m_pTTFFontMain: binary GameDestroy @0x0011d20c clears the raw ptr.
    // Port: null the raw ptr. The owning s_TTFFontMain SmartPtr lives in
    // PreloadFontsTTF.cpp and is released at process exit / next PreloadFontsTTF call.
    game_work.m_pTTFFontMain = 0;
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
    StringTableUtilUnloadTable(0);  // closes StringTableUtilUnloadTable TODO (v1.6.1 @0x14c9f8)
    // Binary GameDestroy @0x0011d1b4 calls CleanupBomb -> CleanupFruit -> CleanUpSplat -> CleanupSlash
    // here. The port does NOT call them: entity teardown is already done above via the
    // actorManager deletion (the "Port specific" substitution at step 3.5), so calling these
    // would DOUBLE-FREE the fruit/splat/slash pools + models. The functions are ported (for
    // asm-verify symbol coverage) but left uncalled until the port's teardown is reconciled
    // with the binary's (use the Cleanup* path OR the actorManager path, not both).
    // TODO: reconcile GameDestroy teardown to the binary's CleanupBomb/Fruit/Splat/Slash path.

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

// ASM-spec v1.6.1 InitialiseStrings @0x11c1c8
// Binary: calls StringTableUtilInit() then StringTableUtilLoadStrings() (@0x11fb20).
// DIFFERS: StringTableUtilLoadStrings @0x11fb20 maps to Localisation::Load() which is
//   already called in GameInitialise at step 1 of InitialiseData. Calling again here
//   would double-load; init is deferred to the GameInitialise call path.
void InitialiseStrings() {
    StringTableUtilInit();
}

// ASM-spec v1.6.1 UnloadRings @0x11cdc8
// Binary: inverse of PreloadRings — nulls the 17 ring textures in binary order.
// The null order matches the binary's free sequence (NOT load order):
//   {0,1,3,4,2,5,6,7,8,9,10,11,12,13,14,16,15}
void UnloadRings() {
    game_work.m_RingTex[ 0].SetNull();
    game_work.m_RingTex[ 1].SetNull();
    game_work.m_RingTex[ 3].SetNull();
    game_work.m_RingTex[ 4].SetNull();
    game_work.m_RingTex[ 2].SetNull();
    game_work.m_RingTex[ 5].SetNull();
    game_work.m_RingTex[ 6].SetNull();
    game_work.m_RingTex[ 7].SetNull();
    game_work.m_RingTex[ 8].SetNull();
    game_work.m_RingTex[ 9].SetNull();
    game_work.m_RingTex[10].SetNull();
    game_work.m_RingTex[11].SetNull();
    game_work.m_RingTex[12].SetNull();
    game_work.m_RingTex[13].SetNull();
    game_work.m_RingTex[14].SetNull();
    game_work.m_RingTex[16].SetNull();
    game_work.m_RingTex[15].SetNull();
}

// ASM-spec v1.6.1 GetLanguage @0x1eebec
// Binary: reads Osp::Locales::LocaleManager to map system language to game lang id.
// Port specific: Osp::Locales unavailable on SDL2 — default to English (id=0).
// Language code -> game lang id table (from binary @0x1eebec):
//   0xc9->5(italian), 0x60/0xcf->14(english_uk), 0x14->20, 0x4e->13(chinese),
//   0x88->2(dutch), 0x95->4(spanish), 0x7a(country!=0xe1)->1(german),
//   0xe6->11(japanese), 0xcc->12(english_uk alt), 0x159->16, 0x16a->19,
//   0x190(country!=0xc6)->15 else 3(french), 0x15b(country==0x1e)->18 else 17,
//   default->0(english_us).
const char* GetLanguage(int& outLang) {
    // Port specific: Osp::Locales unavailable; default to English.
    outLang = 0;
    return "en_US";
}
