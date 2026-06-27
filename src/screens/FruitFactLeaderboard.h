#ifndef FN_SCREENS_FRUIT_FACT_LEADERBOARD_H
#define FN_SCREENS_FRUIT_FACT_LEADERBOARD_H

//
// FruitFactLeaderboard : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x00176980  FruitFactLeaderboard::FruitFactLeaderboard
//   dtor  0x001764b8  FruitFactLeaderboard::~FruitFactLeaderboard
//   Update 0x00177abc FruitFactLeaderboard::Update
//
// Own-field layout (relative to object base; FruitFactPage base = 0x98):
//   +0x98  ptr   m_pDownloadingLabel  (BakedStringBox*)
//   +0x9C  ptr   m_pProviderLabel     (BakedStringBox*)
//   +0xA0  ptr   m_pExtraLabel        (BakedStringBox*)
//   +0xA4  ptr   m_pScoreListHud      (HUDControl*)
//   +0xA8  ptr   m_pActionButton      (MenuButton*)
//   +0xAC  int   m_RefreshCount       -- ctor str#0; Update ldr/add#1/str @0x178524
//   +0xB0  flt   m_RefreshTimer       -- ctor vstr 0.0; Update accumulate/clamp vs 30.0 @0x178394
//   +0xB4  u8    m_ConnectFlag        -- ctor strb#0; Update ldrb @0x1782bc
//   +0xB5  u8[3] _padB5
//   +0xB8  flt   m_FlashTimer         -- Update vldr/vmla/clamp/vstr @0x177ac0 (port was missing)
//   +0xBC  int   m_Mode               -- ctor param2?3:0; feeds ClearScores; Update ldr @0x1782f8
//   +0xC0  int   m_State              -- ctor online-gate 1/2; Update jump-table switch @0x17801c
//   +0xC4  u8[8] _gapC4               -- 8-byte gap, unwritten in ctor/Update/dtor
//   +0xCC  FNHighscore m_Row0         -- dtor str[r5,#0xcc] start, step=0x54
//   +0x120 FNHighscore m_Row1
//   +0x174 FNHighscore m_Row2
//   sizeof == 0x1C8 (456)
//

#include "FruitFactPage.h"
#include "game/FNHighscore.h"
#include <cstdint>

class FruitFactLeaderboard : public FruitFactPage {
public:
    // Binary @ 0x00176980 -- ctor(FruitFactControl*, bool isGlobal)
    FruitFactLeaderboard(FruitFactControl* pCtrl, bool param2);
    ~FruitFactLeaderboard() override;

    // vtable Update override (binary @ 0x00177abc)
    void Update(float dt) override;

private:
    uint32_t  m_pDownloadingLabel;  // +0x98: BakedStringBox* (binary @0x98)
    uint32_t  m_pProviderLabel;     // +0x9C: BakedStringBox* (binary @0x9C)
    uint32_t  m_pExtraLabel;        // +0xA0: BakedStringBox* (binary @0xA0)
    uint32_t  m_pScoreListHud;      // +0xA4: HUDControl* (binary @0xA4)
    uint32_t  m_pActionButton;      // +0xA8: MenuButton* (binary @0xA8)
    int       m_RefreshCount;       // +0xAC: refresh counter (binary int, ctor str#0)
    float     m_RefreshTimer;       // +0xB0: refresh accumulator vs 30.0 (binary float)
    uint8_t   m_ConnectFlag;        // +0xB4: connect byte flag (binary strb/ldrb @0xB4)
    uint8_t   _padB5[3];            // +0xB5
    float     m_FlashTimer;         // +0xB8: flash oscillator (binary float, port was missing)
    int       m_Mode;               // +0xBC: param2?3:0, feeds ClearScores
    int       m_State;              // +0xC0: 1=local-only, 2=online friends
    uint8_t   _gapC4[8];            // +0xC4: genuine 8-byte gap (unwritten in ctor/Update/dtor)
    FNHighscore m_Row0;             // +0xCC
    FNHighscore m_Row1;             // +0x120
    FNHighscore m_Row2;             // +0x174

#ifdef __bada__
    friend struct FruitFactLeaderboardLayoutAssert;
#endif
};

#if defined(__bada__)
#include <cstddef>
struct FruitFactLeaderboardLayoutAssert {
    static_assert(offsetof(FruitFactLeaderboard, m_pDownloadingLabel) == 0x98,  "FruitFactLeaderboard::m_pDownloadingLabel");
    static_assert(offsetof(FruitFactLeaderboard, m_RefreshCount)      == 0xAC,  "FruitFactLeaderboard::m_RefreshCount");
    static_assert(offsetof(FruitFactLeaderboard, m_RefreshTimer)      == 0xB0,  "FruitFactLeaderboard::m_RefreshTimer");
    static_assert(offsetof(FruitFactLeaderboard, m_ConnectFlag)       == 0xB4,  "FruitFactLeaderboard::m_ConnectFlag");
    static_assert(offsetof(FruitFactLeaderboard, m_FlashTimer)        == 0xB8,  "FruitFactLeaderboard::m_FlashTimer");
    static_assert(offsetof(FruitFactLeaderboard, m_Mode)              == 0xBC,  "FruitFactLeaderboard::m_Mode");
    static_assert(offsetof(FruitFactLeaderboard, m_State)             == 0xC0,  "FruitFactLeaderboard::m_State");
    static_assert(offsetof(FruitFactLeaderboard, m_Row0)              == 0xCC,  "FruitFactLeaderboard::m_Row0");
    static_assert(offsetof(FruitFactLeaderboard, m_Row1)              == 0x120, "FruitFactLeaderboard::m_Row1");
    static_assert(offsetof(FruitFactLeaderboard, m_Row2)              == 0x174, "FruitFactLeaderboard::m_Row2");
    static_assert(sizeof(FruitFactLeaderboard)                        == 0x1C8, "FruitFactLeaderboard size");
};
#endif

#endif // FN_SCREENS_FRUIT_FACT_LEADERBOARD_H
