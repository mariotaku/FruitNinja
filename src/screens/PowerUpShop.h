#ifndef FN_POWER_UP_SHOP_H
#define FN_POWER_UP_SHOP_H

// Analysed: 2026-05-04T00:00
//
// PowerUpShop : HUDControl3d (size = 0x138, 312 bytes)
// In-game power-up purchase screen.
//
// Binary vtable @ 0x002cdd88 (vptr->0x002cdd90) (15 slots).
// ctors @ 0x001a81a4 (C1) / 0x001a81f0 (C2)
//
// DEAD CODE in v1.6.1: zero call-site xrefs; see ASM-spec in PowerUpShop.cpp.

#include "hud/HUDControl3d.h"
#include "game/PowerUp.h"
#include "math/_Vector3.h"
#include <cstdint>
#include <vector>

class HUDControl;
class MenuButton;

class PowerUpShop : public HUDControl3d {
public:
    // +0x7C (after HUDControl3d's 0x7c base)
    std::vector<PowerUp*> m_PurchasablePowerUps;   // +0x7C, 12 bytes

    // +0x88
    std::vector<_Vector3<float>> m_SlotLayout;                // +0x88, 12 bytes

    // +0x94
    int m_SelectedIndex;                           // +0x94

    // +0x98: 4-byte gap (confirmed by binary; +0x9C is the half-word field)
    uint8_t _pad98[4];                             // +0x98

    // +0x9C
    uint16_t m_SinIdx;                             // +0x9C

    // +0x9E: pad
    uint8_t _pad9e[2];                             // +0x9E

    // +0xA0
    float m_PulseScale;                            // +0xA0

    // +0xA4
    char m_BuyText[128];                           // +0xA4

    // +0x124
    int m_LastSelectedIndex;                       // +0x124

    // +0x128
    float m_FruitScale;                            // +0x128

    // +0x12C
    MenuButton* m_BuyButton;                       // +0x12C

    // +0x130
    uint8_t m_BuyTriggered;                        // +0x130

    // +0x131
    uint8_t m_BuyButtonState;                      // +0x131

    // +0x132: pad
    uint8_t _pad132[2];                            // +0x132

    // +0x134
    int m_PurchasedCount;                          // +0x134

    PowerUpShop();
    ~PowerUpShop();

    // vtable slot 2 @ 0x001a94b0
    void Init() override;

    // vtable slot 3 @ 0x001a9124
    void Release() override;

    // vtable slot 4 @ 0x001a7fe0 — empty
    void Reset() override;

    // vtable slot 6 @ 0x001a7fe4 — identity (returns hudScale)
    void PreDraw(float* hudScale) override;

    // vtable slot 7 @ 0x001a8364
    void Draw(float* hudScaleRaw) override;

    // vtable slot 10 @ 0x001a8b04
    void Update(float dt) override;

    // Static content management (bodies in PowerUpShop.cpp)
    static void LoadContent();    // @ 0x001a7fdc — empty body
    static void UnLoadContent(); // @ 0x001a830c — nulls five file-static Mortar::SmartPtr<Texture>s
                                 //   (s_boardTexture/s_strokeTexture/s_buttonTextures[0..2]
                                 //    = g_BuyBg/g_Arrow/g_FruitIcons[0..2]);
                                 //   s_strokeTexture->g_Arrow role is plausible-but-untraced

    // Non-virtual members
    void SetBuyButtonState();                     // @ 0x001a8124
    void ButtonSliced();                          // @ 0x001a7fe8
    void ButtonDeleted(HUDControl* deletedCtrl);  // @ 0x001a9438
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(PowerUpShop) == 0x138, "PowerUpShop size mismatch");
static_assert(offsetof(PowerUpShop, m_PurchasablePowerUps) == 0x7c, "m_PurchasablePowerUps offset");
static_assert(offsetof(PowerUpShop, m_SlotLayout)          == 0x88, "m_SlotLayout offset");
static_assert(offsetof(PowerUpShop, m_SelectedIndex)       == 0x94, "m_SelectedIndex offset");
static_assert(offsetof(PowerUpShop, m_SinIdx)              == 0x9c, "m_SinIdx offset");
static_assert(offsetof(PowerUpShop, m_PulseScale)          == 0xa0, "m_PulseScale offset");
static_assert(offsetof(PowerUpShop, m_BuyText)             == 0xa4, "m_BuyText offset");
static_assert(offsetof(PowerUpShop, m_LastSelectedIndex)   == 0x124, "m_LastSelectedIndex offset");
static_assert(offsetof(PowerUpShop, m_FruitScale)          == 0x128, "m_FruitScale offset");
static_assert(offsetof(PowerUpShop, m_BuyButton)           == 0x12c, "m_BuyButton offset");
static_assert(offsetof(PowerUpShop, m_BuyTriggered)        == 0x130, "m_BuyTriggered offset");
static_assert(offsetof(PowerUpShop, m_BuyButtonState)      == 0x131, "m_BuyButtonState offset");
static_assert(offsetof(PowerUpShop, m_PurchasedCount)      == 0x134, "m_PurchasedCount offset");
#endif

#endif // FN_POWER_UP_SHOP_H
