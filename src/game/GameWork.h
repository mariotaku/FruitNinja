#ifndef FN_GAME_GAMEWORK_H
#define FN_GAME_GAMEWORK_H

// game_work -- binary symbol at .bss 0x001f43b8 (size 0x608 / 1544 bytes).
// Confirmed real ELF symbol in .dynstr and .strtab. C-linkage, not C++ class member.
// Ghidra applied the type name `GameContext`; this port uses `GameWork` to match
// the actual binary symbol name (game_work).
//
// Field offsets below are relative to game_work (== g_GameData in the binary).
// NOT relative to the port's merged Game class, which adds sizeof(MortarGame) bytes
// before these fields.

#include <cstdint>
#include <cstddef>
#include "engine/math/Vec3.h"
#include "engine/render/Font.h"
#include "engine/util/SmartPtr.h"

class HUD;
class HUDControl;
class MainScreen;
class FruitCamera;
class FruitSaveData;
class GameOverScreen;
class TimeControl;
class GameSound;
class CoinCounter;
namespace Mortar { class ActorManager; }

struct GameWork {
    uint8_t taskStateIndex;        // +0x00: 0=Splash, 1=Frontend, 2=Game
    uint8_t field_0x01;            // +0x01
    bool    m_Paused;              // +0x02: user-pause flag (see Game.h for full xref list)
    uint8_t languageFlag;          // +0x03: SetLanguage writes 0 here
    uint8_t gameMode;              // +0x04: GAME_MODE enum stored as uint8_t
    uint8_t m_LevelTransitionFlag; // +0x05: non-interactive transition gate
    uint8_t retryFlag;             // +0x06
    uint8_t field_0x07;            // +0x07
    float   retryTimer;            // +0x08
    float   m_GameDt;              // +0x0C: per-frame dt accumulator (Ghidra: m_GameDt; was m_TransitionTimer in port)
    float   m_BombHitTimer;        // +0x10
    uint8_t missCount;             // +0x14: combo counter
    uint8_t _pad_0x15[3];          // +0x15..+0x17
    int     currentScore;          // +0x18
    uint8_t m_bUnsullied;          // +0x1C: 0=no misses yet
    uint8_t _pad_0x1d[3];          // +0x1D..+0x1F
    int32_t m_CoinsBalance;        // +0x20
    int32_t m_CoinsTotalEarned;    // +0x24
    int32_t m_CoinsAtGameStart;    // +0x28
    float   m_CritTimer;           // +0x2C
    int     m_ScoreThreshold;      // +0x30
    uint8_t field_0x34;            // +0x34: state byte: 1=HUDDestructing (HUD::Release), 3=BonusFinalePhase (BonusScreen). Re-verify other values.
    uint8_t m_bSlowMotion;         // +0x35
    uint8_t _pad_0x36[2];          // +0x36..+0x37
    float   dt;                    // +0x38
    HUD*    mHud;                  // +0x3C
    uint8_t _pad_0x40[4];          // +0x40..+0x43
    bool    m_bSoundOn;            // +0x44
    bool    m_bMusicOn;            // +0x45
    uint8_t _pad_0x46[2];          // +0x46..+0x47
    FruitCamera*   m_FruitCamera;  // +0x48
    FruitSaveData* m_SaveData;     // +0x4C

    // Font slots +0x50..+0x84 (g_GameData layout, 11 SmartPtr slots)
    Mortar::SmartPtr<Mortar::Font> pFontReserved0;    // +0x50: unused (always null)
    Mortar::SmartPtr<Mortar::Font> pFontMain;         // +0x54: fonts/font_fruit_ninja.fnt
    Mortar::SmartPtr<Mortar::Font> pFontNumbers;      // +0x58: fonts/fruit_ninja_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontReserved1;    // +0x5C: unused (always null)
    uint32_t _gap_0x60;                               // +0x60: not a Font slot per GameDestroy
    Mortar::SmartPtr<Mortar::Font> pFontReserved2;    // +0x64: unused (always null)
    Mortar::SmartPtr<Mortar::Font> pFontGreen;        // +0x68: fonts/fruit_ninja_numbers_green.fnt
    Mortar::SmartPtr<Mortar::Font> pFontArcade;       // +0x6C: fonts/arcade_results_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontGold;         // +0x70: fonts/gold_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontSilver;       // +0x74: fonts/silver_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontBronze;       // +0x78: fonts/bronze_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontArcadeAlias;  // +0x7C: non-owning alias of pFontArcade
    Mortar::SmartPtr<Mortar::Font> pFontBlue2;        // +0x80: fonts/fruit_ninja_numbers_blue2.fnt

    uint8_t _pad_0x84;             // +0x84
    uint8_t m_bTutorialShown;      // +0x85
    uint8_t _pad_0x86[2];          // +0x86..+0x87
    float   field_0x88;            // +0x88 TODO: 0x001f43b8+0x88 -- usage contradictory; SetupGameWork writes 50.0f, Fruit::LoadInfo reads as scale; re-verify.
    uint8_t _pad_0x8c[4];          // +0x8C..+0x8F
    // +0x90/+0x94: dual-purpose -- GameDraw light direction AND global pointer X/Y
    Vec3    worldPos;              // +0x90
    uint8_t m_bTouchDownThisFrame; // +0x9C: edge flag set by PointerDownCallback; cleared at GameUpdate top
    uint8_t m_bTouchUpThisFrame;   // +0x9D: edge flag set by PointerUpCallback; cleared at GameUpdate top
    uint8_t m_bPointerActive;      // +0x9E
    uint8_t _pad_0x9f;             // +0x9F
    // +0xA0..+0x15F: 16 touch/finger slot positions (12 bytes each)
    Vec3    m_FingerSpawnPos[16];  // +0xA0

