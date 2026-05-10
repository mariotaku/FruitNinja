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
#include "audio/SoundManager.h"
#include "GameOver.h"
#include "GameTaskInput.h"
#include "StartupEffects.h"
#include "entities/Coin.h"
#include <cstdio>

// Matches GameInit (0x16c644, 274 lines) — per-session setup.
// Call order matches binary 23-step sequence (see inline step comments).
void GameInit(unsigned long) {
    Game* game = Game::GetInstance();
    if (!game) return;
    // Guard matches binary 0x0016c660: g_TaskState->initComplete (+0x112) tested at entry.
    // Without this, re-entering State 2 (GameTaskUpdate state-change path) re-runs
    // the entire 274-line setup, leaking heap + duplicating MainScreen/PauseScreen.
    GameTaskState* ts = GetTaskState();
    if (ts->initComplete) return;
    // step 1: HUD allocation (Game+0x3c)
    if (!game->hud) {
        game->hud = new HUD();
    }

    // step 2: HUD::Release post-construction housekeeping
    // Binary calls HUD::Release(hud) immediately after ctor — no-op in port
    // (HUD ctor already initialises cleanly).

    // step 3: 3x visible MissControl widgets + 12-entry pool.
    // Source: docs/structs/miss-control-init.md §2. Binary do-loop at 0x0016c694..0x0016c742
    // reads from GOT+0x30 table at 0x001F3DAC (3 rows x 16 bytes, stride=4 floats).
    // field_0x2c = m_Timer (rotation), field_0x30 = m_bActive, field_0x34 = m_LayerFlags.
    //
    // ASM-verified table dump @ 0x001F3DAC (read_memory 2026-05-10):
    //   row 0: x= 79.0  y= 10.0  rot= -5.0  scale= 0.75
    //   row 1: x= 52.0  y= 13.0  rot= +5.0  scale= 1.00
    //   row 2: x= 20.0  y= 18.0  rot=+10.0  scale= 1.20
    // Earlier port had ALL signs flipped (x and y negated, rot inverted) AND
    // dropped the scale column -- the markers landed in the bottom-left
    // corner instead of the top-right.
    {
        static const struct { float x, y, rot, scale; } kMC[3] = {
            {  79.0f,  10.0f,  -5.0f, 0.75f },   // iter 0, m_AnimState=0
            {  52.0f,  13.0f,  +5.0f, 1.00f },   // iter 1, m_AnimState=1
            {  20.0f,  18.0f, +10.0f, 1.20f },   // iter 2, m_AnimState=2
        };
        // DIFFERS: binary calls HUD::Release(hud) before this loop. In the
        // port, HUD::Release iterates the control list and `delete`s every
        // entry -- nuking any HUDControl already added by GameInitialise
        // (TutorialControl, etc.) and leaving dangling pointers in Game.
        // The binary's HUD::Release does something different (likely a
        // per-control state-reset hook). Skipping the call until the
        // binary's real semantics are RE'd.
        // TODO: RE binary HUD::Release real body, then re-add a port-safe
        // version.
        for (int i = 0; i < 3; ++i) {
            MissControl* mc = new MissControl();
            mc->m_bActive   = 1;                                // field_0x30 = 1
            mc->pos         = Vec3(kMC[i].x, kMC[i].y, 50.0f); // DAT_0016c9ac = 50.0
            mc->pivot       = Vec3(0.5f, 0.5f, 0.0f);          // DAT_0016c9b0 = 0.0
            mc->m_Timer     = kMC[i].rot;                       // field_0x2c (rotation)
            mc->m_AnimState = i;                                // stored before tmp++ in binary
            mc->m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;       // field_0x34 = 1 (configured flag)
            // size = (16, 16, 16) * scale per binary @ 0x0016c75a
            // (DAT_0016c9b8 = 64.0f * 0.5 * 0.5 = 16; per-row scale from
            // 4th column of the table). Rendered as 32x32*scale quad.
            const float s = 16.0f * kMC[i].scale;
            mc->size = Vec3(s, s, s);
            // DIFFERS: bind m_Texture eagerly to hud_cross.tex here so
            // MissControl::Draw doesn't early-return. Binary's exact bind
            // path (likely inside ctor/Init pulling from a static slot
            // table) hasn't been pinned down; this matches the visual
            // outcome.
            mc->m_Texture = MissControl::GetCrossTexture();
            game->hud->AddControl(mc);
        }
    }
    // step 3b: 12-entry pool (MissControl::CreatePool(0xc, hud)).
    MissControl::AllocatePool();

    // step 4: ScoreControl (size 0x100) + AddControl
    ScoreControl* sc = new ScoreControl();
    game->hud->AddControl(sc);

    // step 5: CoinCounter (size 0xD4) + AddControl -> game.pCoinCounter (Game+0x178)
    CoinCounter* cc = new CoinCounter();
    game->hud->AddControl(cc);
    game->pCoinCounter = cc;

    // step 6: TimeControl (size 0x108) + CountDown(90.9f) + AddControl -> game.pTimeCtrl (Game+0x180)
    // DAT_0016c9cc = 90.9 (Zen mode initial countdown time)
    TimeControl* tc = new TimeControl();
    tc->CountDown(90.9f);
    game->pTimeCtrl = tc;
    game->hud->AddControl(tc);

    // step 7: Background load (gb_game.tex / gb_game_sml.tex IsFastHardware branch)
    // Binary GameInit @ 0x..: same call shape (ChangeBackground(NULL) -> default "gb_game" + suffix).
    if (GetCurrentBackground() == nullptr) {
        ChangeBackground(nullptr);
    }

    // step 8: MeshManager loads (0x0016c97c..0x0016c9a8)
    // Binary: MeshManager::Load x2, results -> g_TaskState +0xbc / +0xc0.
    {
        GameTaskState* ts = GetTaskState();
        Mortar::MeshManager* mm = Mortar::MeshManager::GetInstance();
        if (mm) {
            ts->sliceFxMesh     = mm->Load("models/fruit/slice_fx.mmd");
            ts->sliceFxCritMesh = mm->Load("models/fruit/slice_fx_crit.mmd");
        }
    }

    // step 9: SliceEffect list/pool init (0x0016c9a8..0x0016ca90)
    // Binary: List<SliceEffect> ctor + MemoryPool::Create(100).
    // Results -> g_TaskState +0x64 / +0xc8.
    FN::SliceEffect_CreateList(100);

    // step 10: Flag init (0x0016ca8e..0x0016caa8)
    // Binary stores: g_TaskState+0x114 = g_GameData+0x54 (copy),
    //   +0x112 = 1 (re-entry guard), +0x111 = 0, +0x0c = 0 (first frame).
    {
        GameTaskState* ts = GetTaskState();
        ts->pAppState_x54 = nullptr;  // g_GameData+0x54 -- TODO: map this field (RE gap, step 10)
        ts->initComplete  = true;     // +0x112: prevents re-running GameInit
        ts->field_0x111   = false;    // +0x111: semantics TBD (RE gap, step 10)
        ts->firstFrame    = false;    // +0x0c: "first frame after init" flag
    }

    // step 11: MainScreen allocation
    MainScreen* mainScreen = new MainScreen(*game);
    game->mainScreen = mainScreen;
    // step 12: PauseScreen allocation (0x0016cad8..0x0016caf8)
    // Binary: operator new(0xd8), PauseScreen::PauseScreen, vtable[2] (Init).
    // Stored at g_TaskState +0x04.
    {
        PauseScreen* pauseScreen = new PauseScreen();
        pauseScreen->Init();
        GetTaskState()->pPauseScreen = pauseScreen;
    }

    // step 13: TutorialControl allocation (0x0016caf8..0x0016cb1e)
    // Binary: operator new(0xa0), TutorialControl::TutorialControl, vtable[2] (Init).
    // Also sets g_GameData+0x05 = 1 and g_GameData+0x0c = -1.0f.
    // Binary unconditionally re-allocs g_GameData+0x168, leaking the previous ptr.
    // Port matches fidelity: delete existing and re-alloc here.
    // DIFFERS: port deletes the previous ptr; binary leaks it (no free before re-alloc).
    if (game->pTutorialCtrl) {
        delete game->pTutorialCtrl;
        game->pTutorialCtrl = nullptr;
    }
    {
        TutorialControl* tc = new TutorialControl();
        tc->Init();
        game->pTutorialCtrl = tc;
        game->pauseFlag      = 1;      // g_GameData+0x05 = 1
        game->m_TransitionTimer = -1.0f; // g_GameData+0x0c = -1.0f
    }

    // step 14: AddControl x3 batch (MainScreen + PauseScreen + TutorialControl)
    // Binary: HUD::AddControl x3 in order: MainScreen, PauseScreen, TutorialControl.
    game->hud->AddControl(mainScreen);
    game->hud->AddControl(GetTaskState()->pPauseScreen);
    game->hud->AddControl(game->pTutorialCtrl);

    // step 15: Mortar::Entity::HeapCreate (0x0016cb48..0x0016cb4e)
    // Binary: Mortar::Entity::HeapCreate(0x20000) @ 0x000fd500 (PLT thunk).
    // 0x20000 = 128 KB Mortar::Entity LinkedHeap arena; must run before step 16.
    // DIFFERS: port stub is a no-op (std new, no fixed arena). See Entity.h.
    Mortar::Entity::HeapCreate(0x20000);

    // step 16: Mortar::ActorManager full init (0x0016cb50..0x0016cc06)
    // Binary: 16a Initialise(5, 0x2000), 16b RegisterFactory(delegate),
    //         16c RegisterHashConverter(delegate).
    // Factory + hash delegates reference GOT slots [0x0016ccb4..0x0016ccc0] --
    // RE-gap: addresses not yet resolved. Pass nullptr stubs for now.
    // NOTE: Mortar::ActorManager::Initialise already called in GameInitialise; binary
    // calls it again here (per-session reset). Matching binary call order.
    {
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        // 16a: Mortar::ActorManager::Initialise @ 0x000f7d04 (PLT thunk)
        am->Initialise(5, 0x2000);
        // 16b: Mortar::ActorManager::RegisterFactory @ 0x00107c34 (PLT thunk)
        // CreateEntity @ 0x0017421c — maps entityType int -> new Fruit/Bomb/etc.
        am->RegisterFactory(&CreateEntity);
        // 16c: Mortar::ActorManager::RegisterHashConverter @ 0x001069f8 (PLT thunk)
        // HashTypeConvert @ 0x0017414c maps "fruit"/"bomb"/etc StringHash -> entityType.
        // Only consumer is LoadEntity (unported level deserialisation; dead in live port).
        // Keep nullptr until LoadEntity is ported.
        am->RegisterHashConverter(nullptr);
    }

    // step 17: WaveManager::Init()
    WaveManager::GetInstance()->Init();

    // step 18: GameTaskInitInput() (0x0016cc0a..0x0016cc0e)
    // Binary: single call to GameTaskInitInput() @ 0x00169670 (357 lines).
    // Sets up 16 rotated touch regions and global input callbacks.
    GameTaskInitInput();

    // step 19: 30x prespawn loop (0x0016cc0e..0x0016cc4e)
    // Binary: do/while x30 — three Mortar::ActorManager::Add calls per iteration
    //   (entityType 0=Fruit, 1=Bomb, 4=Splat), then flags |= 0x11.
    // flags |= 0x11 = ENT_INACTIVE(0x01) | ENT_KILLED(0x10) = pre-spawned pool slot.
    {
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        for (int i = 0; i < 30; ++i) {
            // Mortar::ActorManager::Add @ 0x00108084 (PLT thunk -> PTR_Add_001f2f7c)
            if (Mortar::Entity* e0 = am->Add(0, true)) e0->flags |= 0x11;
            if (Mortar::Entity* e1 = am->Add(1, true)) e1->flags |= 0x11;
            if (Mortar::Entity* e4 = am->Add(4, true)) e4->flags |= 0x11;
        }
    }

    // step 20: SplatEntity::CreatePool(0x80) (0x0016cc52..0x0016cc56)
    // Binary: SplatEntity::CreatePool(128) @ 0x001042a4 (PLT thunk -> 0x0017ef34).
    // Allocates 128 SplatEntity slots (0xf drops per splat).
    SplatEntity::CreatePool(0x80);

    // step 21: WaveManager::Resume() — MUST come AFTER prespawn + SplatEntity pool
    // Binary: WaveManager::Resume (0x00124b1c) — restores wave state from save.
    WaveManager::GetInstance()->Resume();

    // step 22: BombFlash::CreatePool(0x20)
    BombFlash::CreatePool(0x20);

    // step 23: SoundManager::Initialise + SetSFXVolume (0x0016cc64..0x0016cc94)
    // Binary: SoundManager::Initialise(basePath) then SetSFXVolume based on Game+0x44.
    //   if (m_bSoundOn == 0) volume = 0.0f  else volume = 0.5f
    // basePath in binary = "Sound/Win32Project/Win/FruitNinja" (0x001BC978).
    // DIFFERS: Bada path replaced with port assets path. See SoundManager::Initialise stub.
    {
        Mortar::SoundManager& sm = Mortar::SoundManager::GetInstance();
        sm.Initialise("assets/sound");  // DIFFERS: original = "Sound/Win32Project/Win/FruitNinja"
        const float sfxVol = game->m_bSoundOn ? 0.5f : 0.0f;
        sm.SetSFXVolume(sfxVol);
    }
    // Per-finger SlashEntity array (binary SlashEntity[16] @ BSS, registered
    // by GameTaskInitInput @ 0x00169670). Each instance handles one of 16
    // SDL fingers / Bada touch slots and registers its own per-finger
    // TouchDown_n / TouchMove_*n / TouchUp_n callbacks on InputManager.
    for (int i = 0; i < 16; ++i) {
        if (!g_pSlashEntities[i]) {
            g_pSlashEntities[i] = new SlashEntity();
            g_pSlashEntities[i]->Init(i);
        }
    }
    g_pSlashEntity = g_pSlashEntities[0];  // backward-compat alias
}

