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
#include "entities/Fruit.h"
#include "entities/SlashEntity.h"
#include "entities/BombFlash.h"
#include "entities/SplatEntity.h"
#include "entities/BombBlast.h"
#include "engine/MenuBackground.h"
#include "asset/MeshManager.h"
#include "particle/PSPParticleManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "input/InputManager.h"
#include "input/Touch.h"
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
            mc->pos         = Vec3(-kMC[i].x_tbl, -kMC[i].y_tbl, 50.0f);
            mc->m_HudScale  = Vec3(0.5f, 0.5f, 0.0f);
            mc->m_Timer     = -kMC[i].rot_tbl;
            mc->m_AnimState = i;
            mc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
            const float sz = 32.0f * kMC[i].scale;
            mc->size = Vec3(sz, sz, sz);
            mc->m_Texture = MissControl::GetCrossTexture();
            game_work.mHud->AddControl(mc);
        }
    }
    MissControl::CreatePool(12, game_work.mHud);

    // Step 3: ScoreControl (sizeof 0x108 = 264 bytes)
    {
        ScoreControl* sc = new ScoreControl();
        // Binary loads three textures into specific slots.
        sc->m_Texture          = Mortar::TextureManager::LoadLocalisedTexture("score_fruit.tex");
        sc->m_ScoreIconTex     = Mortar::TextureManager::LoadLocalisedTexture("score_icon.tex");
        sc->m_HighscoreBannerTex = Mortar::TextureManager::LoadLocalisedTexture("high_score_banner.tex");
        // size = Vec3::One * 64.0
        sc->size               = Vec3(64.0f, 64.0f, 64.0f);
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
        // Binary also assigns: hud_font = pM_Fonts[1]; unpause_game = 0; clearInput = 0;
        g_unpause_game = 0;
        game_work.bM_Mode = false;
        game_work.gameMode = 0;
    }

    // Step 7: Entity system — HeapCreate + ActorManager 7-type init
    {
        ts->initComplete = true;  // one-shot guard (binary: initialised = 1)
        Mortar::Entity::HeapCreate(0x20000);
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        // Binary uses 7 types (port previously had 5). Matches v1.6.1 @ 0x001ce1c0.
        am->Initialise(7, 0x2000);
        am->RegisterFactory(&CreateEntity);
        // DIFFERS: binary registers HashTypeConvert delegate; port passes
        // nullptr (hash converter only used by LoadEntity, which is defunct).
        am->RegisterHashConverter(nullptr);
    }

    // Step 8: TutorialControl (sizeof 0xA0 = 160 bytes)
    {
        TutorialControl* tutCtrl = new TutorialControl();
        tutCtrl->Init();       // vtable slot 2
        game_work.m_TutorialControl = tutCtrl;
    }

    // Step 9: MainScreen (binary sizeof 0x12C = 300 bytes; port 0x114)
    {
        MainScreen* mainScreen = new MainScreen(*game);
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
        // ASM-verified: 2026-06-20T00:00Z v1.6.1 GameInit @ 0x001ce1c0 (asm-inspector)
        game_work.bM_Mode   = false;      // +0x02: gameplay-mode gate = inactive (menu)
        game_work.bM_bPaused = 1;         // +0x05: pause/inactive gate = suppressed
        game_work.m_GameDt  = -1.0f;     // +0x0C: flM_PauseAmount
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

    // Per-finger SlashEntity array (binary: allocated by GameTaskInitInput)
    for (int i = 0; i < 16; ++i) {
        if (!g_pSlashEntities[i]) {
            g_pSlashEntities[i] = static_cast<SlashEntity*>(game->actorManager->Add(3, true));
            g_pSlashEntities[i]->Init(i);
        }
    }
    g_pSlashEntity = g_pSlashEntities[0];

    // Re-apply equipped blade now that SlashEntities exist
    {
        ItemManager* im = ItemManager::GetInstance();
        im->SetEquippedItem(0, im->GetEquipped(0));
    }
}

