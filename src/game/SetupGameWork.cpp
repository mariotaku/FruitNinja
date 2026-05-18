// Analysed: 2026-05-03T00:00
// SetupGameWork -- binary @ 0x0010b4e8.

#include "game/SetupGameWork.h"
#include "Game.h"
#include "game/FruitSaveData.h"
#include "util/StringHash.h"

// ASM-verified: 2026-05-03 binary @ 0x0010b4e8 (re-analyst)

// Binary @ 0x0010b4e8: 23 field stores + plays_total bump.
void SetupGameWork() {
    Game* app = Game::GetInstance();
    FruitSaveData* save = app->pSaveData;

    // Bump cumulative play count.
    save->AddToTotal("plays_total", StringHash("plays_total"), 1, true, true);

    // 23 field stores (binary @ 0x0010b4e8 disasm-confirmed):

    // +0x000: set task state to 2 (Game state; binary field is "m_GameMode" in spec,
    //         maps to taskStateIndex in port which is 0=Splash/1=Frontend/2=Game).
    app->taskStateIndex = 2;

    // +0x006: retryFlag = 0.
    app->retryFlag = 0;

    // +0x010: bombHitTimer = 0.0f.
    app->bombHitTimer = 0.0f;

    // +0x18C: m_gameDataLicensedState = 0.
    app->m_gameDataLicensedState = 0;

    // +0x1AC: m_AchievementProgressTimer = 0.0f (binary @ 0x0010b522).
    app->m_AchievementProgressTimer = 0.0f;

    // +0x01C: m_bUnsullied = 0.
    app->m_bUnsullied = 0;

    // +0x020..+0x028: zero the coin trio. Binary writes 3 separate `str`
    // (int) ops at 0x0010b568..0x0010b56c -- confirmed int via re-analyst
    // (vstr would be used for floats, see neighbouring +0x10/+0x2c writes).
    app->m_CoinsBalance     = 0;
    app->m_CoinsTotalEarned = 0;
    app->m_CoinsAtGameStart = 0;

    // +0x02C: m_CritTimer = 0.0f.
    app->m_CritTimer = 0.0f;

    // +0x030: m_ScoreThreshold = save->m_CriticalChance.
    // Binary spec label: "m_CritChanceCounter"; port field at +0x30 is m_ScoreThreshold.
    // DIFFERS: binary stores FruitSaveData[+0x110] (m_CriticalChance) here; port
    //          field name m_ScoreThreshold is provisional -- rename when RE confirms.
    app->m_ScoreThreshold = save->m_CriticalChance;

    // +0x034: field_0x34 = 0.
    app->field_0x34 = 0;

    // +0x088: field_0x88 = 50.0f.
    // DAT_0010b578 confirmed = 0x42480000 = 50.0f.
    app->field_0x88 = 50.0f;

    // +0x160: mainScreen = 0 (clear pointer).
    app->mainScreen = nullptr;

    // +0x164: pGameOverScreen = 0 (clear pointer).
    app->pGameOverScreen = nullptr;

    // +0x16C: field_0x16c = 0 (binary @ 0x0010b55c).
    app->field_0x16c = 0;

    // +0x170: m_bMPRetryPending = 0 (strb, binary @ 0x0010b550).
    app->m_bMPRetryPending = 0;

    // +0x198: field_0x198 = 0 (binary @ 0x0010b554).
    app->field_0x198 = 0;

    // +0x199: field_0x199 = 0 (dead-code MP sync flag; binary @ 0x0010b53e).
    app->field_0x199 = 0;

    // +0x19E: field_0x19e = 0 (binary @ 0x0010b558).
    app->field_0x19e = 0;

    // +0x1A0: m_MenuReturnTimer = 0.0f.
    app->m_MenuReturnTimer = 0.0f;

    // +0x1A8: flag_0x1a8 = 0.
    app->flag_0x1a8 = 0;

    // +0x1B0: field_0x1b0 = 0 (binary @ 0x0010b56e).
    app->field_0x1b0 = 0;
}
