//
// State 2 handlers: GameInit, GameUpdate, GameDraw, GameExit
// Binary v1.6.1: GameInit @0x001ce1c0 (18 steps), GameUpdate @0x001cf534,
//                GameDraw @0x001cd720 (211 lines), GameExit @0x001cfed4 (98 lines)
//
// GameInit rewritten 2026-06-17 to match v1.6.1 binary step order.
//

#include "GameTaskState.h"
#include "Game.h"
#include "FruitCamera.h"
#include "BombHit.h"
#include "WaveManager.h"
#include "FruitSaveData.h"
#include "screens/MainScreen.h"
#include "screens/PauseScreen.h"
#include "screens/SettingsScreen.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/TutorialControl.h"
#include "hud/TimeControl.h"
#include "hud/ScoreControl.h"
#include "hud/CoinCounter.h"
#include "hud/MissControl.h"
#include "hud/SliceEffect.h"
#include "entities/ActorManager.h"
#include "entities/Entity.h"
#include "entities/EntityFactory.h"
#include "game/EntityTypes.h"
#include "entities/Fruit.h"
#include "entities/SlashEntity.h"
#include "entities/BombFlash.h"
#include "entities/SplatEntity.h"
#include "entities/BombBlast.h"
#include "entities/FruitRay.h"
#include "engine/MenuBackground.h"
#include "asset/MeshManager.h"
#include "particle/PSPParticleManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "render/Layout.h"
#include "input/InputManager.h"
#include "input/Touch.h"
#include "input/InputSink.h"
#include "util/StringHash.h"
#include "debug/DebugFlags.h"
#include "UpdateMusic.h"
#include "audio/GameSound.h"
#include "audio/MortarSound.h"
#include "entities/Bomb.h"
#include "audio/SoundManager.h"
#include "GameOver.h"
#include "GameTaskInput.h"
#include "StartupEffects.h"
#include "entities/Coin.h"
#include "debug/Logger.h"
#include "game/GameWork.h"
#include "game/PowerUpManager.h"
#include "game/ItemManager.h"
#include "engine/network/P2PMessageHandling.h"
#include "engine/network/NetworkManager.h"

#if defined(FN_BLOCK_PRELOAD)
#include "resource/ResBlock.h"
#endif

// ASM-spec v1.6.1 GameInit @ 0x001ce1c0 — 18-step sequence.
// Each step annotated with binary behaviour. Order matches binary 1:1.
void GameInit(unsigned long) {
    Game* game = Game::GetInstance();
    GameTaskState* ts = GetTaskState();
    if (ts->initComplete) return;

    // ================================================================
    // Binary v1.6.1 GameInit @ 0x001ce1c0 — steps match 1:1
    // ================================================================

    // Step 1: HUD allocation + Release (post-ctor housekeeping)
    // Binary: if (game_work.mHud == null) { new HUD(); }
    if (!game_work.mHud) {
        game_work.mHud = new HUD();
    }
    // DIFFERS: binary calls HUD::Release(hud) here. Port's HUD::Release
    // iterates the control list and deletes entries -- nuking any HUDControl
    // already added by GameInitialise. Skipped until binary semantics RE'd.
    // TODO: RE binary v1.6.1 HUD::Release semantics, then re-add.

    // Step 2: 3x MissControl widgets + CreatePool(12)
    // Binary read table @ 0x001F3DAC (3 rows x 4 floats: x, y, rot, scale).
    {
        static const struct { float x_tbl, y_tbl, rot_tbl, scale; } kMC[3] = {
            {  79.0f,  10.0f,  -5.0f, 0.75f },
            {  52.0f,  13.0f,  +5.0f, 1.00f },
            {  20.0f,  18.0f, +10.0f, 1.20f },
        };
        for (int i = 0; i < 3; ++i) {
            MissControl* mc = new MissControl();
            mc->m_Active    = 1;
            mc->pos         = _Vector3<float>(-kMC[i].x_tbl, -kMC[i].y_tbl, 50.0f);
            // DIFFERS: opt-in widescreen (Layout::MapX) -- MissControl::Draw
            // anchors via pos + Vec3(480,320,0)*m_HudScale (same idiom as
            // MainScreen's quit button, see CreateButtons's m_pQuitButton
            // MapX comment). The fixed 480*0.5=240 term is the corner anchor;
            // MapX the pre-scale 240 and re-derive m_HudScale.x so the group
            // (all 3 X's share this HudScale) hugs the widened right edge
            // while per-icon spacing (kMC[].x_tbl baked into pos.x) is
            // untouched. Identity at __bada__/3:2 (MapX(240,key)==240 -> 0.5).
            mc->m_HudScale  = _Vector3<float>(MapX(240.0f, "hud.misscontrol") / 480.0f, 0.5f, 0.0f);
            mc->m_Timer     = -kMC[i].rot_tbl;
            mc->m_AnimState = i;
            mc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
            const float sz = 32.0f * kMC[i].scale;
            mc->size = _Vector3<float>(sz, sz, sz);
            mc->m_Texture = MissControl::GetCrossTexture();
            game_work.mHud->AddControl(mc);
        }
    }
    MissControl::CreatePool(12, game_work.mHud);

    // Step 3: ScoreControl (sizeof 0x108 = 264 bytes)
    {
        ScoreControl* sc = new ScoreControl();
        // ASM-spec v1.6.1 GameInit @0x001ce378 (ScoreControl block): 3x LoadLocalisedTexture:
        //   +0x74 m_Texture            <- "hud_fruit.tex"       (@0x0028332E, call @0x1ce3a0)
        //   +0xA0 m_ScoreIconTex       <- "score.tex"           (@0x00283E7D, call @0x1ce3dc)
        //   +0xA4 m_HighscoreBannerTex <- "new_best_score.tex"  (@0x00283E74, call @0x1ce418)
        // (earlier port fabricated score_fruit/score_icon/high_score_banner -- none in binary strtab)
        sc->m_Texture            = Mortar::TextureManager::LoadLocalisedTexture("hud_fruit.tex");
        sc->m_ScoreIconTex       = Mortar::TextureManager::LoadLocalisedTexture("score.tex");
        sc->m_HighscoreBannerTex = Mortar::TextureManager::LoadLocalisedTexture("new_best_score.tex");
        // size = Vec3::One * 64.0
        sc->size               = _Vector3<float>(64.0f, 64.0f, 64.0f);
        // pos: binary uses GetScreenWidth/GetScreenHeight
        // Formula: size.x*0.35 + screenW*-7/15, size.y*(-0.35) + screenH*7/15
        {
            Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();
            const float screenW = static_cast<float>(dm.GetWindowSize().Width());
            const float screenH = static_cast<float>(dm.GetWindowSize().Height());
            sc->pos.x = 64.0f * 0.35f + screenW * (-7.0f / 15.0f);
            sc->pos.y = 64.0f * (-0.35f) + screenH * (7.0f / 15.0f);
            sc->pos.z = 0.0f;
        }
        game_work.mHud->AddControl(sc, false);
    }

    // Step 4: CoinCounter (sizeof 0xD4 = 212 bytes)
    {
        CoinCounter* cc = new CoinCounter();
        game_work.mCoinCounter = cc;
        cc->Init();           // vtable slot 2 (binary empty no-op)
        game_work.mHud->AddControl(cc, false);
    }

    // Step 5: TimeControl (sizeof 0x108 = 264 bytes)
    {
        TimeControl* tc = new TimeControl();
        game_work.mCountDown = tc;
        tc->Init();           // vtable slot 2 (forwards to Reset)
        tc->CountDown(90.9f); // DAT_0016c9cc: Zen mode initial countdown
        game_work.mHud->AddControl(tc, false);
    }

    // Step 6: Background load + flag init
    {
        if (GetCurrentBackground() == nullptr) {
            game->IsFastHardware();  // call but ignore result (binary side-effect call)
            // DIFFERS: binary loads "gb_game.tex" into file-static backgroundTexture
            // directly; port uses ChangeBackground(0) which appends suffix.
            // Functionally equivalent with the port's always-fast IsFastHardware.
            ChangeBackground(nullptr);
        }
        // Binary also assigns: hud_font = pM_Fonts[1].
        g_unpause_game = 0;
        g_clearInput = 0;
        game_work.bM_Mode = false;
        // TODO: v1.6.1 0x001ce1c0 (GameInit) -- the binary also zeroes two globals here that
        //   the port has no counterpart for: `debugMenu = 0` and `challengeOver = 0`. Add
        //   them when the owning subsystems are ported.
        // DIFFERS: original never writes game_work.gameMode in GameInit
        //   (v1.6.1 GameInit @0x001ce1c0); the port zeroes it so the menu pump has a valid
        //   m_WaveInfo[] index before a mode is picked.
        game_work.gameMode = 0;
    }

    // Step 7: Entity system — HeapCreate + ActorManager 7-type init
    {
        ts->initComplete = true;  // one-shot guard (binary: initialised = 1)
        Mortar::Entity::HeapCreate(0x20000);
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        // Binary uses 7 types (port previously had 5). Matches v1.6.1 @ 0x001ce1c0.
        am->Initialise(7, 0x2000);
        am->RegisterFactory(Mortar::Delegate1<Mortar::Entity*, long>::MakeFree(&CreateEntity));
        am->RegisterHashConverter(Mortar::Delegate2<long, unsigned long, bool&>::MakeFree(&HashTypeConvert));
    }

    // Step 8: TutorialControl (sizeof 0xA0 = 160 bytes)
    {
        TutorialControl* tutCtrl = new TutorialControl();
        tutCtrl->Init();       // vtable slot 2
        game_work.m_TutorialControl = tutCtrl;
    }

    // Step 9: MainScreen (binary sizeof 0x12C = 300 bytes; port 0x114)
    {
#if defined(FN_BLOCK_PRELOAD)
        // Task #36 Stage 1 -- block-enter hook (log-only labelling, see
        // tmp/wii/loader-blueprint.md section 2/7). Seeds MENU before the
        // first MainScreen-owned load so boot-time / pre-mode-select loads
        // are labelled MENU instead of NONE.
        fn::wii::SetCurrentBlock(fn::wii::RES_BLOCK_MENU);
#endif
        MainScreen* mainScreen = new MainScreen();
        // DIFFERS: binary stores s_mainScreen file-static; port uses
        // game_work.mMainScreen exclusively.
        mainScreen->Init();    // vtable slot 2
        game_work.mMainScreen = mainScreen;
        mainScreen->m_bNoDestructor = 1;  // HUD::Release skips delete
    }

    // Step 10: PauseScreen (binary sizeof 0xDC = 220 bytes)
    {
        PauseScreen* pauseScreen = new PauseScreen();
        ts->pPauseScreen = pauseScreen;
        pauseScreen->Init();   // vtable slot 2 (forwards to Reset)
        // Binary GameInit @0x1ce1c0 tail: bM_Mode[+0x02]=0, bM_bPaused[+0x05]=1, flM_PauseAmount[+0x0C]=-1.0.
        // ASM-spec v1.6.1 GameInit @0x001ce1c0
        // NOTE: bM_Mode is written EARLY in the binary, alongside unpause_game/clearInput
        // (see step 6), not in this PauseScreen block. The port writes it in both places;
        // the second write is a redundant no-op.
        game_work.bM_Mode   = false;      // +0x02: gameplay-mode gate = inactive (menu)
        game_work.bM_bPaused = 1;         // +0x05: pause/inactive gate = suppressed
        game_work.m_PauseAmount  = -1.0f;     // +0x0C: flM_PauseAmount
    }

    // Step 11: AddControls to HUD (order: MainScreen, PauseScreen, TutorialControl)
    game_work.mHud->AddControl(game_work.mMainScreen, false);
    game_work.mHud->AddControl(ts->pPauseScreen, false);
    game_work.mHud->AddControl(game_work.m_TutorialControl, false);

    // Step 12: WaveManager::GetInstance + Init
    WaveManager::GetInstance()->Init();

    // Step 13: GameTaskInitInput (16 touch regions + input callbacks)
    GameTaskInitInput();

    // Step 14: Pre-spawn 30x (types 0=Fruit, 1=Bomb, 4=Splat)
    {
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        for (int i = 0; i < 30; ++i) {
            if (Mortar::Entity* e0 = am->Add(0, true)) e0->flags |= 0x11;
            if (Mortar::Entity* e1 = am->Add(1, true)) e1->flags |= 0x11;
            if (Mortar::Entity* e4 = am->Add(4, true)) e4->flags |= 0x11;
        }
    }

    // Step 15: SplatEntity::CreatePool(0x100 = 256)
    SplatEntity::CreatePool(0x100);

    // Step 16: WaveManager::Resume (restores wave state from save)
    WaveManager::GetInstance()->Resume();

    // Step 17: BombFlash::CreatePool(0x20 = 32)
    BombFlash::CreatePool(0x20);

    // Step 18: SoundManager init + volume
    {
        Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();
        sm.Initialise("assets/sound");
        const float sfxVol = game_work.m_bSoundOn ? 0.5f : 0.0f;
        sm.SetSFXVolume(sfxVol);
    }

    // ================================================================
    // Port-specific additions (not present in v1.6.1 binary GameInit
    // @ 0x001ce1c0, but required by the port's infrastructure until
    // the relevant init paths are ported from elsewhere in the binary).
    // ================================================================

    // MeshManager loads (binary: loaded in a different init function)
    {
        Mortar::MeshManager* mm = Mortar::MeshManager::GetInstance();
        if (mm) {
            ts->sliceFxMesh     = mm->Load("models/fruit/slice_fx.mmd");
            ts->sliceFxCritMesh = mm->Load("models/fruit/slice_fx_crit.mmd");
        }
    }

    // SliceEffect list + pool are owned by Fruit TU (s_slices + s_pool).
    // Created in Fruit::LoadFruitModels (v1.6.1 @0x001e10c4). No explicit init here.

    // The 16 per-finger SlashEntities are allocated by GameTaskInitInput above
    // (step 13), exactly as the binary does -- the port used to allocate a
    // SECOND set of 16 here, which ActorManager then updated and drew alongside
    // the ones the touch callbacks actually drive. Only the alias is left.
    g_pSlashEntity = g_pSlashEntities[0];

    // Re-apply equipped blade now that SlashEntities exist
    {
        ItemManager* im = ItemManager::GetInstance();
        im->SetEquippedItem(ITEM_TYPE_BLADE, im->GetEquipped(0));
    }
}

