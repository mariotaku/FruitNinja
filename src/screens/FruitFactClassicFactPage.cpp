// FruitFactClassicFactPage -- v1.6.1 Classic-mode fact page (one fact card).
// Binary refs: ctor 0x00174e30.

#include "FruitFactClassicFactPage.h"
#include "hud/GenericHUDControl.h"
#include "engine/math/Vec3.h"
#include "engine/math/Colour.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"

// Binary @ 0x00174e30
FruitFactClassicFactPage::FruitFactClassicFactPage(
    FruitFactPageControl* pCtrl, int factIndex, int pageIndex)
    : FruitFactPage(pCtrl)
    , m_factIndex(factIndex)
    , m_pageIndex(pageIndex)
{
}

FruitFactClassicFactPage::~FruitFactClassicFactPage() {
}

// Binary @ 0x00174e30 (ctor body)
// Builds 2 textureless GenericHUDControls for Classic fact card layout.
// Consts (v1.6.1):
//   ctrl1: pos=Vec3(-202,-24,0) -- DAT_175020=-202, 0xc1c00000=-24
//   ctrl2: pos=ctrl1+Vec3(9,40,0) = Vec3(-193,16,0) -- 0x41100000=9, DAT_175028=40
// Both flags=1, scale unit, col white.
void FruitFactClassicFactPage::Init() {
    Mortar::SmartPtr<Mortar::Texture> emptyTex;
    Vec3 scUnit(1.0f, 1.0f, 1.0f);
    Colour white(1.0f, 1.0f, 1.0f, 1.0f);

    // ctrl 1: primary icon slot
    Vec3 pos1(-202.0f, -24.0f, 0.0f);
    GenericHUDControl* c1 = new GenericHUDControl(
        0.0f, 0.0f, emptyTex, NULL, pos1, scUnit, white, 1);
    AddGenericControl(c1);

    // ctrl 2: secondary slot (offset +9, +40 from ctrl1)
    Vec3 pos2(-202.0f + 9.0f, -24.0f + 40.0f, 0.0f);
    GenericHUDControl* c2 = new GenericHUDControl(
        0.0f, 0.0f, emptyTex, NULL, pos2, scUnit, white, 1);
    AddGenericControl(c2);
}
