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
#include "engine/math/_Vector3.h"
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
    class FontCacheObjectTTF;
}

struct GameWork {
    // +0x00..+0x38: UNCHANGED from v1.5.1 baseline.
    uint8_t taskStateIndex;        // +0x00: 0=Splash, 1=Frontend, 2=Game
    uint8_t m_reserved01;          // +0x01: purpose unknown -- written-never-read (zero-init only; no real binary access -- the lone Ghidra xref @ SetAppLicensed 0x11fc98 is a GOT-load false positive, real access is +0x190)
    bool    bM_Mode;               // +0x02: gameplay-mode-active gate (binary: bM_Mode); 0=active, non-zero=paused/inactive
    uint8_t languageFlag;          // +0x03: SetLanguage writes 0 here
    uint8_t gameMode;              // +0x04: GAME_MODE enum stored as uint8_t
    uint8_t bM_bPaused;           // +0x05: pause/inactive gate (binary: bM_bPaused); 1=paused/suppressed
    uint8_t retryFlag;             // +0x06
    uint8_t m_reserved07;          // +0x07: purpose unknown -- written-never-read (zero-init only; no binary xref to game_work+0x07)
    float   retryTimer;            // +0x08
    float   m_PauseAmount;         // +0x0C: pause/fade indicator, range [-1,1] (binary: flM_PauseAmount); 0.0=settled/active, -1.0=fading in, +1.0=fully settled. NOT a time-step.
    float   m_BombHitTimer;        // +0x10
    uint8_t missCount;             // +0x14: miss count (3-strikes; binary bM_MissCount). NOT combo -- combo is the separate g_ComboCount global.
    uint8_t _pad_0x15[3];          // +0x15..+0x17
    int     currentScore;          // +0x18
    uint8_t m_bUnsullied;          // +0x1C: 0=no misses yet
    uint8_t _pad_0x1d[3];          // +0x1D..+0x1F
    int32_t m_CoinsBalance;        // +0x20
    int32_t m_CoinsTotalEarned;    // +0x24
    int32_t m_CoinsAtGameStart;    // +0x28
    float   m_CritTimer;           // +0x2C
    int     m_ScoreThreshold;      // +0x30
    // +0x34 v1.6.1 GameContext.bField_0x34: HUD-teardown guard. HUD::Release@0x18c29c sets 1 around
    // the HUDControl list teardown then clears to 0; BaseScreen::Release@0x160e08 reads it to skip
    // re-entrant teardown. (Distinct from the HUDControl::m_LayerFlags +0x34 seen in ShopScreen/FruitFactPage.)
    uint8_t m_bHudDestructing;     // +0x34
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
    // v1.6.1 game_work._137 (decimal 137 = 0x89): resume-snapshot-present flag.
    // ParseSaveFile @0x00154c8c sets =1 when a <state> tag is present in the save file.
    // InitialiseData @0x0011c3f0 zeroes it. PauseGameCallback @0x001a5978 reads and clears it.
    // QuitGameCallback @0x001a55e0 and RetryGameCallback @0x001a5800 also clear it unconditionally.
    uint8_t m_bResumeSnapshotPresent; // +0x89
    uint8_t _pad_0x8a[2];          // +0x8A..+0x8B
    // v1.6.1 GameContext+0x8C flM_BombSize: bomb/fruit display scale (CreateFruit@0x0019b8fc, Bomb::Init@0x001d6b90; SetupGameWork writes 50.0f). NOT gravity.
    float   flM_BombSize;          // +0x8C
    // +0x90 v1.6.1 Bomb::Init@0x001d6a80: ColSphere.m_Radius = flM_BombCollision*0.5*scale
    float   flM_BombCollision;     // +0x90
    // +0x94/+0x98: dual-purpose -- GameDraw light direction AND global pointer X/Y
    _Vector3<float> worldPos;              // +0x94
    uint8_t m_bTouchDownThisFrame; // +0xA0: edge flag set by PointerDownCallback; cleared at GameUpdate top
    uint8_t m_bTouchUpThisFrame;   // +0xA1: edge flag set by PointerUpCallback; cleared at GameUpdate top
    uint8_t m_bPointerActive;      // +0xA2
    uint8_t _pad_0xa3;             // +0xA3
    // +0xA4..+0x163: 16 touch/finger slot positions (12 bytes each)
    _Vector3<float> m_FingerSpawnPos[16];  // +0xA4

