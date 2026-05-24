#ifndef FN_HUD_FRUIT_FACT_CONTROL_H
#define FN_HUD_FRUIT_FACT_CONTROL_H

// FruitFactControl : HUDControl3d (binary sizeof = 0x204)
// "Best fruit you sliced" callout panel shown on GameOverScreen.
// Shown in gameMode 0 (classic), 2 (arcade), and 3 (dojo/zen).
//
// Binary addresses:
//   ctor       0x0013cb60
//   dtor D1    0x00139e6c
//   Init       0x0013a278
//   Release    0x00139d24
//   Reset      0x00139298
//   BeginDraw  0x0013a0bc
//   Update     0x0013b604
//   DrawOrder  0x0013b95c  (vtable slot 9)
//   LoadContent   0x001399fc
//   UnLoadContent 0x00139f84
//   UpdateLeaderboard 0x0013afbc
//   DrawLeaderboard   0x0013aac0
//   DrawDownloadIcon  0x001395d0
//   LeftPressed  0x001394ec
//   RightPressed 0x001394b0
//   UpPressed    0x0013993c
//   DownPressed  0x0013987c
//   LeftButton   0x0013a130
//   RightButton  0x0013a1d4
//   ConnectPressed 0x00139440
//
// Binary field layout (ARM32, 4-byte pointers):
//   +0x00: HUDControl3d super (0x7C bytes)
//   +0x7C: float m_AnimTimer
//   +0x80: const char* m_pCurFactString
//   +0x84: int m_FruitIdx
//   +0x88: int m_FactIdx
//   +0x8C: Mortar::SmartPtr<Texture> m_FactTexture  (4B)
//   +0x90: Vec3 m_FactPosOffset (12B) -- ASM-verified: 2026-05-11 binary @ 0x0013a278 (re-analyst)
//   +0x9C: Colour m_FactColour  (4B + 4B pad to reach +0xA4)
//   +0xA4: int[11] m_ComboHashArray  (44B; 0xA4+44=0xD0) -- ASM-verified: 2026-05-24 binary @ 0x0013cb60 (re-analyst)
//   +0xD0: int m_ComboLength
//   +0xD4: float m_StarTimer
//   +0xD8: uint8 m_bConnectPressed
//   +0xD9..+0xDB: padding 3B
//   +0xDC: Mortar::SmartPtr<Texture> m_ComboStarTex  (4B)
//   +0xE0: uint8_t m_ComboType (0xFF = no combo) -- ASM-verified: 2026-05-24 binary @ 0x0013cb60 (re-analyst)
//   +0xE4: uint8 m_TabIndex
//   +0xE5..+0xE7: padding 3B
//   +0xE8: LeaderboardList* m_pLeaderboardMenu
//   +0xEC: MenuButton* m_pConnectButton
//   +0xF0: int m_LBVisitedCount
//   +0xF4: float m_LBProgressTimer
//   +0xF8: int m_LBState (0..4)
//   +0xFC: MenuButton* m_pLeftButton
//   +0x100: MenuButton* m_pRightButton
//   +0x104: FNHighscore m_LocalScore  (81B -> padded to 84B, ends at +0x157)
//   +0x158: FNHighscore m_FriendScore1 (81B -> padded to 84B, ends at +0x1AB)
//   +0x1AC: FNHighscore m_FriendScore2 (81B -> padded to 84B, ends at +0x1FF)
//   +0x200: uint8 m_StarType
//   +0x201..+0x203: padding 3B
//   Total: 0x204
//
// Analysed: 2026-05-04T00:00

#include "hud/HUDControl3d.h"
#include "game/FNHighscore.h"
#include "game/LeaderboardList.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/math/Colour.h"
#include <cstdint>
#include <cstddef>

struct InputEvent;
class MenuButton;

