#ifndef FN_SCREENS_FRUIT_FACT_ZEN_PAGE_H
#define FN_SCREENS_FRUIT_FACT_ZEN_PAGE_H

//
// FruitFactZenPage : FruitFactPage  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor       0x0017fcd4
//   Init       0x00180320  (builds achievement list / 'play to unlock' branch)
//   Update     0x0017fa04
//   DrawOrder  0x00180ef0
//   LoadContent   0x0017fa34
//   UnloadContent 0x0017fb00
//   Release    0x0017fb44
//

#include "FruitFactPage.h"
#include "engine/asset/Texture.h"
#include "engine/util/SmartPtr.h"
#include <cstdint>

class FruitFactZenPage : public FruitFactPage {
public:
    // Binary @ 0x0017fcd4
    explicit FruitFactZenPage(FruitFactPageControl* pCtrl);
    ~FruitFactZenPage() override;

    void Init() override;                                        // Binary @ 0x00180320
    void Update(float dt) override;                             // Binary @ 0x0017fa04
    void DrawOrder(const Vec3& hudScale, int layerMask) override; // Binary @ 0x00180ef0
    void Release() override;                                    // Binary @ 0x0017fb44

    static void LoadContent();    // Binary @ 0x0017fa34
    static void UnloadContent();  // Binary @ 0x0017fb00

protected:
    // Fields added by FruitFactZenPage (start at end of FruitFactPage = +0x98).
    // Offsets below are absolute from the object start (binary v1.6.1 Init @ 0x00180320).
    // TODO: 0x00180320 -- exact offsets +0x94..+0xc4 not fully resolved; field names
    //   provisional. field_0x94=hasCombo, field_0x98=perIterFruitInfo are written by Init
    //   but +0x94 conflicts with FruitFactPage::m_pController if that's at +0x94.
    //   Re-verify FruitFactPage base size vs FruitFactZenPage own-field start offset.
    int  m_ZenHasCombo;     // field_0x94 in binary Init (bool hasCombo, stored as int)
    int  m_ZenFruitInfoIdx; // field_0x98 in binary Init (per-iteration comboFruitInfo value)

    // Padding between 0x9c..0xc3 occupied by fields not yet RE'd (Update/DrawOrder use).
    // Port allocates as a raw byte array to preserve object layout for future RE.
    // TODO: 0x0017fa04 / 0x00180ef0 -- resolve fields +0x9c..+0xc3 used by Update/DrawOrder.
    uint8_t m_ZenPad9C[0x28]; // 40 bytes: +0x9c..+0xc3

    int   m_ZenComboCount;  // field_0xc4 in binary Init (comboCount, init 0)
    float m_ZenC8;          // field_0xc8 in binary Init (init -0.5f; animation param)

    // field_0xd0 in binary Init: star result byte (0xff = no combo). Binary Release
    // @ 0x0017fb44 does `add r0,r0,#0xd0; b SmartPtr<Texture>::SetPtr` — this means
    // the star-result byte (+0xd0) is followed immediately by m_TexZen which Release
    // clears. Reconcile: m_ZenStarResult is placed at +0xd0 (1 byte) and m_TexZen
    // at the next 4-byte-aligned slot +0xd4.
    // TODO: 0x0017fb44 -- verify: does Release's add r0,r0,#0xd0 address the texture
    //   SmartPtr directly (making m_TexZen=+0xd0, not m_ZenStarResult), or is +0xd0
    //   a uint8_t star byte and m_TexZen follows at +0xd4?
    uint8_t m_ZenStarResult; // field_0xd0 (byte; 0xff=no combo; binary Init sets this)
    uint8_t m_ZenPadD1[3];   // align to +0xd4

    // +0xd4 (tentative): zen-page texture, constructed null in ctor (binary @ 0x0017fcd4),
    // loaded by LoadContent (0x0017fa34), released to null in Release (0x0017fb44)
    // and UnloadContent (0x0017fb00).
    Mortar::SmartPtr<Mortar::Texture> m_TexZen;   // @+0xd4 (tentative; see TODO above)
};

#endif // FN_SCREENS_FRUIT_FACT_ZEN_PAGE_H
