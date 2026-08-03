// GameOver — v1.6.1 GameOver @ 0x001cb788

#include "GameOver.h"
#include "Game.h"
#include "WaveManager.h"
#include "FruitSaveData.h"
#include "screens/GameOverScreen.h"
#include "hud/HUD.h"
#include "engine/util/StringHash.h"
#include "ScoreDelegate.h"
#include "PowerUpManager.h"
#include "entities/Fruit.h"
#include "audio/GameSound.h"   // GameSound::SFXPlay (extra-life restore)

#include <algorithm>
#include <ctime>
#include <cstdio>     // snprintf -- explicit for Sourcery 4.4 newlib
#include "game/GameWork.h"

#if defined(FN_BLOCK_PRELOAD)
#include "resource/ResBlock.h"
#endif

// v1.6.1 GameOver @ 0x001cb788
void GameOver(int endReason, float endScore, int endParam) {
    // ASM-spec v1.6.1 GameOver @0x001cb788: entry loads game_work straight from the
    // GOT (ldr r7,[r4,r3]) and reads [r7,#0x5]. No Game::GetInstance call, no null
    // test -- and m_SaveData at [r7,#0x50] is dereferenced unguarded too.

    // re-entry guard: levelTransitionFlag at g_GameData+0x05
    if (game_work.bM_bPaused != 0) return;

#if defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 1 -- block-enter hook (log-only labelling, see
    // tmp/wii/loader-blueprint.md section 2/7). ADDITIVE over INGAME (Risk
    // R4 -- the gameover screen draws over the frozen, still-resident
    // gameplay state; do NOT clear INGAME here).
    fn::wii::AddCurrentBlock(fn::wii::RES_BLOCK_GAMEOVER);
#endif

    game_work.bM_bPaused = 1;

    WaveManager::GetInstance()->ClearUnspawned();

    // FruitSaveData carries the sensei choice fields at +0x11C/0x120/0x124/0x128
    // (m_GameOverField1..4). Binary reads them to pick which sensei head/body
    // texture variant + per-game pom/star counts to display. Wiring proper:
    //   expressionIdx <- m_GameOverField2 (+0x120)
    //   bgPatternIdx  <- m_GameOverField1 (+0x11C)
    //   tabIndex      <- m_GameOverField3 (+0x124)
    //   starCount     <- m_GameOverField4 (+0x128)
    // The fields default to -1 (sentinel) and are written by the gameplay
    // achievement / bonus path which the port hasn't fully RE'd yet.
    // DIFFERS: when a field == -1 we substitute 1 (the first valid texture
    // variant) so sensei body + head are visible. Once the gameplay-side
    // setters land, the substitution can come out.
    // v1.6.1 GameOver @0x001cb788: `ldr r3,[r7,#0x50]` @0x001cb7d0 is followed straight
    // by `ldr r2,[r3,#0x11c]` @0x001cb7dc, then +0x124, +0x128, +0x120 -- no cmp, no
    // branch. The only gate in the head is the one-shot re-entry flag
    // (`ldrb r3,[r7,#0x5] ; cmp #0 ; bne 0x001cb9a0` @0x001cb7ac).
    FruitSaveData* save = game_work.m_SaveData;
    // Substitute 1 when the gameplay-side setter hasn't written a real value
    // (sentinel -1). Inlined per-field instead of a helper lambda -- the
    // cross-toolchain (GCC 4.4.1) doesn't support C++11 lambdas.
    int expressionIdx = (save->m_GameOverField2 > 0) ? save->m_GameOverField2 : 1;
    int bgPatternIdx  = (save->m_GameOverField1 > 0) ? save->m_GameOverField1 : 1;
    // ASM-spec v1.6.1 GameOver @0x001cb788: +0x124/+0x128 are passed RAW.
    // Every v1.6.1 writer stores -1 (FruitSaveData ctors, GameOverScreen::Release,
    // GameOver @0x001cb83c, EndRetryLevel @0x001cbc70) — dead override fields —
    // so the -1 sentinel must reach FruitFactControl/Fruit::GetFact, which uses
    // it to draw a fresh random fruit fact. Clamping to 0 pinned the fact to
    // fruit 0 every game-over.
    int tabIndex      = save->m_GameOverField3;
    int starCount     = save->m_GameOverField4;

    GameOverScreen* gos = new GameOverScreen(
        "GameOver", endReason, endScore,
        expressionIdx, bgPatternIdx, tabIndex, starCount);

    // +0x164: pGameOverScreen
    game_work.pGameOverScreen = gos;

    // TODO: FruitSaveData::AddToTotal("GamesPlayed-...") + unique-day tracking
    // if (endReason == -1) { ... }

    // sd[0x120] = sd[0x128] = sd[0x124] = sd[0x11C] = -1 (clear after passing to ctor)
    // TODO: clear FruitSaveData fields when ported

    gos->Init();
    game_work.mHud->AddControl(gos);

    // v1.6.1 GameOver @0x001cb788, day block 0x001cb84c-0x001cb954: bump the per-mode
    // day stat and write m_LastPlayedDay[mode]. +0x50 is deref'd raw at 0x001cb7d0,
    // 0x001cb830, 0x001cb86c, 0x001cb8cc, 0x001cb904 and 0x001cb948 -- there is no cmp
    // on it anywhere in the function. The real gate is `endReason == -1`
    // (`cmp r6,r2 ; bne` @0x001cb828/0x001cb848).
    // TODO: v1.6.1 0x001cb84c (GameOver) -- port the day block faithfully. Gaps:
    //   (1) the missing `endReason == -1` gate above this block;
    //   (2) the key is GetModeName(gameMode) + "_days", not "<MODE>_today";
    //   (3) the binary branches 3 ways -- AddToTotal(key,1,true,true) when
    //       m_LastPlayedDay[mode] == today-1 || GetTotal(key) == 0, else
    //       SetTotal(key,1,true,true) when m_LastPlayedDay[mode] != today, else nothing;
    //   (4) missing "games_at_grave_yard_time" bump (MortarDate::CurrentDate(&d,1);
    //       if (d.hour - 2u < 3) AddToTotal(...,1,true,true));
    //   (5) the binary runs gos->Init() + HUD::AddControl AFTER this block
    //       (0x001cb958-0x001cb97c); the port runs them before.
    {
        static const char* k_ModeNames[4] = { "CLASSIC", "CASINO", "ARCADE", "ZEN" };
        int mode = (int)game_work.gameMode;
        if (mode >= 0 && mode < 4) {
            // Compute days-since-1900 inline (same helper as FruitSaveData.cpp).
            static const int DAYS_FROM_1900_TO_EPOCH = 25569;
            int today = (int)(time(nullptr) / 86400) + DAYS_FROM_1900_TO_EPOCH;

            // Build "<MODE>_today" key and hash it.
            char todayKey[32];
            std::snprintf(todayKey, sizeof(todayKey), "%s_today", k_ModeNames[mode]);
            uint32_t todayHash = StringHash(todayKey);

            game_work.m_SaveData->AddToTotal(todayKey, todayHash, 1, false, false);
            game_work.m_SaveData->m_LastPlayedDay[mode] = today;
        }
    }
}