// ASM-spec v1.6.1 GameUpdate @ 0x001cf534
// (Downgraded from ASM-verified 2026-06-18: the body carried two port-added
// mGameSound null tests in the bomb-fuse block that the binary does not have
// (0x001cfd3c / 0x001cfe00 load +0x18c and deref it straight), so that diff
// cannot have been clean. Re-verify before restamping.)
//
// slowTime/slowTimeSpeed/slowTimeTime globals -- bss/data statics from the
// binary's GameTask.cpp translation unit (SlowTime sets slowTimeTime=1).
// quickener is the wave-stress multiplier, equivalent to the previously
// port-centric g_PUM_WaveStress. These replace the earlier g_DtModDecayTimer
// based dt-scaling approach.
static float slowTime      = 1.0f;    // _ZL8slowTime @ 0x002d8c84 (ST .data 0x3f800000)
static float slowTimeSpeed = 1.0f;    // _ZL13slowTimeSpeed @ 0x002d8c88 (ST .data 0x3f800000)
static float slowTimeTime  = 0.0f;    // _ZL12slowTimeTime @ 0x00316704 (.bss -- zero; SlowTime sets to 1.0)
static float quickener     = 1.0f;    // _ZL9quickener @ 0x002d8cd8 (ST .data 0x3f800000)

// Per-frame particle-dt divisor. GameUpdate computes it once and stores it here;
// GameDraw only READS it, so a frame whose GameUpdate bailed early keeps the previous
// frame's value. Binary spelling ("paticlesDt") preserved.
// ASM-spec v1.6.1 GameUpdate @0x001cfb8c (writer) / GameDraw @0x001cda18 (reader).
// File-local like its neighbours: 0x002d8ca4 sits INSIDE the _ZL static block
// (slowTimeSpeed 0x2d8c88 .. quickener 0x2d8cd8), and both the writer (GameUpdate)
// and the reader (GameDraw) live in this TU. A non-static global here would export
// a symbol the binary does not have.
static float paticlesDt = 1.0f;       // _ZL10paticlesDt @ 0x002d8ca4 (ST .data 0x3f800000)

// Global pause flag block @ 0x00316700 (binary .bss, adjacent to slowTimeTime @ 0x316704).
// These occupy 0x316708/0x31670c/0x316710; the 4-byte gap at 0x316700..0x316707 is the
// unnamed block head (possibly alignment / reserved).
// PauseGame sets unpauseDelay + clears unpause_game.
// UnpauseGame sets repauseDelay + arms unpause_game.
// GameDraw tail fires the actual bM_Mode clear + ClearActions when unpause_game is armed.
// v1.6.1 PauseGame @0x001ca48c, UnpauseGame @0x001ca4b4, GameDraw tail @0x001cdd64.
float g_unpauseDelay  = 0.0f;   // @ 0x00316708
int   g_unpause_game  = 0;      // @ 0x0031670c  (byte in binary; int here for portability)
float g_repauseDelay  = 0.0f;   // @ 0x00316710
int   g_clearInput    = 0;      // @ 0x00316799 (byte in binary; see GameWork.h)

// ASM-spec v1.6.1 AddCoins @ 0x00119f78
void AddCoins(int delta) {
    game_work.m_CoinsBalance += delta;
    if (delta > 0)
        game_work.m_CoinsTotalEarned += delta;
}

// ASM-spec v1.6.1 SlowTime @ 0x001ca37c
void SlowTime(float scale, float duration) {
    slowTimeTime  = 1.0f;
    slowTime      = scale;
    slowTimeSpeed = 1.0f / duration;
}

