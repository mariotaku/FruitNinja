// FruitFactZenPage -- v1.6.1 Zen-mode fruit-fact page.
// Binary refs: ctor 0x0017fcd4, Init 0x00180320, etc.

#include "FruitFactZenPage.h"

// Binary @ 0x0017fcd4
FruitFactZenPage::FruitFactZenPage(FruitFactPageControl* pCtrl)
    : FruitFactPage(pCtrl)
{
}

FruitFactZenPage::~FruitFactZenPage() {
}

// Binary @ 0x0017fa34
void FruitFactZenPage::LoadContent() {
    // TODO: 0x0017fa34 -- load Zen-page textures
}

// Binary @ 0x0017fb00
void FruitFactZenPage::UnloadContent() {
    // TODO: 0x0017fb00 -- unload Zen-page textures
}

// Binary @ 0x00180320 -- builds achievement list / 'play to unlock' branch
void FruitFactZenPage::Init() {
    // TODO: 0x00180320 -- build BakedStringBox + GenericHUDControl children
}

// Binary @ 0x0017fa04
void FruitFactZenPage::Update(float /*dt*/) {
    // TODO: 0x0017fa04 -- per-frame update for zen page
}

// Binary @ 0x00180ef0
void FruitFactZenPage::DrawOrder(const Vec3& /*hudScale*/, int /*layerMask*/) {
    // TODO: 0x00180ef0 -- draw zen page
}

// Binary @ 0x0017fb44
void FruitFactZenPage::Release() {
    FruitFactPage::Release();
    // TODO: 0x0017fb44 -- release zen page HUD children
}