// Matches GameUpdate (0x16bed0, 359 lines) — main gameplay loop
void GameUpdate(float dt, bool active) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // === Splash phase (g_TaskState +0x1C, init = 1.5f) ===
    // Binary @ 0x0016BED0: gated on splashFadeTimer > 0.
    GameTaskState* splashTs = GetTaskState();
    if (splashTs->splashFadeTimer > 0.0f) {
        if (!game->pSplashTex) {
            game->pSplashTex = Mortar::TextureManager::LoadLocalisedTexture("HB_logo.tex");
        }
        game->dt = 0.0f;
        splashTs->splashFadeTimer -= dt * 2.0f;
        if (splashTs->splashFadeTimer <= 0.0f) {
            splashTs->splashFadeTimer = 0.0f;
            game->pSplashTex.SetNull();
        }
    } else {
        Mortar::Touch::GetInstance().Update(0.0f);  // dt=0 drains all pending events
    }

    const float prevBombTimer = game->bombHitTimer;
    if (game->bombHitTimer > 0.0f) {
        game->bombHitTimer -= dt;
        if (game->bombHitTimer < 0.0f) game->bombHitTimer = 0.0f;
    }
    FN::UpdateBombHit(prevBombTimer);

    // Binary 0x0016c284: bombHitTimer crossing 1.5 downward triggers GameOver.
    if (prevBombTimer > 1.5f && game->bombHitTimer <= 1.5f && !game->pauseFlag) {
        FN::GameOver(-1, -1.0f, -1);
    }

    if (active && game->actorManager)
        game->actorManager->Update(dt);

    if (active) WaveManager::GetInstance()->Update(dt);

    if (game->pSaveData) game->pSaveData->Update(dt, game->hud);

    if (game->pGameSound) game->pGameSound->Update(dt);
    UpdateMusic(dt);

    PSPParticleManager::GetInstance().Update(dt, false);

    FN::UpdateCriticalFlash(dt);

    if (active) SplatEntity::UpdateActiveSplats(dt);
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) g_pSlashEntities[i]->Update(dt);
    }
    if (game->hud) game->hud->Update(dt);
    if (game->pCamera) game->pCamera->UpdateCamera(dt);
}

