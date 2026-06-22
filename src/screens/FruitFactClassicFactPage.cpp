// FruitFactClassicFactPage -- v1.6.1 Classic-mode fact page (one fact card).
// Binary refs: ctor 0x00174e30.

#include "FruitFactClassicFactPage.h"
#include "hud/GenericHUDControl.h"
#include "engine/math/Vec3.h"
#include "engine/math/Colour.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"

// Binary @ 0x00174e30
// The two int args (headIdx, bodyIdx) are consumed locally by the ctor body;
// they are NOT stored as fields. m_pTitleBox and m_pBodyBox are lazy-inited to NULL.
// ASM-verified: v1.6.1 FruitFactClassicFactPage @ 0x00174e30
FruitFactClassicFactPage::FruitFactClassicFactPage(
    FruitFactPageControl* pCtrl, int /*headIdx*/, int /*bodyIdx*/)
    : FruitFactPage(pCtrl)
    , m_pTitleBox(NULL)
    , m_pBodyBox(NULL)
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

    // ctrl 1: primary icon slot; m_pTitleBox will be lazily set on first use
    Vec3 pos1(-202.0f, -24.0f, 0.0f);
    GenericHUDControl* c1 = new GenericHUDControl(
        0.0f, 0.0f, emptyTex, NULL, pos1, scUnit, white, 1);
    AddGenericControl(c1);

    // ctrl 2: secondary slot (offset +9, +40 from ctrl1); m_pBodyBox lazily set on first use
    Vec3 pos2(-202.0f + 9.0f, -24.0f + 40.0f, 0.0f);
    GenericHUDControl* c2 = new GenericHUDControl(
        0.0f, 0.0f, emptyTex, NULL, pos2, scUnit, white, 1);
    AddGenericControl(c2);
}