    MainScreen*    mMainScreen;    // +0x164
    GameOverScreen* pGameOverScreen; // +0x168
    class TutorialControl* m_TutorialControl; // +0x16C
    HUDControl* m_pActiveHUDControl; // +0x170: currently-active dismissible HUD overlay
    uint8_t m_bMPRetryPending;     // +0x174
    uint8_t _pad_0x175[3];         // +0x175..+0x177
    // ASM-spec v1.6.1 GameContext::pM_pLastScoredSaveEntry @GameWork+0x178:
    //   Holds the int count returned by FruitSaveData::AddToTotal("all",...) in
    //   AddToCurrentScore, stored as void* (type-pun: binary stores int-as-ptr,
    //   GameOverScreen reads back as (int)game_work.m_pLastScoredSaveEntry).
    //   Used ONLY by GameOverScreen::Update state-6 to pass to UnlockTotalFruitAchievement.
    void*   m_pLastScoredSaveEntry; // +0x178
    CoinCounter* mCoinCounter;     // +0x17C
    // +0x180: localised "back_icon.tex", loaded once by GameInitialise
    // (v1.6.1 GameInitialise @0x0011d22c). Used as the texture of ShopScreen's
    // state-3 (post-purchase) back button. Ghidra's "m_CountdownTex" name was a
    // misnomer -- it has nothing to do with the countdown.
    Mortar::SmartPtr<Mortar::Texture> m_BackIconTex;
    TimeControl* mCountDown;       // +0x184
    void*   _slot_0x188;           // +0x188: null ptr slot (Ghidra: pM_pControlSlot188)
    GameSound* mGameSound;         // +0x18C
    int     m_gameDataLicensedState; // +0x190

    // +0x194..+0x197: four byte fields (Ghidra: dwField_0x194 undefined4).
    uint8_t m_reserved194;         // +0x194: purpose unknown -- written-never-read in this build. SetupGameWork@0x11c0fc sets =1 (dwField_0x194._0_1_); no reader found (paired byte +0x195 is the live suspend guard).
    uint8_t m_bUpdatesSuspended;   // +0x195: suspend-updates/system-notification guard (Ghidra: dwField_0x194._1_1_). SetupGameWork sets 1; CustomNotificationCallback@0x1cf0cc sets 1 on notification-shown; MainScreen/GameOverScreen::Update toggle it.
    uint8_t _pad_0x196[2];         // +0x196..+0x197

    // +0x198..+0x19B: four bytes (Ghidra: dwField_0x198 undefined4) with ZERO xrefs in
    // v1.6.1 -- neither read nor written anywhere in the binary. They are v1.5.1 relics
    // that survive only as layout. Do NOT route live state through them: the connection
    // and opponent-ready flags the port used to keep here are really +0x1A0 / +0x1A1.
    uint8_t m_reserved198;         // +0x198: no xrefs in v1.6.1
    uint8_t m_reserved199;         // +0x199: no xrefs in v1.6.1
    uint8_t m_reserved19a;         // +0x19A: no xrefs in v1.6.1
    uint8_t m_reserved19b;         // +0x19B: no xrefs in v1.6.1

    int     m_FrameTimer;          // +0x19C: frame accumulator (Ghidra: nM_FrameAccumulator)