// Matches GameDraw (0x16b888, 211 lines) — full render frame.
//
// Binary draw order (verified from decompile, see comments inline):
//   1.  Camera + background quad
//   2.  Mortar::ActorManager::Draw (3D entities — fruit, bomb, SlashEntity)
//   3.  HUD::BeginDraw
//   4.  HUD::Draw(0x40)
//   5.  SplatEntity::DrawActiveSplats / Fruit::DrawShadows /
//       SlashEntity::PreDraw / BombBlast::DrawActiveBlasts /
//       BombFlash::DrawActiveFlashes            [not yet ported]
//   6.  HUD::Draw(0x80)
//   7.  pm.Draw(-1)   — "background" particles (useDepth=-1, earliest)
//   8.  (vtable loop over 16 objects)           [not yet ported]
//   9.  pm.Draw(0)    — "mid" particles
//   10. DrawSlices    — SlashEntity::DrawSlice blade ribbon
//   11. HUD::Draw(0x01) — MainScreen (logo + shade). Drawn AFTER slash
//       so logo appears in front of the blade.
//   12. pm.Draw(1)    — "foreground" particles (drawn over logo,
//       under buttons)
//   13. WaveManager::Draw                       [not yet ported]
//   14. HUD::Draw(0x08) — buttons
//   15. HUD::Draw(0x100) + DrawBombHit + HUD::Draw(0x200)
//   16. HUD::Draw(0x400)
//
// Key insight: the particle layer indices map differently from what the
// layer names imply — pm.Draw(-1) actually draws EARLIEST (background)
// and pm.Draw(1) draws LATER (foreground, over logo).
void GameDraw(float dt, bool active) {
    Game* game = Game::GetInstance();
    if (!game) return;

    GameTaskState* ts = GetTaskState();
    // 1. Camera projection
    if (game->pCamera)
        game->pCamera->SetupPerspective(PT_STANDARD, false);

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
        game->renderer.DrawQuad(white, 0.03125f, 0.1875f, 0.96875f, 0.8125f);

        bgTex->UnSet();
    }

    PSPParticleManager& pm = PSPParticleManager::GetInstance();
    Mortar::DisplayManager& dm = Mortar::DisplayManager::GetInstance();

    // === 1. Mortar::ActorManager::Draw — 3D fruit/bomb/slash entities ===
    // Binary @ 0x0016ba10: SetDepthBufferWrite(1) + SetDepthBuffer(1)
    // just before Mortar::ActorManager::Draw. Depth func stays at GL_LESS set
    // by BeginFrame — binary never overrides it.
    dm.SetDepthBufferWrite(true);
    dm.SetDepthBuffer(true);
    // Port specific: wireframe debug mode (F2). glPolygonMode is loaded
    // optionally by gl_funcs — stays nullptr on GLES, so the toggle is a
    // silent no-op there.
    const bool wireframe = FN::g_DebugWireframe && glPolygonMode != nullptr;
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    if (game->actorManager)
        game->actorManager->Draw(game->renderer);
    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // === 2. HUD::BeginDraw + post-actor block ===
    // Binary @ 0x0016ba10 right after Mortar::ActorManager::Draw:
    //   SetDepthBuffer(1)       — depth test still ON
    //   SetDepthBufferWrite(0)  — writes OFF for HUD/splats/bomb blasts
    dm.SetDepthBuffer(true);
    dm.SetDepthBufferWrite(false);
    if (game->hud) {
        game->hud->BeginDraw(dt);

        // 2a. HUD::Draw(0x40) — menu button sprites @ 0x0016ba5a
        game->hud->Draw(Mortar::HUD_LAYER_MENU_BG);

        // 2b. SplatEntity::DrawActiveSplats @ 0x0016ba6a
        SplatEntity::DrawActiveSplats();

        // 2c. Fruit::DrawShadows @ 0x0016ba6e
        Fruit::DrawShadows();

        // 2d. SlashEntity::PreDraw @ 0x0016ba84 — blade pre-pass for each
        //     of 16 finger slots (binary loops over SlashEntity[16]).
        for (int i = 0; i < 16; ++i) {
            if (g_pSlashEntities[i]) g_pSlashEntities[i]->PreDraw();
        }

        // 2e. BombBlast::DrawActiveBlasts @ 0x0016ba88 — drawn HERE in
        //     the binary, NOT inside the 0x200 layer. Shockwave rings
        //     belong to this post-actor block.
        BombBlast::DrawActiveBlasts();

        // 2f. BombFlash::DrawActiveFlashes @ 0x0016baf0
        BombFlash::DrawActiveFlashes();

        // 2g. HUD::Draw(0x80) — DojoScreen / AboutScreen @ 0x0016baf8
        game->hud->Draw(Mortar::HUD_LAYER_POST_ACTOR);
    }

    // === 3. Background particles ===
    // Binary pm.Draw(-1) @ 0x0016bb02 — drawn BEHIND the logo/shade.
    pm.Draw(0.0f, false, -1);

    // Binary @ 0x0016ba10 after pm.Draw(-1): SetDepthBuffer(0) turns
    // depth test off before the SlashEntity loop ×16 and all later
    // 2D passes.
    dm.SetDepthBuffer(false);
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) g_pSlashEntities[i]->Draw();
    }

    // === 4. Mid particles + slice lines + main-screen logo ===
    // Binary pm.Draw(0) @ 0x0016bb4a
    pm.Draw(0.0f, false, 0);

    // DrawSlices @ 0x0016bb52 — slash-line pool
    FN::SliceEffect_Draw(dt);

    // HUD::Draw(0x01) — MainScreen logo / shade @ 0x0016bb5a
    if (game->hud) game->hud->Draw(Mortar::HUD_LAYER_DEFAULT);

    // pm.Draw(1) — foreground particles @ 0x0016bb6a
    pm.Draw(0.0f, false, 1);

    // WaveManager::Draw(0) @ 0x0016bb98 — stubbed (wave-banner overlay).
    WaveManager::GetInstance()->Draw(0);

    // === 5. HUD overlay layers + flash effects ===
    if (game->hud) {
        // HUD::Draw(0x08) — buttons @ 0x0016bba8
        game->hud->Draw(Mortar::HUD_LAYER_BUTTONS);

        // MainScreen::DrawPostEffects @ 0x0016bbb0
        if (game->mainScreen) game->mainScreen->DrawPostEffects();

        // DrawCritHit (CriticalFlash) @ 0x0016bbd2 — gated on
        // critFlash > 0 && IsFastHardware. Port has CriticalFlash
        // implemented as FN::DrawCriticalFlash.
        FN::DrawCriticalFlash();

        // HUD::Draw(0x100) — overlays @ 0x0016bbde
        game->hud->Draw(Mortar::HUD_LAYER_P2_SCORE);

        // DrawBombHit @ 0x0016bbe6 — bomb-hit white flash, gated on
        // bombFlash > 0
        FN::DrawBombHit();

        // HUD::Draw(0x200) — bomb-hit overlay layer @ 0x0016bbec
        game->hud->Draw(Mortar::HUD_LAYER_SLIDER);

        // DrawNews / DrawStartFade @ 0x0016bbf0..0x0016bc12
        FN::DrawNews();
        FN::DrawStartFade();

        // Debug overlay — fruit/bomb hitboxes (F1 toggle)
        FN::DebugHitbox_Draw();

        // HUD::Draw(0x400) — top layer @ 0x0016bd7c, ALWAYS fires
        // (binary places it OUTSIDE the `active` block).
        game->hud->Draw(Mortar::HUD_LAYER_FADE_MODAL);
    }
}

