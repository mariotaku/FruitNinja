// FruitFactPage -- base class for v1.6.1 fruit-fact "page book" pages.
// Binary refs (v1.6.1): ctor 0x0017c214, dtor 0x0017d030, etc.
// NOTE: v1.6.1 addresses; current port targets v1.5.1 binary.

#include "FruitFactPage.h"

// Binary @ 0x0017c214 / 0x0017c250
FruitFactPage::FruitFactPage(FruitFactPageControl* pCtrl)
    : BaseScreen()
    , m_pController(pCtrl)
{
}

FruitFactPage::~FruitFactPage() {
}

// Binary @ 0x0017c19c -- base Update; concrete pages override
void FruitFactPage::Update(float /*dt*/) {
}

// Binary @ 0x0017c1b4 -- vtable +0x44: show this page
void FruitFactPage::ShowPage() {
    m_Active = 1;
}

// Binary @ 0x0017c1e8 -- vtable +0x40: hide this page
void FruitFactPage::HidePage() {
    m_Active = 0;
}

// TODO: 0x00173760 -- ShowSubObjects: show all child HUD controls
void FruitFactPage::ShowSubObjects() {
}

// TODO: 0x0017375c -- HideSubObjects: hide all child HUD controls
void FruitFactPage::HideSubObjects() {
}

// TODO: 0x0017c3b4 -- CreateSenseisHead
void FruitFactPage::CreateSenseisHead(float /*scale*/) {
}

// TODO: 0x0017c4cc -- CreateTitleTextControl
void FruitFactPage::CreateTitleTextControl(const char* /*str*/) {
}

// TODO: 0x0017c2d0 -- CreateHorizontalDivider
void FruitFactPage::CreateHorizontalDivider() {
}

// TODO: 0x0017c99c -- CreateSenseisFruitFactText
void FruitFactPage::CreateSenseisFruitFactText() {
}

// TODO: 0x0017c734 -- CreateSenseisFruitFactTitle
void FruitFactPage::CreateSenseisFruitFactTitle() {
}