// ASM-spec v1.6.1 RemoveFlashEntities @ 0x001cb4b0
// Iterate type-4 (BombBlast) entities and set ENT_SKIP_MASK (0x11) on each.
// Binary ORs ENT_INACTIVE|ENT_KILLED = 0x11; DeactivateAllEntities only sets 0x10 -- real bug.
void RemoveFlashEntities() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(4, it);
    while (e != NULL) {
        e->flags |= ENT_SKIP_MASK;
        e = am->GetEntityNext(4, it);
    }
}

// ASM-spec v1.6.1 UnpauseSlices @ 0x001da910
// Clear m_pFruit on every active SliceEffect so trails keep animating after host fruit removal.
void UnpauseSlices() {
    Mortar::List<SliceEffect>* list =
        static_cast<Mortar::List<SliceEffect>*>(GetTaskState()->pSliceEffectList);
    if (!list) return;
    for (Mortar::List<SliceEffect>::Iterator it(list->m_pHead); it.Okay(); ++it) {
        (*it).m_pFruit = 0;
    }
}

// ASM-spec v1.6.1 InstantLevelDestroy @ 0x001cbcd8
// DIFFERS: binary s_bombSound @0x00316770 = standalone static; port uses ts->m_pBombFuseSound (established convention)
void InstantLevelDestroy() {
    GameTaskState* ts = GetTaskState();
    Mortar::MortarSound* fuse = ts->m_pBombFuseSound;
    if (fuse)
        fuse->SetVolume(0.0f);
    game_work.retryFlag = 0;                    // +0x06; strb @0x001cbd10
    WaveManager::GetInstance()->ResetGlobalDt(1.0f);
    game_work.bM_bPaused = 1;                   // +0x05; strb @0x001cbd20
    SplatEntity::RemoveAllSplats();
    ClearPause();
    ResetGameEntities(true);
    RemoveFlashEntities();
    game_work.bM_bPaused = 1;                   // set twice -- binary is literal; strb @0x001cbd3c
    game_work.m_PauseAmount = 0.0f;                 // +0x0C (Ghidra: flM_PauseAmount); vstr @0x001cbd40
    game_work.retryFlag = 0;                    // set twice -- binary is literal; strb @0x001cbd44
}

// ASM-spec v1.6.1 TouchReleasedCallback @ 0x001ca838
// Called when a finger lifts; routes the release to the active input sink with the
// per-finger spawn position. Registered via SetTouchReleasedCallback in StartNewsRender
// (defunct online news path -- see FruitNinjaNewsControl::StartNewsRender).
int TouchReleasedCallback(InputEvent* event) {
    if (game_work.m_pActiveTouchSink == NULL) return 0;
    // Touch<n+1> button event: the key id at InputEvent +0x08 names the finger
    // (Touch1 = 0x89, see Mortar::InputManager::ParseKey @0x002438c8).
    int finger = (int)event->m_KeyId - (int)INPUT_KEY_TOUCH1;
    game_work.m_pActiveTouchSink->TouchReleased(event, &game_work.m_FingerSpawnPos[finger]);
    return 1;
}

// ASM-spec v1.6.1 IsSingleTouchPressed @ 0x001ca6f8
bool IsSingleTouchPressed() {
    int count = 0;
    for (unsigned int finger = 0; finger < 16; finger++) {
        if (IsTouchDown(finger))
            count++;
    }
    if (game_work.m_bTouchDownThisFrame != 0)
        return (count == 1);
    return false;
}