// ASM-spec v1.6.1 AddToCurrentScore @0x0011a4c0
// Binary order: GetScoreMultiplyer(0) @0x0011a4f0, g_ScoreDelegate call @0x0011a504,
// add to currentScore @0x0011a510, clamp-to-zero @0x0011a518/0x0011a520/0x0011a524.
// GetScoreMultiplyer returns PowerUpManager::GetScoreGainMultiplier() (default 1),
// so normal fruit gains are unchanged. DefaultScoreDelegate multiplies negative
// deltas by GetScoreLossMultiplier() so bomb penalty magnitude is delegate-controlled.
void AddToCurrentScore(int points, int param1, bool param2, bool /*param3*/) {
    // ASM-spec v1.6.1 AddToCurrentScore @0x0011a4c0: entry is
    // `ldr r7,[r4,r3]; ldr r11,[r7,#0x18]` -- game_work straight from the GOT,
    // no Game::GetInstance, no null test.
    int oldScore = game_work.currentScore;
    int mult  = PowerUpManager::GetInstance()->GetScoreGainMultiplier();
    int delta = g_ScoreDelegate(points * mult);
    game_work.currentScore += delta;
    if (game_work.currentScore < 0) game_work.currentScore = 0;
    // ASM-spec v1.6.1 AddToCurrentScore @0x0011a4c0: crossing a NEW_LIFE_AT(100) boundary restores one life.
    if (oldScore / Fruit::NEW_LIFE_AT < game_work.currentScore / Fruit::NEW_LIFE_AT && game_work.missCount > 0) {
        game_work.missCount--;
        // v1.6.1 AddToCurrentScore @0x0011a4c0: `ldr r7,[r3,#0x18c]` at 0x0011a570 feeds
        // SFXPlay directly; the only gate is `missCount != 0` at 0x0011a558.
        game_work.mGameSound->SFXPlay("extra-life", 1.0f, 1.0f);
    }
    // v1.6.1 AddToCurrentScore @0x0011a4c0: cache "all" cumulative count in
    // game_work for achievement gating in GameOverScreen::Update state-6. The only
    // gate is `points > 0 && param2 && param1 <= 1` @0x0011a658-0x0011a66c;
    // `ldr r0,[r6,#0x50]` @0x0011a698 feeds the first AddToTotal and 0x0011a6bc
    // reloads +0x50 for the second -- neither is tested.
    if (points > 0 && param2 && param1 < 2) {
        static const uint32_t s_allHash = StringHash("all");
        game_work.m_pLastScoredSaveEntry =
            (void*)(intptr_t)game_work.m_SaveData->AddToTotal(
                "all", s_allHash, points, true, false);
        if (game_work.m_bUpsideDownActive) {
            static const uint32_t s_upHash = StringHash("upside_down_points");
            game_work.m_SaveData->AddToTotal(
                "upside_down_points", s_upHash, points, false, true);
        }
    }
    // Defunct: P2P PointsPacket (param3 && param1==1) -- no-op stub; v1.6.1 AddToCurrentScore @0x0011a4c0
}