// Matches GameExit (0x16cf74, 98 lines) — per-session cleanup
void GameExit_Handler() {
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("GameExit: cleaning up\n");

    // Release background texture (shared MenuBackground slot)
    UnloadBackground();
    // Drop any leftover GameTaskState slot too (no-op if never used)
    GameTaskState* ts = GetTaskState();
    ts->pBackgroundTexture.SetNull();

    // Release per-finger SlashEntity[16] array + input callbacks
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) {
            delete g_pSlashEntities[i];
            g_pSlashEntities[i] = nullptr;
        }
    }
    g_pSlashEntity = nullptr;
    if (Mortar::InputManager* im = Mortar::InputManager::GetInstance()) {
        // Binary: InputManager::Destroy (0x001968a0) clears all device callbacks.
        im->Destroy();
        im->Init(0);
    }

    // Release HUD (destroys all controls including MainScreen)
    if (game->hud) {
        game->hud->Release();
        delete game->hud;
        game->hud = nullptr;
    }
    game->mainScreen = nullptr;

    Coin::ClearCoins(false);
    FruitNinja_SaveCurrentData();  // stub (writes FruitSaveData XML in binary)
    WaveManager::GetInstance()->Destroy();  // stub (frees WAVE_INFO/WaveQue)
    PSPParticleManager::GetInstance().ClearEmitters();
    if (Mortar::ActorManager* am = Mortar::ActorManager::GetInstance()) {
        am->Clear();
        am->Destroy();
    }
    Mortar::Entity::HeapDestroy();
}