void GameUpdate(float dt, bool active) {
    Game* game = Game::GetInstance();

    // --- Gap 1: deferred control drain (0x001cf558..0x001cf57c) ---
    {
        GameTaskState* ts = GetTaskState();
        if (ts->pDeferredControl) {
            game_work.mHud->AddControl(ts->pDeferredControl, false);
            ts->pDeferredControl = nullptr;
        }
    }

    // === Splash phase (0x001cf580..0x001cf634) ===
    // DIFFERS: binary draws splash exclusively during loading with a
    // LoadingJob::CanBoot gate; port draws game underneath with splash on top.
    GameTaskState* splashTs = GetTaskState();
    if (splashTs->splashFadeTimer > 0.0f) {
        if (!game->m_StartupTexture) {
            game->m_StartupTexture = Mortar::TextureManager::LoadLocalisedTexture("HB_logo.tex");
        }
        // Port specific: web audio-consent overlay -- freeze the splash timer
        // (and therefore DrawStartFade's visible frame) while the overlay is
        // up, so the splash holds steady behind it instead of fading out from
        // under an unaddressed browser-gesture requirement. See
        // FN::g_AudioConsentPending's header comment (StartupEffects.h) for
        // the full set/clear sequence. Always false on desktop/bada, so this
        // is a no-op everywhere except the Emscripten build.
        if (!FN::g_AudioConsentPending) {
            splashTs->splashFadeTimer -= dt * 2.0f;
            if (splashTs->splashFadeTimer <= 0.0f) {
                splashTs->splashFadeTimer = 0.0f;
                game->m_StartupTexture.SetNull();
            }
        }
    } else {
        // Port specific: the port's single per-tick Mortar::Touch ring drain now
        // happens in InputTranslatorSDL::DispatchForSimTick (stepUpdate, before
        // GameTaskUpdate). Draining again here would re-snapshot states1 and demote
        // the press-edge (phase -1 -> 0) before the HUD poll consumers
        // (MenuButton/ScrollingMenu/BSButton, IsTouchDown==2) read it, making shop
        // buttons and scroll unresponsive (#176).
        // NB: the binary's GameUpdate @0x001cf534 has NO Touch::Update of its own -- it
        // only ages pM_FingerSpawnPos.z (2->0->-1) and calls InputManager::Update; the
        // binary's single ring drain lives in FruitNinja::Draw. So dropping the drain
        // here makes GameUpdate MATCH the binary (no asm-verify divergence), not diverge
        // from it -- hence no __bada__ guard is warranted.
        // The InputManager::Update call itself is NOT here: in the binary it sits at
        // 0x001cf6cc, i.e. AFTER the m_bTouchDownThisFrame clear and the
        // pM_FingerSpawnPos.z aging loop. See the "Per-frame input-device poll" block
        // further down, which is where the port places it for the same reason.
    }

    // --- Common update block: sound + music (0x001cf6b8..0x001cf7d0) ---
    // Binary gates this on LoadingJob::IsLoaded(); port calls unconditionally.
    game_work.mGameSound->Update();
    UpdateMusic(dt);
    // ASM-spec v1.6.1 GameUpdate @0x001cf7c8: ItemManager::Update runs here, after
    // UpdateMusic and before UpdateUpsideDown, with the RAW frame dt (pre quickener/slow-mo).
    ItemManager::GetInstance()->Update(dt);
    // ASM-spec v1.6.1 GameUpdate @0x001cf7d0: UpdateUpsideDown(dt) runs last in the
    // LoadingJob::IsLoaded block, after ItemManager::Update, with raw frame dt.
    // ASM-spec v1.6.1 UpdateUpsideDown @0x0011a184: DeviceUpsideDown() ? timer(+0x1b0)=0.75f
    // : (timer > 0 ? timer -= dt : 0); tail-returns IsDeviceUpsideDown().
    // Call-graph fidelity only -- DeviceUpsideDown() @0x0011a14c is a hard 0, so the
    // timer only ever decays and every m_UpsideDownTimer consumer stays false.
    UpdateUpsideDown(dt);

    // --- Per-frame slot-array re-snap + touch-released dispatch (0x001cf63c..0x001cf6a8) ---
    game_work.m_bTouchDownThisFrame = 0;
    game_work.m_bTouchUpThisFrame   = 0;
    for (int i = 0; i < 16; ++i) {
        float& z = game_work.m_FingerSpawnPos[i].z;
        if (z > 0.0f) {
            z = 0.0f;
        } else if (z == 0.0f) {
            if (game_work.m_pActiveTouchSink) {
                // TODO: InputSink not yet ported; TouchReleased stub skipped.
                // Binary: InputSink::TouchReleased(sink, nullptr, &pos)
            }
            z = -1.0f;
        }
        // z < 0.0f (i.e., -1.0f): left unchanged
    }

    // --- Per-frame input-device poll (0x001cf6ac..0x001cf6d0) ---
    // ASM-spec v1.6.1 GameUpdate @0x001cf6cc:
    //   vldr s15,[splashTimer]; vcmpe s15,#0; bhi <skip>   @0x001cf6b4..0x001cf6c0
    //   bl InputManager::GetInstance  @0x001053f8
    //   vmov.f32 s0, s17              (literal 0.0f @0x001cf85c)
    //   bl InputManager::Update       @0x001070a0
    // i.e. when the splash timer is NOT > 0, GameUpdate polls the input devices
    // with dt = 0.0f. InputManager::Update @0x00243838 broadcasts to
    // InputDeviceBada::Update @0x00242f40, which runs the global-pointer state
    // machine and raises PointerMove / PointerPressed / PointerReleased.
    //
    // Placement is load-bearing: the binary calls this AFTER the
    // m_bTouchDownThisFrame / m_bTouchUpThisFrame clear above (strb @0x001cf644 /
    // @0x001cf648) and BEFORE the main dispatch that consumes them (via
    // IsSingleTouchPressed @0x001ca6f8). It cannot be hosted by
    // InputTranslatorSDL::DispatchForSimTick, which runs in stepUpdate BEFORE
    // GameTaskUpdate -- the clear would wipe the flags before any reader sees
    // them. Keep this call between the clear and the dispatch.
    //
    // No null test: the binary has none, and GameInitialise step 10 constructs
    // the InputManager before any GameUpdate can run.
    if (splashTs->splashFadeTimer <= 0.0f) {
        Mortar::InputManager::GetInstance()->Update(0.0f);
    }

    // --- Main active/inactive dispatch (0x001cf7d4) ---
    float fVar9  = dt;   // scaled gameplay dt
    float fVar10 = dt;   // camera/HUD dt (varies by bomb-hit phase)
    float fVar11 = 0.0f; // BombFlash/ActorManager dt
    if (!active) {
        // ================================================================
        // INACTIVE BRANCH (0x001cfa9c)
        // ================================================================

        // Clear slow-motion flag, then camera-settle auto-clear.
        // v1.6.1 GameUpdate @0x001cfaec (param_2==0 / menu path):
        //   pa = game_work.m_PauseAmount (+0x0c flM_PauseAmount);
        //   settled = (pa >= 0) ? (pa > 0.999f) : (pa < -0.999f);
        //   if (settled && (ps == 0 || ps->m_State != 3)) bM_Mode = 0;
        // This is THE auto-clear; QuitToMenu/UnpauseGame never write bM_Mode directly.
        game_work.m_bSlowMotion = false;
        {
            static const float THR = 0.99896961f;  // DAT_0x001cfe74
            const float pa = game_work.m_PauseAmount;
            const bool settled = (pa >= 0.0f)
                ? (pa > THR)
                : (pa < -THR);
            if (settled) {
                PauseScreen* ps = GetTaskState()->pPauseScreen;
                if (ps == 0 || ps->m_State != PAUSE_STATE_ACTIVE) {
                    game_work.bM_Mode = false;
                }
            }
        }

        // SaveData active-game flag: set when paused
        if (game_work.bM_Mode) {
            game_work.m_SaveData->m_bHasActiveGame = 1;
        }

        // SlashEntity: one PreUpdate(dt=0), then per-instance Update+PostUpdate
        for (int i = 0; i < 16; ++i) {
            if (g_pSlashEntities[i]) {
                g_pSlashEntities[i]->PreUpdate(0.0f);
                break;
            }
        }
        for (int i = 0; i < 16; ++i) {
            if (g_pSlashEntities[i]) {
                g_pSlashEntities[i]->Update(dt);
                g_pSlashEntities[i]->PostUpdate(dt);
            }
        }

        // WaveManager frozen (dt=0) while inactive
        WaveManager::GetInstance()->Update(0.0f);

        fVar9  = dt;
        fVar10 = dt;

    } else {
        // ================================================================
        // ACTIVE BRANCH (0x001cf7e0)
        // ================================================================

        // --- dt scaling: slowTime + quickener inline globals ---
        float slowMod;
        if (slowTimeTime > 0.0f) {
            slowTimeTime -= dt * slowTimeSpeed;
            if (slowTimeTime < 0.0f) slowTimeTime = 0.0f;
            slowMod = (slowTime - 1.0f) * slowTimeTime + 1.0f;
        } else {
            slowMod = 1.0f;
        }

        // Crit timer drain
        if (game_work.m_CritTimer > 0.0f) {
            game_work.m_CritTimer -= dt;
        }

        // Clear active-game flag (re-set below in normal-play path)
        game_work.m_SaveData->m_bHasActiveGame = 0;

        // Quickener (wave-stress) decay
        {
            const float decay = game_work.m_bSlowMotion ? 5.0f : 10.0f;
            const float newQuickener = quickener - dt * decay;
            quickener = 1.0f;
            if (newQuickener > 1.0f) quickener = newQuickener;
        }

        game_work.m_bSlowMotion = false;
        fVar9 = dt * quickener * slowMod;            // scaled gameplay dt
        game_work.dt *= quickener * slowMod;          // accumulate scaled frame time

        // SlashEntity::PreUpdate(dt) once, then SplatEntity update
        for (int i = 0; i < 16; ++i) {
            if (g_pSlashEntities[i]) {
                g_pSlashEntities[i]->PreUpdate(dt);
                break;
            }
        }
        SplatEntity::UpdateActiveSplats(dt);

        // --- Bomb-hit timer dispatch ---
        {
            const float prevBombTimer = game_work.m_BombHitTimer;
            if (game_work.m_BombHitTimer <= 0.0f) {
                // No bomb hit -- normal gameplay with WaveManager active
                game_work.m_SaveData->m_bHasActiveGame = 1;
                // ASM-spec v1.6.1 GameUpdate @0x001cf534: active branch ticks WaveManager::Update(dt)
                // unconditionally; menu spawn-suppression is WaveManager's internal bM_bPaused gate
                // (WaveManager.cpp:1123), not a dt=0 special-case here.
                WaveManager::GetInstance()->Update(fVar9);
                float wavedt = WaveManager::GetInstance()->GetWavedt(0);
                fVar11 = fVar9 * wavedt;
                fVar10 = fVar9;
            } else {
                // Bomb hit active
                GameTaskState* ts = GetTaskState();
                if (ts && ts->m_bMenuBombFlashFlag == 0) {
                    game_work.m_bSlowMotion = true;
                }

                game_work.m_BombHitTimer -= fVar9;

                // Double-drain in Arcade mode during pause transition
                if (game_work.gameMode == 2 && game_work.m_PauseAmount < 1.0f) {
                    game_work.m_BombHitTimer -= fVar9;
                }

                if (prevBombTimer > 1.55f) {
                    fVar10 = -fVar9;              // reverse camera
                } else {
                    UpdateBombHit(prevBombTimer);
                    fVar10 = fVar9 + fVar9;       // double-speed camera
                }

                // Cross-1.5 GameOver trigger
                if (prevBombTimer > 1.5f && game_work.m_BombHitTimer <= 1.5f &&
                    game_work.bM_bPaused == 0 &&
                    (!ts || ts->m_bMenuBombFlashFlag == 0)) {
                    GameOver(-1, -1.0f, -1);
                }

                if (game_work.m_BombHitTimer < 0.0f) {
                    game_work.m_BombHitTimer = 0.0f;
                }

                // BombFlash+ActorManager frozen (dt=0) during bomb hit,
                // unless in pause transition where m_PauseAmount < 0.
                fVar11 = 0.0f;
                if (game_work.m_PauseAmount < 0.0f) {
                    fVar11 = fVar9;
                }
            }
        }

        BombFlash::UpdateActiveFlashes(fVar11);
        if (game->actorManager)
            game->actorManager->Update(fVar11);
        // ASM-spec v1.6.1 GameUpdate @0x001cfa84-0x001cfa90: after ActorManager::Update,
        // `if (IsMultiplayer()) Fruit::CheckFruitDropped();`
        // Defunct: P2P multiplayer -- gate is a hard 0; v1.6.1 ::IsMultiplayer @0x0011a094.
        // The gate is load-bearing: CheckFruitDropped folds to GameOver(-1,-1.0f,0), so an
        // ungated call would fire GameOver every frame.
        if (IsMultiplayer()) {
            Fruit::CheckFruitDropped();
        }
    }

    // ================================================================
    // COMMON TAIL (0x001cfb8c)
    // ================================================================

    // DIFFERS: original = m_BladeActive shift lives in SlashEntity::DrawSlice
    // @0x001e83d4 (v1.6.1), which is correct because Bada ran render 1:1 with the
    // sim tick; the port has interpolated frames, so the shift is sim-tick-gated
    // to preserve the binary's ONE-shift-per-tick semantic.
    // Unconditional 16-slot loop mirroring GameDraw's DrawSlice loop, placed after
    // BOTH branches' SlashEntity::Update (the inactive branch's explicit loop and
    // the active branch's ActorManager::Update) so the per-tick order stays the
    // binary's re-arm -> Update -> shift.
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) g_pSlashEntities[i]->UpdateBladeLatch();
    }

    // --- PSPParticleManager ---
    {
        // ASM-spec v1.6.1 GameUpdate @0x001cfb8c..0x001cfbd8: the divisor is computed
        // ONCE per frame here and parked in paticlesDt; GameDraw only reads it.
        // The gate is bM_Mode ALONE -- while paused the binary leaves the divisor at
        // 1.0 and never divides by the wave dt. Do not fold SettingsScreen::IsOpen()
        // in here; that port-specific term belongs to the `paused` argument only.
        paticlesDt = 1.0f;
        WaveManager* wm = WaveManager::GetInstance();
        float wavedt = wm->GetWavedt(0);
        if (wavedt == 0.0f) {
            wavedt = 1.0f;
        }
        if (game_work.bM_Mode == 0) {
            float norm = 1.0f / wavedt;
            if (norm < 1.0f) {
                norm = 1.0f;
            }
            paticlesDt = norm;
        }
        const float particleDt = fVar9 / paticlesDt;   // @0x001cfbf0
        // ASM-spec v1.6.1 PSPParticleManager::Update @0x0013cee8: GameUpdate passes
        // paused = (bM_Mode != 0). The extra SettingsScreen term is port specific --
        // that modal has no binary counterpart (see its header) and does not set
        // bM_Mode, so it is OR'd into the same `paused` argument rather than carrying
        // a second freeze mechanism.
        const bool particlesPaused = (game_work.bM_Mode != 0) || SettingsScreen::IsOpen();
        PSPParticleManager::GetInstance().Update(particleDt, particlesPaused);
    }

    // --- FruitCamera::Update(fVar10) -- varies by bomb-hit phase ---
    game_work.m_FruitCamera->UpdateCamera(fVar10);

    // --- HUD::Update -- dt subdivided into 1/60 ticks during slow-mo ---
    if (!game_work.m_bSlowMotion) {
        game_work.mHud->Update(fVar10);
    } else {
        // HUD ticks at real-time rate even in slow-mo
        float remaining = fVar9;
        if (remaining == 0.0f) {
            game_work.mHud->Update(0.0f);
        } else {
            static const float HUD_TICK = 1.0f / 60.0f;
            for (; remaining > 0.0f; remaining -= HUD_TICK) {
                float step = HUD_TICK;
                if (step > remaining) step = remaining;
                game_work.mHud->Update(step);
            }
        }
    }

    // ASM-spec v1.6.1 GameUpdate @0x001cf534 (block 0x001cfc88): during slow-mo, touching ramps quickener x2/frame, clamp 5.
    if (game_work.m_bSlowMotion) {
        quickener *= (IsSingleTouchPressed() ? 2.0f : 1.0f);
        if (quickener > 5.0f) quickener = 5.0f;
    }

    // --- Bomb fuse sound (0x001cfd08..0x001cfe2c) ---
    {
        GameTaskState* ts = GetTaskState();
        GameSound* gs = game_work.mGameSound;
        const bool noSfx  = (game_work.bM_bPaused != 0);
        const bool paused = game_work.bM_Mode;
        const float metric = Bomb::GetHeighestBomb();
        Mortar::MortarSound* fuse = ts->m_pBombFuseSound;
        float vol;
        if (noSfx || metric <= 0.0f || paused) {
            vol = 0.0f;
        } else {
            // v1.6.1 GameUpdate @0x001cf534, fuse block 0x001cfcf4-0x001cfe2c: the only
            // gate on the SFXPlay is `ldr r3,[r3,#0x70] ; cmp #0 ; bne` @0x001cfd30,
            // i.e. m_pBombFuseSound == 0. mGameSound is `ldr r6,[r5,#0x18c]`
            // @0x001cfd3c straight into r0. The master volume comes from a second
            // untested load, `ldr r3,[r3,#0x18c] ; vldr.32 s14,[r3]` @0x001cfe00.
            if (!ts->m_pBombFuseSound) {
                ts->m_pBombFuseSound = gs->SFXPlay("Bomb-Fuse", 0.0f, 1.0f);
            }
            float t = metric / 100.0f;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
            vol = t * gs->m_MasterVolume;
        }
        if (fuse) fuse->SetVolume(vol);
    }

    // --- Retry dispatch (0x001cfe2c..0x001cfe70) ---
    if (game_work.retryFlag != 0) {
        if (game_work.retryTimer > 0.0f) {
            RetryUpdate(fVar9);
            game_work.retryTimer -= fVar9;
        } else {
            EndRetryLevel();
        }
    }

    // --- Quit transition timer (0x001cfe70..0x001cfeac) ---
    // Uses RAW dt (function param), not the scaled fVar9.
    if (game_work.m_QuitTransitionTimer > 0.0f) {
        game_work.m_QuitTransitionTimer -= dt;
        if (game_work.m_QuitTransitionTimer <= 0.0f) {
            CleanupAndReturnToMainMenu();
        }
    }

    // --- Per-frame dirty flag clear ---
    // DIFFERS: original clears game_work.m_bFrameDirty (+0x610) at the TOP of
    // GameTaskUpdate (`strb r3,[r7,#0x610]` v1.6.1 GameTaskUpdate @0x0011a328), before the
    // state dispatch -- that is the binary's only 0-store to +0x610 program-wide, and
    // v1.6.1 GameUpdate @0x001cf534 has no clear at all. The port clears here instead
    // because +0x610 is a back/pause input latch (set by RegressMenuCallback /
    // ShowPauseMenuCallback) read by MenuButton::Update @0x0019ad1c, which runs inside
    // mHud->Update above -- i.e. inside the dispatch. The port pumps input synchronously
    // right before GameTaskUpdate (Game::stepUpdate -> DispatchForSimTick), so clearing at
    // the binary's site would zero the latch between the set and the read every frame and
    // the back-key forced slice would never fire. Bada dispatched those callbacks outside
    // the frame tick. Restoring the binary's placement needs the input-dispatch ordering
    // RE'd first.
    game_work.m_bFrameDirty = false;
}