// Binary free functions @ 0x0011a0ec / 0x0011a12c.
// Defunct sig: playerIdx ignored (online MP scrubbed) — binary v1.6.1 SetScore @0x0011a0ec / v1.6.1 SetMissCount @0x0011a12c.
// ASM-spec v1.6.1 SetScore @ 0x0011a0ec. Writes score to Game+0x18 (the live
// `currentScore` that ScoreControl reads), NOT to pSaveData->m_CurrentScore
// (which earlier port had wrong -- game-start SetScore(0,-1) failed to reset the
// live score, so the previous run's final score persisted into the new game).
// The whole body is 6 instructions: load game_work via the GOT, `str r0,[r3,#0x18]`,
// `bx lr`. No Game::GetInstance call and no null test.
// (Downgraded from ASM-verified: the stamp survived a later port-side
//  `if (game) ...` guard being added, so it no longer described this body.)
void SetScore(int score, int /*playerIdx*/) {
    game_work.currentScore = score;
}

// ASM-spec v1.6.1 SetMissCount @ 0x0011a12c. Same 6-instruction shape as SetScore:
// load game_work via the GOT, `strb r0,[r3,#0x14]`, `bx lr`. No Game::GetInstance,
// no null test.
// (Downgraded from ASM-verified: the stamp survived a later port-side
//  `if (game) ...` guard being added, so it no longer described this body.)
// Binary writes `strb r0, [game_work + 0x14]` -- the LIVE missCount that
// MissControl::Update reads. Prior port wrote to m_SaveData->m_CurrentMissCount
// (FruitSaveData+0x68, the persisted snapshot) which is a DIFFERENT field.
// The bug allowed the X miss markers from a finished Arcade game to persist
// across game starts and on MainScreen indefinitely, because no reset path
// (WaveManager::Reset(true), WaveManager::Resume, etc.) was actually clearing
// the live counter. FruitSaveData snapshot writes already happen elsewhere
// (FruitSaveData.cpp:671 mirrors game_work.missCount on save).
void SetMissCount(int n, int /*playerIdx*/) {
    game_work.missCount = (uint8_t)n;
}

