// Analysed: 2026-05-03T00:00
// SetupGameWork -- binary @ 0x0010b4e8.

#include "game/SetupGameWork.h"
#include "Game.h"
#include "game/FruitSaveData.h"
#include "util/StringHash.h"
#include "game/GameWork.h"

// ASM-verified: 2026-05-03 binary @ 0x0010b4e8 (re-analyst)

// Binary @ 0x0010b4e8: 23 field stores + plays_total bump.
void SetupGameWork() {
    Game* app = Game::GetInstance();
    FruitSaveData* save = game_work.m_SaveData;

    // Bump cumulative play count.
    save->AddToTotal("plays_total", StringHash("plays_total"), 1, true, true);

    // 23 field stores (binary @ 0x0010b4e8 disasm-confirmed):

    // +0x000: set task state to 2 (Game state; binary field is "m_GameMode" in spec,
    //         maps to taskStateIndex in port which is 0=Splash/1=Frontend/2=Game).
    game_work.taskStateIndex = 2;

    // +0x006: retryFlag = 0.
    game_work.retryFlag = 0;

    // +0x010: bombHitTimer = 0.0f.
    game_work.m_BombHitTimer = 0.0f;

    // +0x18C: m_gameDataLicensedState = 0.
    game_work.m_gameDataLicensedState = 0;

    // +0x1B8: m_ElapsedGameTime = 0.0f (binary @ 0x0010b522).
    game_work.m_ElapsedGameTime = 0.0f;

    // +0x01C: m_bUnsullied = 0.
    game_work.m_bUnsullied = 0;

    // +0x020..+0x028: zero the coin trio. Binary writes 3 separate `str`
    // (int) ops at 0x0010b568..0x0010b56c -- confirmed int via re-analyst
    // (vstr would be used for floats, see neighbouring +0x10/+0x2c writes).
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

    // +0x034: field_0x34 = 0.
    game_work.field_0x34 = 0;

    // +0x08C: field_0x8c = 50.0f.
    // DAT_0010b578 confirmed = 0x42480000 = 50.0f.
    game_work.field_0x8c = 50.0f;

    // +0x160: mainScreen = 0 (clear pointer).
    game_work.mMainScreen = nullptr;

    // +0x164: pGameOverScreen = 0 (clear pointer).
    game_work.pGameOverScreen = nullptr;

    // +0x16C: m_pActiveHUDControl = 0 (binary @ 0x0010b55c).
    game_work.m_pActiveHUDControl = nullptr;

    // +0x170: m_bMPRetryPending = 0 (strb, binary @ 0x0010b550).
    game_work.m_bMPRetryPending = 0;

    // +0x198: m_bGameCenterConnecting = 0 (binary @ 0x0010b554).
    game_work.m_bGameCenterConnecting = 0;

    // +0x199: m_bP2PReady = 0 (dead-code MP sync flag; binary @ 0x0010b53e).
    game_work.m_bP2PReady = 0;

    // +0x19E was field_0x19e in v1.5.1; in v1.6.1 this byte is interior to
    // m_FrameTimer (int @ +0x19C); the explicit zero is dropped (m_FrameTimer
    // is not set here; zero-init from .bss suffices).

    // +0x194: field_0x194 = 1 (v1.6.1; binary init confirmed by SetupGameWork).
    game_work.field_0x194 = 1;

    // +0x1A2..+0x1A5: zero four byte fields (v1.6.1 SetupGameWork).
    game_work.field_0x1a2 = 0;
    game_work.field_0x1a3 = 0;
    game_work.field_0x1a4 = 0;
    game_work.field_0x1a5 = 0;

    // +0x1A8: m_QuitTransitionTimer = 0.0f.
    game_work.m_QuitTransitionTimer = 0.0f;

    // +0x1B0: field_0x1b0 = 0.0f (binary @ 0x0010b56e).
    game_work.field_0x1b0 = 0.0f;

    // +0x1B4: field_0x1b4 = 0 (v1.6.1; gate byte zeroed by SetupGameWork).
    game_work.field_0x1b4 = 0;

    // +0x1BC: field_0x1bc = 0 (v1.6.1; gate byte zeroed by SetupGameWork).
    game_work.field_0x1bc = 0;
}