// v1.6.1 CleanupAndReturnToMainMenu @ 0x00157620 -- bx lr (empty body in v1.6.1; may
// have had teardown logic in earlier builds). Call site: GameUpdate quit-transition timer.
// ASM-verified-spec v1.6.1 CleanupAndReturnToMainMenu @0x00157620 -- empty stub.
void CleanupAndReturnToMainMenu() {
}

// ASM-spec v1.6.1 DrawBackground @ 0x001ccaf4
// Draws the current background texture quad for the game screen.
// Normal path (ViewIsNormal == true): full-screen background quad at Z=-5599.
// Non-normal path (ViewIsNormal == false): 3x3 UV-seam grid for rotated camera.
// TODO: v1.6.1 DrawBackground @0x001ccaf4 non-normal 3x3 UV-seam grid path
//   (fires only with PT_ROTATED_CW / PT_ROTATED_CCW perspective; deferred until
//   the 3x3 grid UV math is RE'd).
void DrawBackground() {
    Mortar::Texture* bgTex = GetCurrentBackground();
    if (!bgTex) return;

    if (game_work.m_FruitCamera && !game_work.m_FruitCamera->ViewIsNormal()) {
        // TODO: v1.6.1 DrawBackground @0x001ccaf4 non-normal 3x3 UV-seam grid path.
        // Falls through to normal path as a safe fallback.
    }

    // Normal path: Scale(481, 321, 1) Translate(0, 0, -5599) DrawQuad(cropped UVs)
    // UV window: u0=0.03125, u1=0.96875, v0=0.1875, v1=0.8125
    bgTex->Set();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    // DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 240 under __bada__ --
    // scale the existing dojo art wider to fill the widened field instead of
    // re-authoring the background asset. Height/UV window unchanged.
#ifdef __bada__
    Matrix44 mat = Matrix44::MakeScale(481.0f, 321.0f, 1.0f);
#else
    Matrix44 mat = Matrix44::MakeScale(481.0f * (Layout::HalfWidth() / 240.0f), 321.0f, 1.0f);
#endif
    mat.GlobalTranslate44(_Vector3<float>(0.0f, 0.0f, -5599.0f));
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // v1.6.1 DrawBackground @0x001ccaf4: background quad vertex colour is the HUD
    // WORLD tint via TintWhite(&HUD::scales[3]) @0x0010fea8 -- NOT hardcoded white.
    // This is the Arcade 2x / freeze / etc. whole-scene darken (ScreenEffect
    // backTint drives scales[3..5]). Splats read the same tint; fruit models stay white.
    // Both arms of the FruitCamera::ViewIsNormal branch (@0x001ccb38) do
    // `ldr r1,[r4,#0x40] ; add r1,r1,#0x14 ; bl TintWhite` (0x001ccba8 / 0x001ccc34)
    // with no cmp -- there is no white fallback in the binary.
    Colour bgTint = Colour::TintWhite(game_work.mHud->scales[3],
                                      game_work.mHud->scales[4],
                                      game_work.mHud->scales[5]);
    Renderer::GetInstance()->DrawQuad(bgTint, 0.03125f, 0.96875f, 0.1875f, 0.8125f);

    bgTex->UnSet();
}

