// FruitFactBigClassicFactPage -- v1.6.1 big Classic-mode fact page.
// Binary refs: ctor 0x00172884.

#include "FruitFactBigClassicFactPage.h"
#include "hud/GenericHUDControl.h"
#include "engine/math/Vec3.h"
#include "engine/math/Colour.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"

// Binary @ 0x00172884
FruitFactBigClassicFactPage::FruitFactBigClassicFactPage(
    FruitFactControl* pCtrl, int factIndex, int pageIndex)
    : FruitFactPage(pCtrl)
    , m_factIndex(factIndex)
    , m_pageIndex(pageIndex)
{
}

FruitFactBigClassicFactPage::~FruitFactBigClassicFactPage() {
}

// Binary @ 0x00172884 (ctor body)
// Builds 3 textureless GenericHUDControls for big Classic-mode icon layout.
// Consts (v1.6.1):
//   ctrl1: pos=Vec3(-102,4,0)    -- DAT_172b40=-102, 0x40800000=4.0, DAT_172b44=0
//   ctrl2: pos=ctrl1+Vec3(9,90,0) = Vec3(-93,94,0) -- 0x41100000=9, DAT_172b4c=90
//   ctrl3: pos=Vec3(-20,90,0), scale=Vec3(0,0,0.8) -- 0xc1a00000=-20, DAT_172b50=0.8
// All flags=1 except ctrl3 flags=8. All white col.
void FruitFactBigClassicFactPage::Init() {
    Mortar::SmartPtr<Mortar::Texture> emptyTex;
    Vec3 scUnit(1.0f, 1.0f, 1.0f);
    Colour white(1.0f, 1.0f, 1.0f, 1.0f);

    // ctrl 1: icon slot A
    Vec3 pos1(-102.0f, 4.0f, 0.0f);
    GenericHUDControl* c1 = new GenericHUDControl(
        0.0f, 0.0f, emptyTex, NULL, pos1, scUnit, white, 1);
    AddGenericControl(c1);

    // ctrl 2: icon slot B (offset +9, +90 from ctrl1)
    Vec3 pos2(-102.0f + 9.0f, 4.0f + 90.0f, 0.0f);
    GenericHUDControl* c2 = new GenericHUDControl(
        0.0f, 0.0f, emptyTex, NULL, pos2, scUnit, white, 1);
    AddGenericControl(c2);

    // ctrl 3: icon slot C; scale Z=0.8 (DAT_172b50), flags=8
    Vec3 pos3(-20.0f, 90.0f, 0.0f);
    Vec3 sc3(0.0f, 0.0f, 0.8f);
    GenericHUDControl* c3 = new GenericHUDControl(
        0.0f, 0.0f, emptyTex, NULL, pos3, sc3, white, 8);
    AddGenericControl(c3);
}
