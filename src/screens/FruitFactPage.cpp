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

// Binary @ 0x00173760 -- empty base-class virtual (bare `bx lr`).
// Concrete FruitFactPage subclasses override to show their child controls;
// the base default is a no-op (verified against v1.6.1 binary disassembly).
void FruitFactPage::ShowSubObjects() {
}

// Binary @ 0x0017375c -- empty base hook (single `bx lr`).
// Concrete page subclasses override this to hide their child HUD controls;
// the FruitFactPage base body is a genuine no-op in the binary.
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