// GameDraw -- v1.6.1 @0x001cd720. Full render frame.
//
// Structure: the ENTIRE draw body is wrapped in `active && IsRenderingAllowed()`;
// the tail (unpause drain, clearInput drain, HUD::Draw(0x800)) runs even when
// `active` is false. The one exception is the LoadingJob::CanBoot() early return at
// 0x001cd84c, which leaves the function outright -- it skips the tail too.
//
// The frame is cut into ten camera passes. Every pass re-runs
// FruitCamera::SetupPerspective(<type>, /*forceUpdate=*/true) via the thunk at
// 0x00106ec4; forceUpdate is 1 in all ten call sites. Perspective types (see
// FruitCamera::PERSPECIVE_TYPE): 0 = depth-on 3D, 1 = depth-off 2D, 4 = screen-space.
//
//   SP#1  @0x001cd844  type 1  DrawBackground
//   SP#2  @0x001cd8b4  type 0  ActorManager::Draw
//   SP#3  @0x001cd944  type 1  HUD 0x40 + splats/shadows/PreDraw/blasts/flashes + HUD 0x80
//   SP#4  @0x001cd9ac  type 0  FruitRay::DrawRays, DrawSlices(pass=1), pm.Draw(-1)
//   SP#5  @0x001cda78  type 4  the 16 blade DrawSlice calls -- CONDITIONAL, see below
//   SP#6  @0x001cdae0  type 0  pm.Draw(0), DrawSlices(pass=0)
//   SP#7  @0x001cdb7c  type 4  HUD 0x01
//   SP#8  @0x001cdba8  type 0  pm.Draw(1)
//   SP#9  @0x001cdc04  type 4  WaveManager, HUD 0x08/0x400, PostEffects, CritHit,
//                              HUD 0x100, DrawBombHit
//   SP#10 @0x001cdcc0  type 0  HUD 0x200 + the news gate
//
// SP#5 is the only guarded one: `vcmpe s15,#0 ; bls` @0x001cda58 skips the
// SetupPerspective when m_BombHitTimer > 0, so during a bomb hit the 16 blades draw
// under SP#4's type-0 perspective instead. The blade loop itself is unconditional
// and has no null check.
//
// Ambience is the DisplayManager global-ambience colour (vslot +0x5c,
// SetGlobalAmbience(Colour::PlatformColour())), NOT SetDrawColour. It is Black for
// the frame except three White windows: around ActorManager::Draw, around
// FruitRay::DrawRays + DrawSlices(pass=1), and around DrawSlices(pass=0).
//
// HUD tint scales[0..2] are saved at function ENTRY (@0x001cd764), forced to 1.0
// just before SP#9 (@0x001cdbdc) so the overlay passes ignore ScreenEffect tinting,
// and restored at 0x001cdcfc -- BEFORE DrawStartFade, not after.
//
// The binary also builds a grey Colour(0x40,0x40,0x40,0xff) at 0x001cd7bc and never
// uses it (dead leftover); it is deliberately not ported.
void GameDraw(float dt, bool active) {
    Game* game = Game::GetInstance();
    GameTaskState* ts = GetTaskState();

    PSPParticleManager& pm = PSPParticleManager::GetInstance();
    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();

    // @0x001cd72c `cmp r0,#0 ; beq tail` and @0x001cd744 vslot +0x68
    // (DisplayManager::IsRenderingAllowed) `cmp r0,#0 ; beq tail`.
    if (active && dm.IsRenderingAllowed()) {
        // @0x001cd764: snapshot the HUD tint scales before anything draws.
        // PowerUpManager::SetDefaults resets them each GameUpdate tick and
        // ScreenEffect::Update multiplies them, so the overlay passes below force 1.0
        // and this snapshot puts the ScreenEffect values back at 0x001cdcfc.
        float savedScales[3];
        savedScales[0] = game_work.mHud->scales[0];
        savedScales[1] = game_work.mHud->scales[1];
        savedScales[2] = game_work.mHud->scales[2];

        // @0x001cd78c / 0x001cd7a0: depth write OFF then depth test OFF, before the
        // background quad. Depth func stays at GL_LESS from BeginFrame.
        dm.SetDepthBufferWrite(false);
        dm.SetDepthBuffer(false);

        const Colour white(Colour::White);   // @0x001cd7cc
        const Colour black(Colour::Black);   // @0x001cd7dc

        dm.SetGlobalAmbience(black.PlatformColour());  // @0x001cd800

        // @0x001cd834: per-frame light direction = (worldPos.x, worldPos.y, 100).
        // worldPos (+0x94) doubles as the global pointer position, so the scene
        // light tracks the last touch.
        dm.SetLightDirection(_Vector3<float>(game_work.worldPos.x,
                                             game_work.worldPos.y,
                                             100.0f));

        // SP#1 @0x001cd844
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD_2D, true);

        // DrawBackground (v1.6.1 @0x001ccaf4) -- factored from inline to free function.
        DrawBackground();

        // DIFFERS: original = `if (!LoadingJob::CanBoot()) { if (s_startFadeInTime > 0)
        //   DrawStartFade(); return; }` (v1.6.1 GameDraw @0x001cd84c; the false arm
        //   branches to the epilogue at 0x001cde14, skipping even the tail), using an
        //   unconditional true because the port has no async LoadingJob -- CanBoot() is
        //   always true here (same reading as Game::Paused / Game::UnPaused) and the port
        //   deliberately draws the game underneath the splash instead of replacing it
        //   (see GameUpdate's splash-phase DIFFERS). The false arm is unreachable, so it
        //   is not emitted.

        // @0x001cd888 / 0x001cd89c: depth write + depth test back ON for the 3D pass.
        dm.SetDepthBufferWrite(true);
        dm.SetDepthBuffer(true);

        // SP#2 @0x001cd8b4
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);

        // Ambience window 1 (White) -- @0x001cd8d8 .. 0x001cd904.
        dm.SetGlobalAmbience(white.PlatformColour());
        // Port specific: wireframe debug mode (F2). Renderer::SetWireframe is a
        // no-op where glPolygonMode is unavailable (GLES / Emscripten).
        if (FN::g_DebugWireframe) game->renderer.SetWireframe(true);
        game->actorManager->Draw(game->renderer);   // @0x001cd8e0
        if (FN::g_DebugWireframe) game->renderer.SetWireframe(false);
        dm.SetGlobalAmbience(black.PlatformColour());

        // @0x001cd918 / 0x001cd92c: depth test stays ON, depth writes OFF for the
        // HUD / splat / blast block.
        dm.SetDepthBuffer(true);
        dm.SetDepthBufferWrite(false);

        // SP#3 @0x001cd944
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD_2D, true);

        game_work.mHud->BeginDraw(dt);                          // @0x001cd958
        game_work.mHud->Draw(Mortar::HUD_LAYER_MENU_BG);        // @0x001cd96c  0x40
        SplatEntity::DrawActiveSplats();                        // @0x001cd970
        Fruit::DrawShadows();                                   // @0x001cd974
        // SlashEntity::PreDraw -- blade pre-pass. ONE call, like the neighbouring
        // draw-them-all statics: v1.6.1 SlashEntity::PreDraw @0x001e8514 ignores `this`
        // and walks the 8 global ghost slots itself (@0x001cd978 `bl 0x0010a240`).
        SlashEntity::PreDraw();
        // Shockwave rings belong to this post-actor block, NOT to layer 0x200.
        BombBlast::DrawActiveBlasts();                          // @0x001cd97c
        BombFlash::DrawActiveFlashes();                         // @0x001cd980
        game_work.mHud->Draw(Mortar::HUD_LAYER_POST_ACTOR);     // @0x001cd994  0x80

        // SP#4 @0x001cd9ac
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);

        // Ambience window 2 (White) -- @0x001cd9d0 .. 0x001cda04.
        dm.SetGlobalAmbience(white.PlatformColour());
        FruitRay::DrawRays();                                   // @0x001cd9d4
        // @0x001cd9e0 `mov r0,#1 ; bl DrawSlices` -- the modelIdx==3 pass. This is the
        // super-fruit slice model; without it that model never draws.
        DrawSlices(dt, true);
        dm.SetGlobalAmbience(black.PlatformColour());

        // Particle dt: the binary recomputes s0 = frameDt / paticlesDt before each of
        // the three pm.Draw calls (@0x001cda20 / 0x001cdafc / 0x001cdbc4) and passes
        // paused = (bM_Mode != 0).
        // ASM-spec v1.6.1 GameDraw @0x001cda18: GameDraw only READS the paticlesDt
        // global (@0x002d8ca4); its sole writer is GameUpdate @0x001cfb8c. Do not
        // recompute it here -- on a frame whose GameUpdate bailed early the binary
        // divides by the STALE value.
        const float particleDt = dt / paticlesDt;
        // ASM-spec v1.6.1 PSPParticleManager::Draw @0x0013eccc: GameDraw passes
        // paused = (bM_Mode != 0) to all three pm.Draw calls. The extra
        // SettingsScreen term is port specific -- that modal has no binary
        // counterpart and does not set bM_Mode, so it is OR'd into the same
        // `paused` argument rather than carrying a second freeze mechanism.
        const bool particlesPaused = (game_work.bM_Mode != 0) || SettingsScreen::IsOpen();

        // @0x001cda34: background particles, drawn BEHIND the logo/shade.
        pm.Draw(particleDt, particlesPaused, -1);

        // @0x001cda48: depth test off for the blade loop and every later 2D pass.
        dm.SetDepthBuffer(false);

        // SP#5 @0x001cda78 -- guarded. `vcmpe s15,#0 ; bls` @0x001cda58 tests
        // m_BombHitTimer: the perspective switch only happens when NO bomb hit is
        // running. During a bomb hit the loop below falls through on SP#4's type 0.
        if (game_work.m_BombHitTimer <= 0.0f) {
            game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_GENERIC, true);
        }

        // @0x001cdaa4: 16 x `ldr r0,[r3,r5] ; ldr r3,[r0] ; ldr r3,[r3,#0x34] ; blx r3`
        // -- unconditional, no null test in the binary. ActorManager::Draw above already
        // walked the type-3 SlashEntity slots but their Draw(Renderer&) vtable slot is a
        // `bx lr` stub, so all blade rendering comes from here.
        // Slot confirmed: _ZTV11SlashEntity @0x002cea08 (vptr 0x2cea10), +0x34 ->
        // v1.6.1 SlashEntity::DrawSlice @0x001e83b0.
        for (int i = 0; i < 16; ++i) {
            if (g_pSlashEntities[i]) g_pSlashEntities[i]->DrawSlice();
        }

        // SP#6 @0x001cdae0
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);

        pm.Draw(particleDt, particlesPaused, 0);                // @0x001cdb10

        // Ambience window 3 (White) -- @0x001cdb34 .. 0x001cdb64.
        dm.SetGlobalAmbience(white.PlatformColour());
        DrawSlices(dt, false);                                  // @0x001cdb40  modelIdx != 3
        dm.SetGlobalAmbience(black.PlatformColour());

        // SP#7 @0x001cdb7c -- HUD 0x01 (MainScreen logo + shade) draws AFTER the blade
        // so the logo sits in front of it.
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_GENERIC, true);
        game_work.mHud->Draw(Mortar::HUD_LAYER_DEFAULT);        // @0x001cdb90  0x01

        // SP#8 @0x001cdba8
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);
        pm.Draw(particleDt, particlesPaused, 1);                // @0x001cdbd8

        // @0x001cdbf8: force the tint scales to 1.0 so the 0x08 / 0x400 / 0x100 / 0x200
        // overlay passes are NOT affected by ScreenEffect gameplay tinting (which can
        // drive them to 0 and turn every overlay sprite black).
        game_work.mHud->scales[0] = 1.0f;
        game_work.mHud->scales[1] = 1.0f;
        game_work.mHud->scales[2] = 1.0f;

        // SP#9 @0x001cdc04
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_GENERIC, true);

        WaveManager::GetInstance()->Draw(0);                    // @0x001cdc10

        // DIFFERS: original = no pause bg-dim (v1.6.1 GameDraw @0x001cd720). Port injects a full-screen
        //   ~50% black quad between WaveManager::Draw(0) and HUD::Draw(0x08) so the frozen gameplay
        //   dims while the manual-pause UI (layers 0x08/0x108/0x100) stays bright.
        //   Alpha source is PauseScreen::m_Alpha ONLY (the manual pause overlay's own state machine).
        //   Do NOT drive this from game_work.m_PauseAmount: GameOverScreen::Update ramps it to 1.0
        //   and HOLDS it there for the entire game-over display in every mode (STATE_MAIN_DISPLAY /
        //   STATE_RETRY_PREPARE), while all game-over/bonus UI -- GameOverScreen banner,
        //   FruitFactControl, BonusScreen (all HUD_LAYER_POST_ACTOR 0x80) -- draws BEFORE this quad;
        //   only ScoreControl (0x08 at game over) draws after. An m_PauseAmount term therefore dims
        //   the whole game-over screen except the score (regression from f4a473ac). It also never
        //   yields a bonus-reveal dim: m_PauseAmount stays 0 throughout STATE_BONUS_PHASE.
        //   PauseScreen::m_Alpha stays 0 for all of game-over (its state machine only leaves
        //   PAUSE_STATE_HIDDEN via the manual-pause / pause-quit paths), so this dim is
        //   manual-pause-only by construction.
        {
            PauseScreen* ps = ts->pPauseScreen;
            const float dimA = ps ? ps->m_Alpha : 0.0f;
            if (dimA > 0.0f) {
                MatrixManager& mm = MatrixManager::GetInstance();
                mm.GetWorldStack().Reset();
                // Scale(481,321,1) matches DrawBackground's full-ortho-coverage quad above.
                // DIFFERS: opt-in widescreen (Layout::HalfWidth); faithful 240 under __bada__ --
                // widen alongside DrawBackground so the pause dim has no side gap.
#ifdef __bada__
                Matrix44 dimMat = Matrix44::MakeScale(481.0f, 321.0f, 1.0f);
#else
                Matrix44 dimMat = Matrix44::MakeScale(481.0f * (Layout::HalfWidth() / 240.0f), 321.0f, 1.0f);
#endif
                dimMat.GlobalTranslate44(_Vector3<float>(0.0f, 0.0f, 0.0f));
                mm.GetWorldStack().SetCurrentMatrix(dimMat);
                mm.UploadModelViewOnly();
                const uint8_t dimAlpha = (uint8_t)(dimA * 128.0f);
                Renderer::GetInstance()->DrawColorQuad(Colour(0, 0, 0, dimAlpha));
            }
        }

        game_work.mHud->Draw(Mortar::HUD_LAYER_BUTTONS);        // @0x001cdc24  0x08
        // 0x400 draws BEFORE the bomb-hit white flash, so game-over fact-board text is
        // covered by the flash on quit instead of popping on top of it. (#35)
        game_work.mHud->Draw(Mortar::HUD_LAYER_FADE_MODAL);     // @0x001cdc38  0x400

        // @0x001cdc44 `ldr r0,[r3,#0x164] ; cmp r0,#0 ; beq` -- the only null test the
        // binary makes on a game_work pointer inside GameDraw.
        if (game_work.mMainScreen) {
            game_work.mMainScreen->DrawPostEffects();           // @0x001cdc50
        }

        // @0x001cdc54: IsFastHardware() && m_CritTimer > 0 -> DrawCritHit (@0x001cdc78).
        // The binary calls the FREE IsFastHardware @0x0011f394 here (no `this`), not the
        // MortarGame vtable slot 3.
        if (IsFastHardware() && game_work.m_CritTimer > 0.0f) {
            DrawCritHit();
        }

        game_work.mHud->Draw(Mortar::HUD_LAYER_P2_SCORE);       // @0x001cdc8c  0x100

        // @0x001cdc98: m_BombHitTimer > 0 -> DrawBombHit (@0x001cdca8).
        if (game_work.m_BombHitTimer > 0.0f) {
            DrawBombHit();
        }

        // SP#10 @0x001cdcc0 -- note this one is type 0, not 4.
        game_work.m_FruitCamera->SetupPerspective(FruitCamera::PT_STANDARD, true);
        game_work.mHud->Draw(Mortar::HUD_LAYER_SLIDER);         // @0x001cdcd4  0x200

        // Defunct: online news -- the triple gate is ported for shape; both
        // NetworkManager::IsShowingModalDialog and MainScreen::IsDisplayingNews are
        // hard-false stubs, so DrawNews (itself a no-op) is never reached.
        // v1.6.1 GameDraw @0x001cdcdc / 0x001cdcf4 / 0x001cdd3c.
        if (Mortar::NetworkManager::GetInstance()->IsShowingModalDialog() &&
            game_work.mMainScreen &&
            game_work.mMainScreen->IsDisplayingNews()) {
            Mortar::NetworkManager::GetInstance()->DrawNews();
        }

        // @0x001cdcfc: restore the entry snapshot so the next PowerUpManager::SetDefaults
        // reset starts from the correct per-frame base. This happens BEFORE DrawStartFade.
        game_work.mHud->scales[0] = savedScales[0];
        game_work.mHud->scales[1] = savedScales[1];
        game_work.mHud->scales[2] = savedScales[2];

        // @0x001cdd24: s_startFadeInTime > 0 -> DrawStartFade (@0x001cdd50).
        if (ts->splashFadeTimer > 0.0f) {
            DrawStartFade();
        }

        // Debug overlay -- fruit/bomb hitboxes + MenuButton AABBs + blade trails (F1 toggle)