class FruitFactControl : public HUDControl3d {
public:
    float          m_AnimTimer;          // +0x7C
    const char*    m_pCurFactString;     // +0x80
    int            m_FruitIdx;           // +0x84 (default -1)
    int            m_FactIdx;            // +0x88 (default -1)
    Mortar::SmartPtr<Mortar::Texture> m_FactTexture; // +0x8C
    // ASM-verified: 2026-05-11 binary @ 0x0013a278 (re-analyst)
    Vec3           m_FactPosOffset;      // +0x90 (12B: x,y,z floats)
    Colour         m_FactColour;         // +0x9C  (4B)
    uint8_t        m_ComboActiveFlag;    // +0xA0: field_0xa0; Zen-only comboFlag (set by Init, read by Update/Draw)
    uint8_t        _pad_factColour[3];   // +0xA1: 3B pad to reach +0xA4
    // +0xA4: int[11] (44B) -- see TODO above re: spec says int[12]
    int            m_ComboHashArray[11]; // +0xA4
    int            m_ComboLength;        // +0xD0
    float          m_StarTimer;          // +0xD4
    uint8_t        m_bConnectPressed;    // +0xD8
    uint8_t        _pad_D9[3];           // +0xD9
    Mortar::SmartPtr<Mortar::Texture> m_ComboStarTex; // +0xDC
    // ASM-verified: 2026-05-24 binary @ 0x0013cb60 (re-analyst) -- uint8_t, NOT int; 0xFF = "no combo"
    uint8_t        m_ComboType;          // +0xE0
    uint8_t        _pad_E1[3];           // +0xE1
    uint8_t        m_TabIndex;           // +0xE4
    uint8_t        _pad_E5[3];           // +0xE5
    LeaderboardList* m_pLeaderboardMenu; // +0xE8
    MenuButton*    m_pConnectButton;     // +0xEC
    int            m_LBVisitedCount;     // +0xF0
    float          m_LBProgressTimer;   // +0xF4
    int            m_LBState;           // +0xF8
    MenuButton*    m_pLeftButton;        // +0xFC
    MenuButton*    m_pRightButton;       // +0x100
    FNHighscore    m_LocalScore;         // +0x104 (81B padded to 84)
    uint8_t        _pad_LocalScore[3];
    FNHighscore    m_FriendScore1;       // +0x158 (binary: 0x104+0x54=0x158)
    uint8_t        _pad_FriendScore1[3];
    FNHighscore    m_FriendScore2;       // +0x1AC (binary: 0x158+0x54=0x1AC)
    uint8_t        _pad_FriendScore2[3];
    uint8_t        m_StarType;           // +0x200
    uint8_t        _pad_201[3];

    // Binary @ 0x0013cb60
    FruitFactControl();
    // Binary @ 0x00139e6c
    virtual ~FruitFactControl() override;

    // HUDControl vtable overrides
    virtual void Init() override;           // Binary @ 0x0013a278
    virtual void Release() override;        // Binary @ 0x00139d24
    virtual void Reset() override {}        // Binary @ 0x00139298 (empty)
    virtual void BeginDraw(float dt) override; // Binary @ 0x0013a0bc
    virtual void Update(float dt) override; // Binary @ 0x0013b604
    virtual void DrawOrder(const Vec3& hudScale, int layerMask) override; // Binary @ 0x0013b95c

    virtual int GetType() override { return 6; }

    // Input handlers
    bool LeftPressed(InputEvent* ev);   // Binary @ 0x001394ec
    bool RightPressed(InputEvent* ev);  // Binary @ 0x001394b0
    bool UpPressed(InputEvent* ev);     // Binary @ 0x0013993c
    bool DownPressed(InputEvent* ev);   // Binary @ 0x0013987c

    // Button callbacks
    void LeftButton();    // Binary @ 0x0013a130
    void RightButton();   // Binary @ 0x0013a1d4

    // Binary @ 0x00139440 -- Defunct: online-services -- no-op stub; binary @ 0x00139440
    void ConnectPressed();

    static void LoadContent();    // Binary @ 0x001399fc
    static void UnLoadContent();  // Binary @ 0x00139f84

private:
    void UpdateLeaderboard(float dt);  // Binary @ 0x0013afbc
    void DrawLeaderboard();            // Binary @ 0x0013aac0
    void DrawDownloadIcon();           // Binary @ 0x001395d0
};

// Layout lock: m_LocalScore at binary offset 0x104.
// Valid only on ARM32 (4-byte pointers, SmartPtr=4B, padding matches binary).
#ifdef __bada__
static_assert(offsetof(FruitFactControl, m_LocalScore) == 0x104,
              "FruitFactControl::m_LocalScore offset mismatch");
static_assert(sizeof(FruitFactControl) == 0x204,
              "FruitFactControl size mismatch");
#endif

#endif // FN_HUD_FRUIT_FACT_CONTROL_H
