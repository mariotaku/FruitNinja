#ifndef FN_GAME_GAMEWORK_H
#define FN_GAME_GAMEWORK_H

// game_work -- binary symbol at .bss 0x002d931c (size 0x6a4 / 1700 bytes).
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
#include "engine/math/Colour.h"
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
namespace Mortar {
    class ActorManager;
    class Texture;
    class InputSink;
}

struct GameWork {
    // +0x00..+0x38: UNCHANGED from v1.5.1 baseline.
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

    // +0x3C: NEW in v1.6.1 -- raw (unscaled) frame delta-time.
    float   rawDt;                 // +0x3C (v1.6.1 insert; shifts all following fields by +4)

    // All fields below here are shifted +4 relative to v1.5.1.
    HUD*    mHud;                  // +0x40
    uint8_t _pad_0x44[4];          // +0x44..+0x47
    bool    m_bSoundOn;            // +0x48
    bool    m_bMusicOn;            // +0x49
    uint8_t _pad_0x4a[2];          // +0x4A..+0x4B
    FruitCamera*   m_FruitCamera;  // +0x4C
    FruitSaveData* m_SaveData;     // +0x50

    // Font SmartPtr slots +0x54..+0x87 (13 slots x 4 bytes = 52 bytes).
    // In v1.6.1, slot +0x60 IS a real Font slot (confirmed via Ghidra Font*[13]
    // array at offset 84 / 0x54, no gap within the run).
    Mortar::SmartPtr<Mortar::Font> pFontReserved0;    // +0x54: unused (always null)
    Mortar::SmartPtr<Mortar::Font> pFontMain;         // +0x58: fonts/font_fruit_ninja.fnt
    Mortar::SmartPtr<Mortar::Font> pFontNumbers;      // +0x5C: fonts/fruit_ninja_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontReserved1;    // +0x60: Font slot (was pFontReserved1 @v1.5.1+0x5C, shifted)
    Mortar::SmartPtr<Mortar::Font> pFontSlot64;       // +0x64: NEW Font slot in v1.6.1 (was _gap_0x60 non-Font in v1.5.1)
    Mortar::SmartPtr<Mortar::Font> pFontReserved2;    // +0x68: unused (was @v1.5.1+0x64, shifted)
    Mortar::SmartPtr<Mortar::Font> pFontGreen;        // +0x6C: fonts/fruit_ninja_numbers_green.fnt
    Mortar::SmartPtr<Mortar::Font> pFontArcade;       // +0x70: fonts/arcade_results_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontGold;         // +0x74: fonts/gold_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontSilver;       // +0x78: fonts/silver_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontBronze;       // +0x7C: fonts/bronze_numbers.fnt
    Mortar::SmartPtr<Mortar::Font> pFontArcadeAlias;  // +0x80: non-owning alias of pFontArcade
    Mortar::SmartPtr<Mortar::Font> pFontBlue2;        // +0x84: fonts/fruit_ninja_numbers_blue2.fnt

    uint8_t _pad_0x88;             // +0x88
    uint8_t m_bTutorialShown;      // +0x89
    uint8_t _pad_0x8a[2];          // +0x8A..+0x8B
    float   field_0x8c;            // +0x8C: fruitBaseGravity / Fruit scale factor (Ghidra: flFruitBaseGravity). SetupGameWork writes 50.0f.
    uint8_t _pad_0x90[4];          // +0x90..+0x93
    // +0x94/+0x98: dual-purpose -- GameDraw light direction AND global pointer X/Y
    Vec3    worldPos;              // +0x94
    uint8_t m_bTouchDownThisFrame; // +0xA0: edge flag set by PointerDownCallback; cleared at GameUpdate top
    uint8_t m_bTouchUpThisFrame;   // +0xA1: edge flag set by PointerUpCallback; cleared at GameUpdate top
    uint8_t m_bPointerActive;      // +0xA2
    uint8_t _pad_0xa3;             // +0xA3
    // +0xA4..+0x163: 16 touch/finger slot positions (12 bytes each)
    Vec3    m_FingerSpawnPos[16];  // +0xA4

    MainScreen*    mMainScreen;    // +0x164
    GameOverScreen* pGameOverScreen; // +0x168
    class TutorialControl* m_TutorialControl; // +0x16C
    HUDControl* m_pActiveHUDControl; // +0x170: currently-active dismissible HUD overlay
    uint8_t m_bMPRetryPending;     // +0x174
    uint8_t _pad_0x175[3];         // +0x175..+0x177
    int     fruitTotal;            // +0x178
    CoinCounter* mCoinCounter;     // +0x17C
    Mortar::SmartPtr<Mortar::Texture> m_CountdownTex; // +0x180: countdown background texture (Ghidra: m_CountdownTex)
    TimeControl* mCountDown;       // +0x184
    void*   _slot_0x188;           // +0x188: null ptr slot (Ghidra: pM_pControlSlot188)
    GameSound* mGameSound;         // +0x18C
    int     m_gameDataLicensedState; // +0x190

