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
// Layout (FruitFactPage base = 0x98 bytes; derived own-fields start at +0x98):
//   +0x94 : FruitFactPageControl* m_pController  (BASE field, read-only in Init)
//   +0x98 : uint8 m_HasCombo      (strb [r5,#0x98] = (fruitCount>2)?1:0)
//   +0x9c : int   m_ComboFruitInfo[11]  (array +0x9c..+0xc7; CheckCombo(this+0x9c,...))
//   +0xc8 : int   m_ComboCount    (str [r5,#0xc8])
//   +0xcc : float m_AnimCC        (vstr s15(=-0.5f),[r5,#0xcc])
//   +0xd0 : SmartPtr<Texture> m_TexZen  (4-byte raw-ptr SmartPtr; ctor+Release use +0xd0)
//   +0xd4 : uint8 m_StarResult    (strb [r5,#0xd4] = 0xff; ldrsb [r5,#0xd4])
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
    // +0x98: whether this session had a combo (fruitCount > 2)
    uint8_t m_HasCombo;          // @+0x98
    uint8_t _pad99[3];

    // +0x9c: per-combo fruit-info id array (int[11], spans +0x9c..+0xc7)
    // CheckCombo(this+0x9c, m_ComboCount, &outDominant) reads this array.
    // Elements written by Init loop: *(int*)(this+0x9c+i*4) = fruitInfoId[i].
    int m_ComboFruitInfo[11];    // @+0x9c..+0xc7

    // +0xc8: how many fruits in the best combo this session
    int   m_ComboCount;          // @+0xc8

    // +0xcc: animation accumulator (init -0.5f)
    float m_AnimCC;              // @+0xcc

    // +0xd0: zen-page texture SmartPtr (4-byte raw-ptr form; ctor/Release operate on [r0,#0xd0])
    Mortar::SmartPtr<Mortar::Texture> m_TexZen;   // @+0xd0

    // +0xd4: star result byte (0xff = no combo; set by CheckCombo; read as signed byte)
    uint8_t m_StarResult;        // @+0xd4
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(FruitFactZenPage, m_TexZen) == 0xd0,
    "FruitFactZenPage::m_TexZen must be at +0xd0");
static_assert(offsetof(FruitFactZenPage, m_StarResult) == 0xd4,
    "FruitFactZenPage::m_StarResult must be at +0xd4");
#endif

#endif // FN_SCREENS_FRUIT_FACT_ZEN_PAGE_H
