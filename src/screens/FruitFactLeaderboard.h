#ifndef FN_SCREENS_FRUIT_FACT_LEADERBOARD_H
#define FN_SCREENS_FRUIT_FACT_LEADERBOARD_H

//
// FruitFactLeaderboard : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x00176980  (FruitFactControl*, bool)
//
// Own-field layout (relative to object base; FruitFactPage base = 0x98).
// Names from binary ctor @ 0x00176980 (see disassembly offsets in brackets).
// NOTE on offset divergence: the binary places nM_Mode at +0xBC and nM_State
// at +0xC0; the port currently stores the mode selector at +0xB8 and the
// state at +0xBC (off by one slot, with no +0xC0 field). Renamed here to the
// binary's semantic roles at the PORT's existing offsets (rename-only task,
// offsets preserved). See "LAYOUT GAP" in the .cpp Update TODO.
//   +0x98  ptr   pM_pDownloadingLabel  [str r3,[r4,#0x98]=0]
//   +0x9C  ptr   pM_pProviderLabel     [str r3,[r4,#0x9c]=0]
//   +0xA0  ptr   pM_pExtraLabel        [str r3,[r4,#0xa0]=0]
//   +0xA4  ptr   pM_pScoreListHud      [str r3,[r4,#0xa4]=0]
//   +0xA8  ptr   pM_pActionButton      [str r3,[r4,#0xa8]=0]
//   +0xAC  int   nM_RefreshCount       [str r3,[r4,#0xac]=0]   (binary type=int)
//   +0xB0  flt   flM_RefreshTimer      [vstr s15,[r4,#0xb0]=0] (binary type=float)
//   +0xB4  flt   flM_ConnectTimer      [strb r3,[r4,#0xb4]=0]  (low byte cleared)
//   +0xB8  u32   nM_Mode (selector)    [str r3,[r4,#0xBC] in binary; param2?3:0]
//   +0xBC  u32   nM_State              [str ...; 1=local-only, 2=online friends]
//   +0xC0  (8 bytes gap; binary nM_State lives here @0xC0 + 4-byte pad)
//   +0xC8  FNHighscore  embedded row 0 (0x54 bytes)
//   +0x11C FNHighscore  embedded row 1 (0x54 bytes)
//   +0x170 FNHighscore  embedded row 2 (0x54 bytes)
//   Total own data through +0x1C3; next align to +0x1C4.
//

#include "FruitFactPage.h"
#include "game/FNHighscore.h"
#include <cstdint>

class FruitFactLeaderboard : public FruitFactPage {
public:
    // Binary @ 0x00176980 -- ctor(FruitFactControl*, bool isGlobal)
    FruitFactLeaderboard(FruitFactPageControl* pCtrl, bool param2);
    ~FruitFactLeaderboard() override;

    // vtable Update override (binary @ leaderboard Update slot)
    void Update(float dt) override;

private:
    uint32_t  m_pDownloadingLabel;  // +0x98: BakedStringBox* (binary @0x98)
    uint32_t  m_pProviderLabel;     // +0x9C: BakedStringBox* (binary @0x9C)
    uint32_t  m_pExtraLabel;        // +0xA0: BakedStringBox* (binary @0xA0)
    uint32_t  m_pScoreListHud;      // +0xA4: HUDControl* (binary @0xA4)
    uint32_t  m_pActionButton;      // +0xA8: MenuButton* (binary @0xA8)
    float     m_RefreshCount;       // +0xAC: refresh counter (binary int @0xAC)
    uint8_t   m_FlashFlag;          // +0xB0: flash/refresh timer (binary float @0xB0)
    uint8_t   _padB1[3];   // +0xB1 alignment
    float     m_ConnectTimer;       // +0xB4: connect timer (binary @0xB4)
    uint32_t  m_Mode;          // +0xB8: param2 ? 3 : 0 (binary nM_Mode @0xBC)
    uint32_t  m_State;         // +0xBC: 1=local-only, 2=online (binary nM_State @0xC0)
    uint8_t   _padC0[8];        // +0xC0: unidentified gap
    FNHighscore m_Row0;         // +0xC8
    FNHighscore m_Row1;         // +0x11C
    FNHighscore m_Row2;         // +0x170

#ifdef __bada__
    friend struct FruitFactLeaderboardLayoutAssert;
#endif
};

#if defined(__bada__)
#include <cstddef>
struct FruitFactLeaderboardLayoutAssert {
    static_assert(offsetof(FruitFactLeaderboard, m_pDownloadingLabel) == 0x98, "FruitFactLeaderboard::m_pDownloadingLabel");
    static_assert(offsetof(FruitFactLeaderboard, m_Mode)  == 0xB8, "FruitFactLeaderboard::m_Mode");
    static_assert(offsetof(FruitFactLeaderboard, m_State) == 0xBC, "FruitFactLeaderboard::m_State");
    static_assert(offsetof(FruitFactLeaderboard, m_Row0)         == 0xC8, "FruitFactLeaderboard::m_Row0");
    static_assert(offsetof(FruitFactLeaderboard, m_Row1)         == 0x11C,"FruitFactLeaderboard::m_Row1");
    static_assert(offsetof(FruitFactLeaderboard, m_Row2)         == 0x170,"FruitFactLeaderboard::m_Row2");
};
#endif

#endif // FN_SCREENS_FRUIT_FACT_LEADERBOARD_H