    // +0x1A0..+0x1A7: P2P/GameCenter session-state byte cluster (Defunct: online P2P MP).
    // +0x1A0 IsP2PConnecting@0x11a1f0 returns it; P2PConnect@0x11c388 + GameModeScreen::Update set 1
    //        immediately before ConnectGameCenter -> GameCenter/P2P connection-in-progress flag.
    uint8_t m_bP2PConnecting;      // +0x1A0  (Defunct: online P2P -- live read by IsP2PConnecting @0x0011a1f0, ConnectPressed @0x00175e30; zeroed by SetupGameWork @0x0011c104)
    // +0x1A1 opponent-ready gate. WaveManager::Update @0x001267e8/@0x00126888 forces dt to 0
    //        while IsOnlineMultiplayer() && this byte == 0 (opponent not yet ready), and
    //        TimeControl::Update @0x001c0adc suppresses the timer tick on
    //        (m_bMPRetryPending && !m_bP2POpponentReady). Set by GameModeScreen::Update,
    //        cleared by SetupGameWork @0x0011c104 and WaveManager::Reset @0x0012beb8.
    //        NOTE: this is the real flag -- +0x199 is a dead v1.5.1 relic, never use it.
    uint8_t m_bP2POpponentReady;   // +0x1A1  (Defunct: online P2P -- but LIVE-read every frame)
    // +0x1A2..+0x1A6: P2P session flags written-never-read in this build (readers were in dead-stripped
    //        networking code). The two writers do NOT overlap: QuitToMenu @0x001cb764 zeroes
    //        1a2..1a5 (alongside DisconnectP2P/MPRetryPending); SetupGameWork @0x0011c104
    //        zeroes only 1a6. No reader found via xref -> reserved, descriptive offset names.
    uint8_t m_reserved1a2;         // +0x1A2: P2P session flag -- written-never-read (QuitToMenu zeroes it)
    uint8_t m_reserved1a3;         // +0x1A3: P2P session flag -- written-never-read (QuitToMenu zeroes it)
    uint8_t m_reserved1a4;         // +0x1A4: P2P session flag -- written-never-read (QuitToMenu zeroes it)
    uint8_t m_reserved1a5;         // +0x1A5: P2P session flag -- written-never-read (QuitToMenu zeroes it)
    uint8_t m_reserved1a6;         // +0x1A6: P2P session flag -- written-never-read (SetupGameWork zeroes it; its ONLY xref)
    uint8_t _pad_0x1a7;            // +0x1A7

    float   m_QuitTransitionTimer; // +0x1A8: quit/cleanup transition timer (Ghidra: flM_QuitTransitionTimer)
    Mortar::InputSink* m_pActiveTouchSink; // +0x1AC: currently active touch-input sink
    // +0x1B0 v1.6.1 GameContext.flM_UpsideDownTimer: device-orientation timer. UpdateUpsideDown@0x11a184
    //        sets 0.75 while the device is held upside-down and decays it by dt once upright;
    //        IsDeviceUpsideDown@0x11a164 returns (timer > 0).
    float   m_UpsideDownTimer;     // +0x1B0
    // +0x1B4 upside-down scoring-active flag. AddToCurrentScore@0x11a6a8 reads it to also bump the
    //        "upside_down_points" save total; WaveManager::Update@0x126908 clears it after >5s upright.
    uint8_t m_bUpsideDownActive;   // +0x1B4
    uint8_t _pad_0x1b5[3];         // +0x1B5..+0x1B7
    float   m_ElapsedGameTime;     // +0x1B8: elapsed game-time accumulator in seconds (Ghidra: dwField_0x1b8 undefined4)
    uint8_t m_reserved1bc;         // +0x1BC: purpose unknown -- written-never-read (SetupGameWork@0x11c0f0 zeroes it; no reader found via xref)

    // +0x1BD..+0x5BC: four contiguous 256-byte state blocks (1024 bytes total).
    // TODO: InitialiseData -- address unresolved (0x0010b66c's PLT thunk resolves to the
    // unrelated Mortar::CombinedDrawCacheList::Iterator::operator++, not this function).
    // Four back-to-back memset(base+offset, 0, 0x100)
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
    // RESERVE ONLY -- port keeps string table in its own singleton (Mortar::StringTable
    // static instance); this field just reserves the correct bytes to preserve
    // GameWork's binary layout, without pulling StringTable.h's heavier machinery
    // (File/StringTableData types) into every GameWork.h consumer.
    uint8_t m_StringTable[0x50];      // +0x5C0: binary Mortar::StringTable slot (reserve; sizeof==0x50)

    bool    m_bFrameDirty;         // +0x610: per-frame dirty flag (was +0x604 in v1.5.1)
    uint8_t _pad_0x611[3];         // +0x611..+0x613

    // +0x614: shared localized TTF face, loaded once by PreloadFontsTTF @0x0011c1fc.
    // Points to "fontstruetype/gangofchinese.ttf" (default) or "fontstruetype/arabic.ttf"
    // (when languageFlag==0x14). Raw owning ptr in the binary; port's FontTTFRegistry owns
    // the FontCacheObjectTTF object — null this on teardown (GameDestroy @0x0011d20c).
    // DojoScreen and ShopScreen read this field for their BakedStringBox font parameter.
    // ASM-spec v1.6.1 PreloadFontsTTF @0x0011c1fc: arabic.ttf if bM_LangId==0x14 else gangofchinese.ttf -> game_work+0x614
    Mortar::FontCacheObjectTTF* m_pTTFFontMain;  // +0x614