// ASM-verified: 2026-06-18 v1.6.1 GameUpdate @ 0x001CF534 (asm-inspector)
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
        if (!game->pSplashTex) {
            game->pSplashTex = Mortar::TextureManager::LoadLocalisedTexture("HB_logo.tex");
        }
        splashTs->splashFadeTimer -= dt * 2.0f;
        if (splashTs->splashFadeTimer <= 0.0f) {
            splashTs->splashFadeTimer = 0.0f;
            game->pSplashTex.SetNull();
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
    }

    // --- Common update block: sound + music (0x001cf6b8..0x001cf7d0) ---
    // Binary gates this on LoadingJob::IsLoaded(); port calls unconditionally.
    game_work.mGameSound->Update(dt);
    UpdateMusic(dt);

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
        //   pa = game_work.m_GameDt (+0x0c flM_PauseAmount);
        //   settled = (pa >= 0) ? (pa > 0.999f) : (pa < -0.999f);
        //   if (settled && (ps == 0 || ps->m_State != 3)) bM_Mode = 0;
        // This is THE auto-clear; QuitToMenu/UnpauseGame never write bM_Mode directly.
        game_work.m_bSlowMotion = false;
        {
            static const float THR = 0.99896961f;  // DAT_0x001cfe74
            const float pa = game_work.m_GameDt;
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
                if (game_work.gameMode == 2 && game_work.m_GameDt < 1.0f) {
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
                // unless in pause transition where m_GameDt < 0.
                fVar11 = 0.0f;
                if (game_work.m_GameDt < 0.0f) {
                    fVar11 = fVar9;
                }
            }
        }

        BombFlash::UpdateActiveFlashes(fVar11);
        if (game->actorManager)
            game->actorManager->Update(fVar11);
    }

    // ================================================================
    // COMMON TAIL (0x001cfb8c)
    // ================================================================

    // --- PSPParticleManager ---
    {
        float particleDtNorm = 1.0f;
        WaveManager* wm = WaveManager::GetInstance();
        float wavedt = wm->GetWavedt(0);
        if (wavedt != 0.0f) {
            particleDtNorm = 1.0f / wavedt;
        }
        if (!game_work.bM_Mode && particleDtNorm < 1.0f) {
            particleDtNorm = 1.0f;
        }
        PSPParticleManager::GetInstance().Update(fVar9 / particleDtNorm, false);
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

    // --- Post-HUD quickener recovery (binary: gated on IsSingleTouchPressed) ---
    // TODO: v1.6.1 0x001CF534 (GameUpdate) -- quickener recovery from touch state.

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
            if (!ts->m_pBombFuseSound && gs) {
                ts->m_pBombFuseSound = gs->SFXPlay("Bomb-Fuse", 0.0f, 1.0f);
            }
            float t = metric / 100.0f;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
            vol = t * (gs ? gs->m_MasterVolume : 1.0f);
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
            // Binary: blx CleanupAndReturnToMainMenu v1.6.1 @0x00157620 (empty body in v1.6.1).
        }
    }

    // --- Per-frame dirty flag clear ---
    game_work.m_bFrameDirty = false;
}