#ifndef __bada__
        FN::DebugHitbox_Draw();
        FN::DebugHUDBounds_Draw();
        FN::DebugBladeTrails_Draw();
#endif
    }

    // ---- tail @0x001cdd5c: runs even when `active` is false ----

    // @0x001cdd64: unpause_game auto-clear. When UnpauseGame() arms unpause_game=1,
    // GameDraw fires the actual bM_Mode clear + ClearActions on the NEXT rendered frame,
    // after the fade overlay has settled -- the binary clears bM_Mode here (not in
    // UnpauseGame) so the gameplay tick cannot restart mid-fade.
    // v1.6.1 also does debugMenu(0x00316798) = 0 here; that global is write-only dead in
    // v1.6.1 (no reader) -- deliberately not ported.
    // This ClearActions IS reachable and fires on every unpause (g_unpause_game is
    // armed at PauseScreen.cpp:152). It is safe because ClearActions matches by
    // CONFIG-SOURCE hash and the port only ever loads Input/Input.txt
    // (GameTaskInput.cpp:70 is the sole LoadConfigFile call site) -- no mapper
    // carries the PauseMenu.txt source hash, so this erases nothing. It would stop
    // being safe the moment a PauseMenu.txt config is loaded. Verified 2026-08-06.
    if (g_unpause_game != 0 && game_work.bM_Mode != 0) {
        g_unpause_game = 0;
        Mortar::InputManager::GetInstance()->ClearActions(StringHash("Input/PauseMenu.txt"));
        game_work.bM_Mode = false;
    }

    // @0x001cddcc: clearInput drain -- flush the gameplay action set, then clear the flag.
    // NB: ClearActions is LIVE now that InputManager::LoadConfigFile is ported --
    // its argument is the CONFIG-SOURCE hash, so this really does destroy all 67
    // Input/Input.txt mappers and every callback bound to them. Nothing reloads
    // them short of GameExit's InputManager Destroy/Init + the next
    // GameTaskInitInput, so firing this mid-session would kill input outright.
    // Harmless today only because nothing in the port ever sets g_clearInput to 1.
    //
    // VERIFIED 2026-08-06, exhaustive: g_clearInput's lifetime value set is {0}.
    // Its only three writes all store 0 -- the static init and GameInit Step 6
    // (GameInit.cpp:174, unconditional and un-#ifdef'd, so identical on every
    // platform) and the self-clear below. Nothing takes its address (no
    // &g_clearInput, no pointer/reference bind, no debug/cheat var table), and in
    // the port it is a standalone namespace-scope int rather than a GameWork
    // member, so the binary's [r5,#0x99] offset-addressing has no port equivalent
    // and no struct-relative write can reach it. Zero occurrences in tests/ or in
    // any platform entry point. So this branch is unreachable here exactly as it
    // is in v1.6.1 -- do NOT "wire up" an armer without adding a reload first.
    // (Contrast g_unpause_game, which DOES have a real armer at
    // PauseScreen.cpp:152 -- that is what an arming path looks like.)
    if (g_clearInput != 0) {
        Mortar::InputManager::GetInstance()->ClearActions(StringHash("Input/Input.txt"));
        g_clearInput = 0;
    }

    // @0x001cde10: `ldr r0,[r3,#0x40] ; bl HUD::Draw`, no cmp. Fires unconditionally
    // outside the active-guard.
    game_work.mHud->Draw(Mortar::HUD_LAYER_TOP_MOST);
}