    // +0x618..+0x623: std::vector<IngamePopup*> (12 bytes on ARM32 / ptr,size,cap).
    // RESERVE ONLY -- not wired in port; popups managed separately.
    // Declared as byte array to avoid sizeof divergence between host (x64=24B) and ARM32.
    uint8_t m_Popups[12];             // +0x618: binary std::vector<IngamePopup*> slot (reserve; 12B on ARM32)

    // +0x624..+0x667: SmartPtr<Texture>[17] ring textures (68 bytes = 17x4).
    // Populated by PreloadRings() (binary @ 0x11c644), called from GameInitialise.
    Mortar::SmartPtr<Mortar::Texture> m_RingTex[17];   // +0x624..+0x668 (17 ring textures)

    Colour  m_RingColours[13];      // +0x668..+0x69B  v1.6.1 PreloadRings@0x0011cd00 writes pM_Colours[0..12]
    Colour  m_Colour69C;            // +0x69C  spare standalone Colour (PreloadRings sets 0x5C5C5C grey)
    Colour  m_TitleColour;          // +0x6A0  Zen-plate metallic title colour (PreloadRings sets 0x6F461E)
};

extern "C" GameWork game_work;  // C-linkage global at .bss 0x002d931c, zero-initialised

// Free functions defined in GameInit.cpp (binary free functions in the game-task TU).
// AddCoins v1.6.1 @ 0x00119f78 — add delta to coin balance; if delta > 0, also
// accumulates in m_CoinsTotalEarned.
void AddCoins(int delta);

// Pause flag block @ 0x00316700 (binary .bss).
// Defined in GameInit.cpp (same TU as slowTimeTime @ 0x316704).
// PauseGame @0x001ca48c writes g_unpauseDelay / clears g_unpause_game.
// UnpauseGame @0x001ca4b4 writes g_repauseDelay / arms g_unpause_game.
// GameDraw tail @0x001cdd64 reads g_unpause_game and fires the bM_Mode clear + ClearActions.
extern float g_unpauseDelay;   // @ 0x00316708
extern int   g_unpause_game;   // @ 0x0031670c  (byte in binary)
extern float g_repauseDelay;   // @ 0x00316710