    // +0x194..+0x197: four byte fields (Ghidra: dwField_0x194 undefined4).
    uint8_t field_0x194;           // +0x194: SetupGameWork inits to 1. TODO: 0x002d931c+0x194 -- semantic unknown.
    uint8_t m_bUpdatesSuspended;   // +0x195: suspend-updates/paused-by-system guard (Ghidra: bField_0x195)
    uint8_t _pad_0x196[2];         // +0x196..+0x197

    // +0x198..+0x19B: four P2P/connection byte fields (Ghidra: dwField_0x198 undefined4).
    uint8_t m_bGameCenterConnecting; // +0x198: set during GameCenter/P2P connection
    uint8_t m_bP2PReady;           // +0x199: gates wave-tick on remote-peer ready
    uint8_t m_bP2PHostMatched;     // +0x19A: P2P checkpoint (TODO: re-verify reads side)
    uint8_t m_bP2PClientJoined;    // +0x19B: P2P checkpoint (TODO: re-verify)

    int     m_FrameTimer;          // +0x19C: frame accumulator (Ghidra: nM_FrameAccumulator)

    // +0x1A0..+0x1A7: byte fields (Ghidra bField_0x1a0..bField_0x1a6 + pad).
    uint8_t field_0x1a0;           // +0x1A0
    uint8_t field_0x1a1;           // +0x1A1
    uint8_t field_0x1a2;           // +0x1A2: zeroed by SetupGameWork
    uint8_t field_0x1a3;           // +0x1A3: zeroed by SetupGameWork
    uint8_t field_0x1a4;           // +0x1A4: zeroed by SetupGameWork
    uint8_t field_0x1a5;           // +0x1A5: zeroed by SetupGameWork
    uint8_t field_0x1a6;           // +0x1A6
    uint8_t _pad_0x1a7;            // +0x1A7

    float   m_QuitTransitionTimer; // +0x1A8: quit/cleanup transition timer (Ghidra: flM_QuitTransitionTimer)
    Mortar::InputSink* m_pActiveTouchSink; // +0x1AC: currently active touch-input sink
    float   field_0x1b0;           // +0x1B0: TODO: 0x002d931c+0x1b0 -- semantic unknown (Ghidra: flM_UpsideDownTimer)
    uint8_t field_0x1b4;           // +0x1B4: gate byte; zeroed by SetupGameWork (Ghidra: bField_0x1b4)
    uint8_t _pad_0x1b5[3];         // +0x1B5..+0x1B7
    float   m_ElapsedGameTime;     // +0x1B8: elapsed game-time accumulator in seconds (Ghidra: dwField_0x1b8 undefined4)
    uint8_t field_0x1bc;           // +0x1BC: gate byte; zeroed by SetupGameWork (Ghidra: bField_0x1bc)

    // +0x1BD..+0x5BC: four contiguous 256-byte state blocks (1024 bytes total).
    // Binary @ 0x0010b66c (InitialiseData): four back-to-back memset(base+offset, 0, 0x100)
    // calls zero-init the whole region. All subsequent access is via runtime-computed
    // pointers (base + dynamic index) for keyed item/achievement total arrays populated
    // through ItemManager::LoadItemData / AchievementManager::LoadAchievementInfo.
    char    buf0[256];             // +0x1BD
    char    buf1[256];             // +0x2BD
    char    buf2[256];             // +0x3BD
    char    buf3[256];             // +0x4BD

    // +0x5BD..+0x5BF: pad between buf3 end and StringTable.
    uint8_t _pad_0x5bd[3];         // +0x5BD..+0x5BF

    // +0x5C0..+0x60F: embedded Mortar::StringTable (0x50 / 80 bytes).
    // RESERVE ONLY -- port keeps string table in its own singleton; this field
    // reserves the correct bytes without pulling in StringTable.h (which would
    // conflict with CheckBox.h's port-local LocalizedString stub).
    uint8_t m_StringTable[0x50];      // +0x5C0: binary Mortar::StringTable slot (reserve; sizeof==0x50)

    bool    m_bFrameDirty;         // +0x610: per-frame dirty flag (was +0x604 in v1.5.1)
    uint8_t _pad_0x611[7];         // +0x611..+0x617

    // +0x618..+0x623: std::vector<IngamePopup*> (12 bytes on ARM32 / ptr,size,cap).
    // RESERVE ONLY -- not wired in port; popups managed separately.
    // Declared as byte array to avoid sizeof divergence between host (x64=24B) and ARM32.
    uint8_t m_Popups[12];             // +0x618: binary std::vector<IngamePopup*> slot (reserve; 12B on ARM32)

