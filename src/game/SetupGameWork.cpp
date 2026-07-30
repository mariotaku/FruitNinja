// SetupGameWork -- binary @ 0x0011c06c (0x0010ed34 is a PLT/GOT thunk to it).

#include "game/SetupGameWork.h"
#include "Game.h"
#include "game/FruitSaveData.h"
#include "util/StringHash.h"
#include "game/GameWork.h"

// ASM-spec v1.6.1 SetupGameWork @ 0x0011c06c: FruitSaveData::AddToTotal("sessions", 1)
// followed by exactly 24 field stores into game_work. Store order below is per
// store class, as emitted (the three classes interleave in the encoding; only the
// within-class order is pinned, so the body groups by field instead of guessing it):
//   strb: +0x00=2, +0x1a1, +0x34, +0x1c, +0x06, +0x174, +0x1a0, +0x1a6,
//         +0x1bc, +0x1b4, +0x194=1
//   str : +0x190, +0x30=save[+0x110], +0x170, +0x168, +0x164, +0x20, +0x24, +0x28
//   vstr: +0x8c=50.0f, +0x10, +0x2c, +0x1a8, +0x1b8
// Notably NOT stored by v1.6.1: +0x198..+0x19b (dead bytes, no xrefs at all),
// +0x1a2..+0x1a5 (zeroed by QuitToMenu @0x001cb764 instead) and +0x1b0
// (m_UpsideDownTimer -- only UpdateUpsideDown @0x0011a1ac/@0x0011a1c8 touches it).
void SetupGameWork() {
    Game* app = Game::GetInstance();
    FruitSaveData* save = game_work.m_SaveData;

    // Bump cumulative session count (key confirmed via decompile: "sessions", #335).
    save->AddToTotal("sessions", StringHash("sessions"), 1, true, true);

    // +0x000: set task state to 2 (Game state; binary field is "m_GameMode" in spec,
    //         maps to taskStateIndex in port which is 0=Splash/1=Frontend/2=Game).
    game_work.taskStateIndex = 2;

    // +0x006: retryFlag = 0.
    game_work.retryFlag = 0;

    // +0x010: bombHitTimer = 0.0f.
    game_work.m_BombHitTimer = 0.0f;

    // +0x01C: m_bUnsullied = 0.
    game_work.m_bUnsullied = 0;

    // +0x020..+0x028: zero the coin trio. Binary writes 3 separate `str`
    // (int) ops -- confirmed int via re-analyst (vstr would be used for floats,
    // see neighbouring +0x10/+0x2c writes).
    game_work.m_CoinsBalance     = 0;
    game_work.m_CoinsTotalEarned = 0;
    game_work.m_CoinsAtGameStart = 0;

    // +0x02C: m_CritTimer = 0.0f.
    game_work.m_CritTimer = 0.0f;

    // +0x030: m_ScoreThreshold = save->m_CriticalChance.
    // Binary spec label: "m_CritChanceCounter"; port field at +0x30 is m_ScoreThreshold.
    // DIFFERS: binary stores FruitSaveData[+0x110] (m_CriticalChance) here; port
    //          field name m_ScoreThreshold is provisional -- rename when RE confirms.
    game_work.m_ScoreThreshold = save->m_CriticalChance;

    // +0x034: m_bHudDestructing = 0 (HUD-teardown guard).
    game_work.m_bHudDestructing = 0;

    // +0x08C: flM_BombSize = 50.0f.
    // Literal pool DAT_0011c130 confirmed = 0x42480000 = 50.0f.
    game_work.flM_BombSize = 50.0f;

    // +0x164: mMainScreen = 0 (clear pointer).
    game_work.mMainScreen = nullptr;

    // +0x168: pGameOverScreen = 0 (clear pointer).
    game_work.pGameOverScreen = nullptr;

    // +0x170: m_pActiveHUDControl = 0.
    game_work.m_pActiveHUDControl = nullptr;

    // +0x174: m_bMPRetryPending = 0.
    game_work.m_bMPRetryPending = 0;

    // +0x190: m_gameDataLicensedState = 0.
    game_work.m_gameDataLicensedState = 0;

    // +0x194: m_reserved194 = 1 (written-never-read; the paired +0x195 suspend guard
    //         is NOT touched here).
    game_work.m_reserved194 = 1;

    // +0x19E was field_0x19e in v1.5.1; in v1.6.1 this byte is interior to
    // m_FrameTimer (int @ +0x19C); the explicit zero is dropped (m_FrameTimer
    // is not set here; zero-init from .bss suffices).

    // +0x1A0: m_bP2PConnecting = 0 (Defunct: online P2P -- read by IsP2PConnecting).
    game_work.m_bP2PConnecting = 0;

    // +0x1A1: m_bP2POpponentReady = 0 (Defunct: online P2P -- but read every frame by
    //         TimeControl::Update and WaveManager::Update).
    game_work.m_bP2POpponentReady = 0;

    // +0x1A6: m_reserved1a6 = 0. SetupGameWork is this byte's ONLY xref in v1.6.1
    //         (write-never-read) -- kept because it is a real store.
    game_work.m_reserved1a6 = 0;

    // +0x1A8: m_QuitTransitionTimer = 0.0f.
    game_work.m_QuitTransitionTimer = 0.0f;

    // +0x1B4: m_bUpsideDownActive = 0.
    game_work.m_bUpsideDownActive = 0;

    // +0x1B8: m_ElapsedGameTime = 0.0f.
    game_work.m_ElapsedGameTime = 0.0f;

    // +0x1BC: m_reserved1bc = 0 (written-never-read).
    game_work.m_reserved1bc = 0;
}
