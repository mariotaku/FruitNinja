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
    // +0xd0: zen-page texture, constructed null in ctor (binary @ 0x0017fcd4),
    // loaded by LoadContent (0x0017fa34), released to null in Release (0x0017fb44)
    // and UnloadContent (0x0017fb00).
    Mortar::SmartPtr<Mortar::Texture> m_TexZen;   // @+0xd0
};

#endif // FN_SCREENS_FRUIT_FACT_ZEN_PAGE_H