    MainScreen*    mMainScreen;    // +0x160
    GameOverScreen* pGameOverScreen; // +0x164
    class TutorialControl* m_TutorialControl; // +0x168
    HUDControl* m_pActiveHUDControl; // +0x16C: currently-active dismissible HUD overlay
    uint8_t m_bMPRetryPending;     // +0x170
    uint8_t _pad_0x171[3];         // +0x171..+0x173
    int     fruitTotal;            // +0x174
    CoinCounter* mCoinCounter;     // +0x178
    uint8_t _pad_0x17c[4];         // +0x17C..+0x17F
    TimeControl* mCountDown;       // +0x180
    uint8_t _pad_0x184[4];         // +0x184..+0x187
    GameSound* mGameSound;         // +0x188
    int     m_gameDataLicensedState; // +0x18C
    uint8_t m_bGameOverActive;     // +0x190
    uint8_t _pad_0x191[3];         // +0x191..+0x193
    int     m_FrameTimer;          // +0x194
    uint8_t m_bGameCenterConnecting; // +0x198: set during GameCenter/P2P connection
    uint8_t m_bP2PReady;           // +0x199: gates wave-tick on remote-peer ready
    uint8_t m_bP2PHostMatched;     // +0x19A: P2P checkpoint (TODO: re-verify reads side)
    uint8_t m_bP2PClientJoined;    // +0x19B: P2P checkpoint (TODO: re-verify)
    uint8_t m_bP2PGameStarted;     // +0x19C: P2P checkpoint (TODO: re-verify)
    uint8_t m_bDisconnectPending;  // +0x19D: P2P teardown (TODO: re-verify)
    uint8_t field_0x19e;           // +0x19E
    uint8_t _pad_0x19f;            // +0x19F
    float   m_MenuReturnTimer;     // +0x1A0
    uint8_t _pad_0x1a4[4];         // +0x1A4..+0x1A7
    uint8_t m_bArcadeBonusActive;  // +0x1A8: paired with the 10.5s arcade-bonus timer below
    uint8_t _pad_0x1a9[3];         // +0x1A9..+0x1AB
    float   m_ArcadeBonusTimer;    // +0x1AC: arcade-bonus tally timer (10.5s threshold then ClearTotal("arcadeBonus"))
    uint8_t field_0x1b0;           // +0x1B0 TODO: appears init-only; possibly dead.
    // +0x1B1..+0x5B0: four contiguous 256-byte state blocks (1024 bytes total).
    // Binary @ 0x0010b66c (InitialiseData): the ONLY static references are four
    // back-to-back memset(base+offset, 0, 0x100) calls (PARAM xrefs at
    // 0x0010b68e/0x69e/0x6ae/0x6be) that zero-init the whole region. No other
    // static xref touches any byte in this range (interior addresses checked,
    // all empty) -- so all subsequent access is via runtime-computed pointers
    // (base + dynamic index), the usual shape for keyed item/achievement total
    // arrays populated through ItemManager::LoadItemData /
    // AchievementManager::LoadAchievementInfo. Kept as raw buffers to preserve
    // the 0x608 layout; the indexers live in those managers, not here.
    char    buf0[256];             // +0x1B1
    char    buf1[256];             // +0x2B1
    char    buf2[256];             // +0x3B1
    char    buf3[256];             // +0x4B1
    uint8_t _pad_0x5b1[83];        // +0x5B1..+0x603
    uint8_t m_bFrameDirty;         // +0x604
    uint8_t _pad_0x605[3];         // +0x605..+0x607 (tail pad to 0x608)
};

extern "C" GameWork game_work;  // C-linkage global at .bss 0x001f43b8, zero-initialised

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(GameWork, m_Paused)              == 0x02,  "GameWork::m_Paused");
static_assert(offsetof(GameWork, gameMode)              == 0x04,  "GameWork::gameMode");
static_assert(offsetof(GameWork, m_LevelTransitionFlag) == 0x05,  "GameWork::m_LevelTransitionFlag");
static_assert(offsetof(GameWork, retryFlag)             == 0x06,  "GameWork::retryFlag");
static_assert(offsetof(GameWork, m_GameDt)              == 0x0c,  "GameWork::m_GameDt");
static_assert(offsetof(GameWork, m_BombHitTimer)        == 0x10,  "GameWork::m_BombHitTimer");
static_assert(offsetof(GameWork, mHud)                  == 0x3c,  "GameWork::mHud");
static_assert(offsetof(GameWork, m_bSoundOn)            == 0x44,  "GameWork::m_bSoundOn");
static_assert(offsetof(GameWork, m_bMusicOn)            == 0x45,  "GameWork::m_bMusicOn");
static_assert(offsetof(GameWork, m_FruitCamera)         == 0x48,  "GameWork::m_FruitCamera");
static_assert(offsetof(GameWork, m_SaveData)            == 0x4c,  "GameWork::m_SaveData");
static_assert(offsetof(GameWork, mMainScreen)           == 0x160, "GameWork::mMainScreen");
static_assert(offsetof(GameWork, m_TutorialControl)     == 0x168, "GameWork::m_TutorialControl");
static_assert(offsetof(GameWork, mCoinCounter)          == 0x178, "GameWork::mCoinCounter");
static_assert(offsetof(GameWork, mCountDown)            == 0x180, "GameWork::mCountDown");
static_assert(offsetof(GameWork, mGameSound)            == 0x188, "GameWork::mGameSound");
static_assert(sizeof(GameWork) == 0x608, "GameWork must be 1544 bytes (binary @ 0x001f43b8)");
#endif

#endif // FN_GAME_GAMEWORK_H