    // +0x624..+0x66B: SmartPtr<Texture>[17] ring/countdown textures (68 bytes = 17x4).
    // RESERVE ONLY.
    Mortar::SmartPtr<Mortar::Texture> m_RingTex[17]; // +0x624: ring textures (reserve)

    // +0x668..+0x69B: Colour[13] ring colour table (52 bytes = 13x4).
    // RESERVE ONLY.
    Colour  m_RingColours[13];     // +0x668: ring colour palette (reserve)

    // +0x69C..+0x6A3: tail padding / unknown fields.
    uint8_t _pad_0x69c[8];         // +0x69C..+0x6A3
};

extern "C" GameWork game_work;  // C-linkage global at .bss 0x002d931c, zero-initialised

// Shared per-frame gameplay/render flag bitfield -- binary standalone .bss
// uint32_t @ 0x00332bc8 (GOT-loaded, NOT a game_work field). Subsystems OR/BIC
// individual bits each frame: 0x80 = combo-modifier active (ComboModifier::
// UpdateSpecific/RemoveModifier), 0x40 = slice-trail (Game::Update), 0x20 =
// read by DrawUpdate @ 0x1da688. Zeroed by PowerUpManager::SetDefaults/Reset.
extern uint32_t g_GameFrameFlags;

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(GameWork, m_Paused)              == 0x02,  "GameWork::m_Paused");
static_assert(offsetof(GameWork, gameMode)              == 0x04,  "GameWork::gameMode");
static_assert(offsetof(GameWork, m_LevelTransitionFlag) == 0x05,  "GameWork::m_LevelTransitionFlag");
static_assert(offsetof(GameWork, retryFlag)             == 0x06,  "GameWork::retryFlag");
static_assert(offsetof(GameWork, m_GameDt)              == 0x0c,  "GameWork::m_GameDt");
static_assert(offsetof(GameWork, m_BombHitTimer)        == 0x10,  "GameWork::m_BombHitTimer");
static_assert(offsetof(GameWork, rawDt)                 == 0x3c,  "GameWork::rawDt");
static_assert(offsetof(GameWork, mHud)                  == 0x40,  "GameWork::mHud");
static_assert(offsetof(GameWork, m_bSoundOn)            == 0x48,  "GameWork::m_bSoundOn");
static_assert(offsetof(GameWork, m_bMusicOn)            == 0x49,  "GameWork::m_bMusicOn");
static_assert(offsetof(GameWork, m_FruitCamera)         == 0x4c,  "GameWork::m_FruitCamera");
static_assert(offsetof(GameWork, m_SaveData)            == 0x50,  "GameWork::m_SaveData");
static_assert(offsetof(GameWork, m_bTutorialShown)      == 0x89,  "GameWork::m_bTutorialShown");
static_assert(offsetof(GameWork, worldPos)              == 0x94,  "GameWork::worldPos");
static_assert(offsetof(GameWork, m_FingerSpawnPos)      == 0xa4,  "GameWork::m_FingerSpawnPos");
static_assert(offsetof(GameWork, mMainScreen)           == 0x164, "GameWork::mMainScreen");
static_assert(offsetof(GameWork, m_TutorialControl)     == 0x16c, "GameWork::m_TutorialControl");
static_assert(offsetof(GameWork, mCoinCounter)          == 0x17c, "GameWork::mCoinCounter");
static_assert(offsetof(GameWork, m_CountdownTex)        == 0x180, "GameWork::m_CountdownTex");
static_assert(offsetof(GameWork, mCountDown)            == 0x184, "GameWork::mCountDown");
static_assert(offsetof(GameWork, mGameSound)            == 0x18c, "GameWork::mGameSound");
static_assert(offsetof(GameWork, m_FrameTimer)          == 0x19c, "GameWork::m_FrameTimer");
static_assert(offsetof(GameWork, m_bUpdatesSuspended)   == 0x195, "GameWork::m_bUpdatesSuspended");
static_assert(offsetof(GameWork, m_pActiveTouchSink)    == 0x1ac, "GameWork::m_pActiveTouchSink");
static_assert(offsetof(GameWork, m_ElapsedGameTime)     == 0x1b8, "GameWork::m_ElapsedGameTime");
static_assert(offsetof(GameWork, m_bFrameDirty)         == 0x610, "GameWork::m_bFrameDirty");
static_assert(sizeof(GameWork) == 0x6a4, "GameWork must be 1700 bytes (binary @ 0x002d931c)");
#endif

#endif // FN_GAME_GAMEWORK_H