// v1.6.1 GameExit @0x001cfed4. Order matters:
//   1. UnloadBackground / pBackgroundTexture.SetNull
//   2. mHud->Release()   -- destroys HUDControls (MenuButtons etc.); does NOT
//                          delete their m_pEntity (ActorManager-owned).
//   3. Coin::ClearCoins, SaveCurrentData, WaveManager::Destroy, ParticleMgr clear.
//   4. ActorManager::Clear  -- Release+delete every entity in type lists,
//                              including g_pSlashEntities[16] (type 3) and
//                              fruits/bombs (type 0/1).
//   5. Null g_pSlashEntities[*] (already deleted by Clear; just drop refs).
//   6. InputManager Destroy/Init reset.
//   7. Entity::HeapDestroy (free the entity pool itself).
void GameExit() {
    LOG_INFO("GAMEINIT", "GameExit: cleaning up");

    // Release background texture (shared MenuBackground slot)
    UnloadBackground();
    // Drop any leftover GameTaskState slot too (no-op if never used)
    GameTaskState* ts = GetTaskState();
    ts->pBackgroundTexture.SetNull();

    // Release HUD (destroys all controls EXCEPT the m_bNoDestructor ones --
    // MainScreen and the MissControl pool slots, both handled explicitly below).
    // MenuButton::Release does NOT delete its m_pEntity (entities are
    // ActorManager-owned); ActorManager::Clear below handles entity deletion.
    {
        game_work.mHud->Release();
        // MainScreen is AddControl'd with m_bNoDestructor=1, so HUD::Release
        // above deliberately SKIPS it. v1.6.1 GameExit @0x001cfed4 then calls the
        // deleting dtor through the vtable (slot 1) directly, bypassing the
        // no-destructor gate -- this is the ONLY path that frees MainScreen and
        // its 11 instance textures (newgame, dojo_icon, openfeint,
        // gc_achievements, quit, more_games, hd_sound(+_cross), hd_music(+_cross),
        // hd_slice_fruit). ~MainScreen -> Release() only NULLS its MenuButton
        // pointers (they are HUD-owned and already freed above), so this is not a
        // double-free.
        // NOTE: this null test is GENUINE. v1.6.1 GameExit @0x001cfed4 reads the
        // MainScreen slot as `ldr r0,[r5,#0x1c]` (r5 = the second static block, the one
        // the port models as game_work.mMainScreen) and does `cmp r0,r6(0) ; beq
        // 0x001cffd0` at 0x001cffb4-0x001cffbc before the vtable-slot-1 deleting dtor.
        // TODO: v1.6.1 0x001cffdc (GameExit) -- the binary guards the mHud teardown the
        //   same way (`cmp r5,#0x0 ; beq 0x001cfff4` around HUD::Release @0x00110888 and
        //   the delete @0x00108ecc) and runs it AFTER the MainScreen delete. The port
        //   splits Release/delete around the MainScreen block and guards neither.
        if (game_work.mMainScreen) {
            delete game_work.mMainScreen;
            game_work.mMainScreen = nullptr;
        }
        delete game_work.mHud;
        game_work.mHud = nullptr;
    }

    // v1.6.1 GameExit @0x001cfed4 -- release the 12-slot MissControl pool. Pool slots
    // are created with m_bNoDestructor=1 so HUD::Release skips them; CleanPool
    // is the only path that actually frees them. Heap leak otherwise.
    MissControl::CleanPool();

    // Null all game_work / GameTaskState pointers that GameInit allocates
    // and HUD::Release destroys. Binary's GameInit leaks the previous values
    // on re-init (just overwrites); port's GameInit deletes-first, which
    // requires explicit nulls here so the re-run guards become no-ops.
    // (mMainScreen is nulled by its own delete block above.)
    game_work.mCoinCounter      = nullptr;
    game_work.mCountDown        = nullptr;
    game_work.m_TutorialControl = nullptr;
    ts->pPauseScreen            = nullptr;
    ts->pDeferredControl        = nullptr;
    ts->m_pBombFuseSound        = nullptr;

    // ActorManager owns SlashEntity[16] + all fruits/bombs. Clear before
    // we null the SlashEntity pointers so the entity destruction order is
    // ActorManager-driven (Release + delete per slot via the type-list walk).
    //
    // Entity teardown runs AFTER the HUD here (v1.6.1 GameExit @0x001cfed4 order),
    // the opposite of GameDestroy, which deletes ActorManager first specifically so
    // Fruit::Release / Bomb::Release can still reach live MenuButtons. That is safe
    // on this path because MenuButton::Release @0x0019d064 (reached from ~MenuButton
    // during HUD::Release above) nulls its tracked entity's m_pOwner /
    // m_pOwnerButton back-ref, so the Release calls below see a null owner.
    // Residual gap (faithful to the binary): MenuButton::Remove @0x0019b448 flings
    // the fruit and clears m_pTrackedFruit WITHOUT clearing fruit->m_pOwner, so a
    // fruit flung during a menu transition and still airborne at exit reads a freed
    // MenuButton in Fruit::Release. Fixing it means changing Remove, which is not
    // binary-faithful -- do not add a port-side guard here.
    Coin::ClearCoins(true);  // v1.6.1 GameExit @0x001cfed4 passes r0=1 (true), not false
    SaveCurrentData();           // writes FruitSaveData XML; v1.6.1 GameExit @0x001cfed4
    WaveManager::GetInstance()->Destroy();  // frees per-session wave state; v1.6.1 WaveManager::Destroy @0x00123b54
    // ASM-spec v1.6.1 GameExit @0x001cff88: `add r0,r5,#0x90; bl 0x001cf52c` --
    // releases the shared s_flashTexture (SmartPtr<Texture>::SetPtr(nullptr)).
    // Distinct from FlashTexture_UnloadStatics (GameDestroy's pre-GL-teardown
    // backstop, mirroring the binary's separate atexit dtor) -- task #141.
    g_FlashTexture.SetNull();
    PSPParticleManager::GetInstance().ClearEmitters();
    {
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        am->Clear();
        am->Destroy();
    }

    // Now ActorManager has deleted all SlashEntities -- just null the
    // global pointers (no explicit delete; that's the double-free bug
    // that crashed GameExit on quit-to-main).
    for (int i = 0; i < 16; ++i) {
        g_pSlashEntities[i] = nullptr;
    }
    g_pSlashEntity = nullptr;
    {
        Mortar::InputManager* im = Mortar::InputManager::GetInstance();
        // Binary: InputManager::Destroy (0x001968a0) clears all device callbacks.
        im->Destroy();
        im->Init(0);
    }

    Mortar::Entity::HeapDestroy();

    // Clear the initComplete guard so the next GameInit (via FrontendInit ->
    // taskStateIndex=2 handoff) can rebuild HUD + MainScreen. Without this,
    // GameInit's early-return at the initComplete check leaves the screen
    // blank after quit-to-main. v1.6.1 GameExit @0x001cfed4 head clears the
    // equivalent flag at GameTaskState +0x113.
    ts->initComplete = false;
}
