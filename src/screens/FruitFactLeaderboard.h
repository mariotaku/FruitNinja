#ifndef FN_SCREENS_FRUIT_FACT_LEADERBOARD_H
#define FN_SCREENS_FRUIT_FACT_LEADERBOARD_H

//
// FruitFactLeaderboard : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor  0x00176980  (FruitFactControl*, bool)
//
// Own-field layout (relative to object base; FruitFactPage base = 0x98):
//   +0x98  uint32_t  (state)
//   +0x9C  uint32_t  (state)
//   +0xA0  uint32_t  (state)
//   +0xA4  uint32_t  (state)
//   +0xA8  uint32_t  (state)
//   +0xAC  float     (anim)
//   +0xB0  uint8_t   (flag)
//   +0xB4  float     (anim/alpha)
//   +0xB8  uint32_t  mode selector: param2 ? 3 : 0
//   +0xBC  uint32_t  display mode: 1=local-only, 2=online friends
//   +0xC0  (8 bytes unidentified / padding before first FNHighscore)
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
    uint32_t  m_field98;   // +0x98
    uint32_t  m_field9C;   // +0x9C
    uint32_t  m_fieldA0;   // +0xA0
    uint32_t  m_fieldA4;   // +0xA4
    uint32_t  m_fieldA8;   // +0xA8
    float     m_fieldAC;   // +0xAC
    uint8_t   m_fieldB0;   // +0xB0
    uint8_t   _padB1[3];   // +0xB1 alignment
    float     m_fieldB4;   // +0xB4
    uint32_t  m_ModeSelector;   // +0xB8: param2 ? 3 : 0
    uint32_t  m_DisplayMode;    // +0xBC: 1=local, 2=online
    uint8_t   _padC0[8];        // +0xC0: unidentified gap
    FNHighscore m_Row0;         // +0xC8
    FNHighscore m_Row1;         // +0x11C
    FNHighscore m_Row2;         // +0x170
};

#if defined(__bada__)
#include <cstddef>
static_assert(offsetof(FruitFactLeaderboard, m_field98)    == 0x98,  "FruitFactLeaderboard::m_field98");
static_assert(offsetof(FruitFactLeaderboard, m_ModeSelector) == 0xB8, "FruitFactLeaderboard::m_ModeSelector");
static_assert(offsetof(FruitFactLeaderboard, m_DisplayMode)  == 0xBC, "FruitFactLeaderboard::m_DisplayMode");
static_assert(offsetof(FruitFactLeaderboard, m_Row0)         == 0xC8, "FruitFactLeaderboard::m_Row0");
static_assert(offsetof(FruitFactLeaderboard, m_Row1)         == 0x11C,"FruitFactLeaderboard::m_Row1");
static_assert(offsetof(FruitFactLeaderboard, m_Row2)         == 0x170,"FruitFactLeaderboard::m_Row2");
#endif

#endif // FN_SCREENS_FRUIT_FACT_LEADERBOARD_H