// Same .bss block, +0x99 (v1.6.1 GameDraw @0x001cddcc: `ldrb r3,[r5,#0x99]`).
// GameDraw's tail drains it: when set, it clears the Input/Input.txt action set and
// re-clears the flag. GameInit zeroes it at boot.
// TODO: v1.6.1 0x00316799 (clearInput) — no port-side writer yet; whichever binary
//   function arms this flag is not RE'd, so the drain below never fires in the port.
extern int   g_clearInput;     // @ 0x00316799 (byte in binary)

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(GameWork, bM_Mode)               == 0x02,  "GameWork::bM_Mode");
static_assert(offsetof(GameWork, gameMode)              == 0x04,  "GameWork::gameMode");
static_assert(offsetof(GameWork, bM_bPaused)            == 0x05,  "GameWork::bM_bPaused");
static_assert(offsetof(GameWork, retryFlag)             == 0x06,  "GameWork::retryFlag");
static_assert(offsetof(GameWork, m_PauseAmount)         == 0x0c,  "GameWork::m_PauseAmount");
static_assert(offsetof(GameWork, m_BombHitTimer)        == 0x10,  "GameWork::m_BombHitTimer");
static_assert(offsetof(GameWork, rawDt)                 == 0x3c,  "GameWork::rawDt");
static_assert(offsetof(GameWork, mHud)                  == 0x40,  "GameWork::mHud");
static_assert(offsetof(GameWork, m_bSoundOn)            == 0x48,  "GameWork::m_bSoundOn");
static_assert(offsetof(GameWork, m_bMusicOn)            == 0x49,  "GameWork::m_bMusicOn");
static_assert(offsetof(GameWork, m_FruitCamera)         == 0x4c,  "GameWork::m_FruitCamera");
static_assert(offsetof(GameWork, m_SaveData)            == 0x50,  "GameWork::m_SaveData");
static_assert(offsetof(GameWork, m_bResumeSnapshotPresent)      == 0x89,  "GameWork::m_bResumeSnapshotPresent");
static_assert(offsetof(GameWork, flM_BombSize)          == 0x8c,  "GameWork::flM_BombSize");
static_assert(offsetof(GameWork, flM_BombCollision)     == 0x90,  "GameWork::flM_BombCollision");
static_assert(offsetof(GameWork, worldPos)              == 0x94,  "GameWork::worldPos");
static_assert(offsetof(GameWork, m_FingerSpawnPos)      == 0xa4,  "GameWork::m_FingerSpawnPos");
static_assert(offsetof(GameWork, mMainScreen)           == 0x164, "GameWork::mMainScreen");
static_assert(offsetof(GameWork, m_TutorialControl)     == 0x16c, "GameWork::m_TutorialControl");
static_assert(offsetof(GameWork, mCoinCounter)          == 0x17c, "GameWork::mCoinCounter");
static_assert(offsetof(GameWork, m_BackIconTex)         == 0x180, "GameWork::m_BackIconTex");
static_assert(offsetof(GameWork, mCountDown)            == 0x184, "GameWork::mCountDown");
static_assert(offsetof(GameWork, mGameSound)            == 0x18c, "GameWork::mGameSound");
static_assert(offsetof(GameWork, m_FrameTimer)          == 0x19c, "GameWork::m_FrameTimer");
static_assert(offsetof(GameWork, m_bUpdatesSuspended)   == 0x195, "GameWork::m_bUpdatesSuspended");
static_assert(offsetof(GameWork, m_gameDataLicensedState) == 0x190, "GameWork::m_gameDataLicensedState");
static_assert(offsetof(GameWork, m_reserved198)         == 0x198, "GameWork::m_reserved198");
static_assert(offsetof(GameWork, m_bP2PConnecting)      == 0x1a0, "GameWork::m_bP2PConnecting");
static_assert(offsetof(GameWork, m_bP2POpponentReady)   == 0x1a1, "GameWork::m_bP2POpponentReady");
static_assert(offsetof(GameWork, m_reserved1a2)         == 0x1a2, "GameWork::m_reserved1a2");
static_assert(offsetof(GameWork, m_reserved1a6)         == 0x1a6, "GameWork::m_reserved1a6");
static_assert(offsetof(GameWork, m_QuitTransitionTimer) == 0x1a8, "GameWork::m_QuitTransitionTimer");
static_assert(offsetof(GameWork, m_UpsideDownTimer)     == 0x1b0, "GameWork::m_UpsideDownTimer");
static_assert(offsetof(GameWork, m_bUpsideDownActive)   == 0x1b4, "GameWork::m_bUpsideDownActive");
static_assert(offsetof(GameWork, m_reserved1bc)         == 0x1bc, "GameWork::m_reserved1bc");
static_assert(offsetof(GameWork, pGameOverScreen)       == 0x168, "GameWork::pGameOverScreen");
static_assert(offsetof(GameWork, m_pActiveHUDControl)   == 0x170, "GameWork::m_pActiveHUDControl");
static_assert(offsetof(GameWork, m_bMPRetryPending)     == 0x174, "GameWork::m_bMPRetryPending");
static_assert(offsetof(GameWork, m_pActiveTouchSink)    == 0x1ac, "GameWork::m_pActiveTouchSink");
static_assert(offsetof(GameWork, m_ElapsedGameTime)     == 0x1b8, "GameWork::m_ElapsedGameTime");
static_assert(offsetof(GameWork, m_bFrameDirty)         == 0x610, "GameWork::m_bFrameDirty");
static_assert(offsetof(GameWork, m_pTTFFontMain)        == 0x614, "GameWork::m_pTTFFontMain");
static_assert(offsetof(GameWork, m_RingTex)             == 0x624, "GameWork::m_RingTex");
static_assert(offsetof(GameWork, m_RingColours)         == 0x668, "GameWork::m_RingColours");
static_assert(offsetof(GameWork, m_Colour69C)           == 0x69c, "GameWork::m_Colour69C");
static_assert(offsetof(GameWork, m_TitleColour)         == 0x6a0, "GameWork::m_TitleColour");
static_assert(sizeof(GameWork) == 0x6a4, "GameWork must be 1700 bytes (binary @ 0x002d931c)");
#endif

#endif // FN_GAME_GAMEWORK_H