// Matches GameDraw (v1.6.1 @0x001cd720, 211 lines) -- full render frame.
//
// Binary draw order (verified from decompile, see comments inline):
//   1.  Camera + background quad
//   2.  Mortar::ActorManager::Draw (3D entities -- fruit, bomb, SlashEntity)
//   3.  HUD::BeginDraw
//   4.  HUD::Draw(0x40)
//   5.  SplatEntity::DrawActiveSplats / Fruit::DrawShadows /
//       SlashEntity::PreDraw / BombBlast::DrawActiveBlasts /
//       BombFlash::DrawActiveFlashes            [not yet ported]
//   6.  HUD::Draw(0x80)
//   7.  pm.Draw(-1)   -- "background" particles (useDepth=-1, earliest)
//   8.  SlashEntity::DrawSlice x16 via g_pSlashEntities vtable loop
//   9.  pm.Draw(0)    -- "mid" particles
//   10. DrawSlices    -- SlashEntity::DrawSlice blade ribbon
//   11. HUD::Draw(0x01) -- MainScreen (logo + shade). Drawn AFTER slash
//       so logo appears in front of the blade.
//   12. pm.Draw(1)    -- "foreground" particles (drawn over logo,
//       under buttons)
//   13. WaveManager::Draw                       [not yet ported]
//   14. HUD::Draw(0x08) -- buttons
//   15. HUD::Draw(0x100) + DrawBombHit + HUD::Draw(0x200)
//   16. HUD::Draw(0x400)
//
// Key insight: the particle layer indices map differently from what the
// layer names imply -- pm.Draw(-1) actually draws EARLIEST (background)
// and pm.Draw(1) draws LATER (foreground, over logo).
void GameDraw(float dt, bool active) {
    Game* game = Game::GetInstance();

    GameTaskState* ts = GetTaskState();
    // 1. Camera projection
    game_work.m_FruitCamera->SetupPerspective(PT_STANDARD, false);

    MatrixManager& mm = MatrixManager::GetInstance();

    Mortar::Texture* bgTex = GetCurrentBackground();
    // Background texture quad. Reads from the MenuBackground file-static
    // (same slot ItemManager::SetEquippedItem(BACKGROUND, ...) updates
    // via ChangeBackground), so a shop equip swaps the visible bg
    // immediately on the next frame.
    // Matches binary: Scale(481, 321, 1) Translate(0, 0, -5599) DrawQuad(cropped UVs)
    if (bgTex) {
        bgTex->Set();

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(481.0f, 321.0f, 1.0f);
        mat.GlobalTranslate44(Vec3(0.0f, 0.0f, -5599.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        Colour white(255, 255, 255, 255);
        game->renderer.DrawQuad(white, 0.03125f, 0.96875f, 0.1875f, 0.8125f);

        bgTex->UnSet();
    }

    PSPParticleManager& pm = PSPParticleManager::GetInstance();
    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();

    // === 1. Mortar::ActorManager::Draw -- 3D fruit/bomb/slash entities ===
    // v1.6.1 GameDraw @0x001cd720: SetDepthBufferWrite(1) + SetDepthBuffer(1)
    // just before Mortar::ActorManager::Draw. Depth func stays at GL_LESS set
    // by BeginFrame -- binary never overrides it.
    dm.SetDepthBufferWrite(true);
    dm.SetDepthBuffer(true);
#if !defined(__bada__) && !defined(__EMSCRIPTEN__)
    // Port specific: wireframe debug mode (F2). glPolygonMode is loaded
    // optionally by gl_funcs -- stays nullptr on GLES, so the toggle is a
    // silent no-op there. Absent under Emscripten (WebGL has no polygon mode).
    const bool wireframe = FN::g_DebugWireframe && glPolygonMode != nullptr;
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif
    game->actorManager->Draw(game->renderer);
#if !defined(__bada__) && !defined(__EMSCRIPTEN__)
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    // === 2. HUD::BeginDraw + post-actor block ===
    // v1.6.1 GameDraw @0x001cd720 right after Mortar::ActorManager::Draw:
    //   SetDepthBuffer(1)       -- depth test still ON
    //   SetDepthBufferWrite(0)  -- writes OFF for HUD/splats/bomb blasts
    dm.SetDepthBuffer(true);
    dm.SetDepthBufferWrite(false);
    {
        game_work.mHud->BeginDraw(dt);

        // 2a. HUD::Draw(0x40) -- menu button sprites (v1.6.1 GameDraw @0x001cd720)
        game_work.mHud->Draw(Mortar::HUD_LAYER_MENU_BG);

        // 2b. SplatEntity::DrawActiveSplats (v1.6.1 GameDraw @0x001cd720)
        SplatEntity::DrawActiveSplats();

        // 2c. Fruit::DrawShadows (v1.6.1 GameDraw @0x001cd720)
        Fruit::DrawShadows();

        // 2d. SlashEntity::PreDraw -- blade pre-pass for each of 16 finger slots
        //     (v1.6.1 GameDraw @0x001cd720; binary loops over SlashEntity[16]).
        for (int i = 0; i < 16; ++i) {
            if (g_pSlashEntities[i]) g_pSlashEntities[i]->PreDraw();
        }

        // 2e. BombBlast::DrawActiveBlasts (v1.6.1 GameDraw @0x001cd720) -- drawn HERE
        //     in the binary, NOT inside the 0x200 layer. Shockwave rings belong to
        //     this post-actor block.
        BombBlast::DrawActiveBlasts();

        // 2f. BombFlash::DrawActiveFlashes (v1.6.1 GameDraw @0x001cd720)
        BombFlash::DrawActiveFlashes();

        // 2g. HUD::Draw(0x80) -- DojoScreen / AboutScreen (v1.6.1 GameDraw @0x001cd720)
        game_work.mHud->Draw(Mortar::HUD_LAYER_POST_ACTOR);
    }

    // Particle dt: binary GameDraw recomputes s0=frameDt/wavedt before each
    // pm.Draw call (v1.6.1 @0x001cda20/0x001cdafc/0x001cdbc4). Mirror the same derivation
    // used by the Update path (GameInit.cpp lines 484-493).
    float particleDt;
    {
        float particleDtNorm = 1.0f;
        WaveManager* wm = WaveManager::GetInstance();
        float wavedt = wm->GetWavedt(0);
        if (wavedt != 0.0f) {
            particleDtNorm = 1.0f / wavedt;
        }
        if (!game_work.bM_Mode && particleDtNorm < 1.0f) {
            particleDtNorm = 1.0f;
        }
        particleDt = dt / particleDtNorm;
    }

    // === 3. Background particles ===
    // Binary pm.Draw(-1) @ v1.6.1 0x001cda34 -- drawn BEHIND the logo/shade.
    pm.Draw(particleDt, false, -1);

    // v1.6.1 GameDraw @0x001cd720 after pm.Draw(-1): SetDepthBuffer(0) turns
    // depth test off before the SlashEntity DrawSlice loop x16 and all
    // later 2D passes. Explicit per-finger DrawSlice dispatch loop.
    // ActorManager::Draw above already walked type-3 SlashEntity slots but
    // their Draw(Renderer&) vtable slot is a BX lr stub -- no output.
    // All blade rendering comes from here.
    // ASM-verified: 2026-05-18 v1.6.1 GameDraw @ 0x001cd720 (re-analyst)
    dm.SetDepthBuffer(false);
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) g_pSlashEntities[i]->DrawSlice();
    }

    // === 4. Mid particles + slice lines + main-screen logo ===
    // Binary pm.Draw(0) @ v1.6.1 0x001cdb10
    pm.Draw(particleDt, false, 0);

    // DrawSlices (v1.6.1 GameDraw @0x001cd720) -- slash-line pool
    DrawSlices(dt, false);   // pass=false: draw modelIdx!=3 nodes

    // HUD::Draw(0x01) -- MainScreen logo / shade (v1.6.1 GameDraw @0x001cd720)
    game_work.mHud->Draw(Mortar::HUD_LAYER_DEFAULT);

    // pm.Draw(1) -- foreground particles @ v1.6.1 0x001cdbd8
    pm.Draw(particleDt, false, 1);

    // WaveManager::Draw(0) (v1.6.1 GameDraw @0x001cd720) -- stubbed (wave-banner overlay).
    WaveManager::GetInstance()->Draw(0);

    // === 5. HUD overlay layers + flash effects ===
    {
        // HUD::Draw(0x08) -- buttons (v1.6.1 GameDraw @0x001cd720)
        game_work.mHud->Draw(Mortar::HUD_LAYER_BUTTONS);

        // MainScreen::DrawPostEffects (v1.6.1 GameDraw @0x001cd720)
        game_work.mMainScreen->DrawPostEffects();

        // DrawCritHit (CriticalFlash) (v1.6.1 GameDraw @0x001cd720) -- gated on
        // critFlash > 0 && IsFastHardware. Port has CriticalFlash
        // implemented as FN::DrawCriticalFlash.
        FN::DrawCriticalFlash();

        // HUD::Draw(0x100) -- overlays (v1.6.1 GameDraw @0x001cd720)
        game_work.mHud->Draw(Mortar::HUD_LAYER_P2_SCORE);

        // DrawBombHit (v1.6.1 GameDraw @0x001cd720) -- bomb-hit white flash, gated on
        // bombFlash > 0. Binary gates on LoadingJob::CanBoot() -- when splash
        // is exclusive (CanBoot false), DrawBombHit is never reached. Port
        // always draws game content, so suppress bomb flash while the splash
        // is active.
        {
            GameTaskState* splashTs = GetTaskState();
            if (splashTs->splashFadeTimer <= 0.0f) {
                DrawBombHit();
            }
        }

        // HUD::Draw(0x200) -- bomb-hit overlay layer (v1.6.1 GameDraw @0x001cd720)
        game_work.mHud->Draw(Mortar::HUD_LAYER_SLIDER);

        // DrawNews / DrawStartFade (v1.6.1 GameDraw @0x001cd720)
        FN::DrawNews();
        DrawStartFade();

        // Debug overlay -- fruit/bomb hitboxes + MenuButton AABBs + blade trails (F1 toggle)
#ifndef __bada__
        FN::DebugHitbox_Draw();
        FN::DebugHUDBounds_Draw();
        FN::DebugBladeTrails_Draw();
#endif

        // HUD::Draw(0x400) -- top layer (v1.6.1 GameDraw @0x001cd720), ALWAYS fires
        // (binary places it OUTSIDE the `active` block).
        game_work.mHud->Draw(Mortar::HUD_LAYER_FADE_MODAL);
    }

    // v1.6.1 GameDraw tail @0x001cdd64: unpause_game auto-clear.
    // When UnpauseGame() arms unpause_game=1, GameDraw fires the actual bM_Mode clear
    // + ClearActions on the NEXT rendered frame, after the fade overlay has settled.
    // The binary clears bM_Mode here (not in UnpauseGame directly) so the gameplay
    // tick cannot restart before the pause overlay has completely faded.
    if (g_unpause_game != 0 && game_work.bM_Mode != 0) {
        g_unpause_game = 0;
        Mortar::InputManager::GetInstance()->ClearActions(StringHash("Input/PauseMenu.txt"));
        game_work.bM_Mode = false;
    }
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

    // Release HUD (destroys all controls including MainScreen).
    // MenuButton::Release does NOT delete its m_pEntity (entities are
    // ActorManager-owned); ActorManager::Clear below handles entity deletion.
    {
        game_work.mHud->Release();
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
    game_work.mMainScreen       = nullptr;
    game_work.mCoinCounter      = nullptr;
    game_work.mCountDown        = nullptr;
    game_work.m_TutorialControl = nullptr;
    ts->pPauseScreen            = nullptr;
    ts->pDeferredControl        = nullptr;
    ts->m_pBombFuseSound        = nullptr;

    // ActorManager owns SlashEntity[16] + all fruits/bombs. Clear before
    // we null the SlashEntity pointers so the entity destruction order is
    // ActorManager-driven (Release + delete per slot via the type-list walk).
    Coin::ClearCoins(false);
    FruitNinja_SaveCurrentData();           // writes FruitSaveData XML; v1.6.1 GameExit @0x001cfed4
    WaveManager::GetInstance()->Destroy();  // frees per-session wave state; v1.6.1 WaveManager::Destroy @0x001c1be8
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
