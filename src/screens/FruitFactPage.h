#ifndef FN_SCREENS_FRUIT_FACT_PAGE_H
#define FN_SCREENS_FRUIT_FACT_PAGE_H

//
// FruitFactPage : BaseScreen  (v1.6.1 binary)
//
// Binary refs (v1.6.1):
//   ctor C1  0x0017c214
//   ctor C2  0x0017c250
//   dtor D0  0x0017d030
//   dtor D1  0x0017d06c
//   dtor D2  0x00173764
//   Update   0x0017c19c
//   ShowPage 0x0017c1b4  (vtable slot +0x44 = vtable[17])
//   HidePage 0x0017c1e8  (vtable slot +0x40 = vtable[16])
//   ShowSubObjects 0x00173760
//   HideSubObjects 0x0017375c
//   Helpers:
//     CreateSenseisHead(float)      0x0017c3b4
//     CreateTitleTextControl(char*) 0x0017c4cc
//     CreateHorizontalDivider()     0x0017c2d0
//     CreateSenseisFruitFactText()  0x0017c99c
//     CreateSenseisFruitFactTitle() 0x0017c734
//
// Binary layout (ARM32, 4-byte ptrs):
//   +0x00..+0x93 : BaseScreen base (0x94 bytes)
//   +0x94        : FruitFactPageControl* m_pController  (word 0x25)
//   Total: at least 0x98
//
// NOTE: these are v1.6.1 addresses. The current port targets v1.5.1;
// address spaces differ. This stub is added for compile-clean call-graph
// completeness.
//

#include "BaseScreen.h"

// Forward-declare the page-book controller (v1.6.1 page-book; named
// FruitFactPageControl in the port to avoid collision with the v1.5.1
// FruitFactControl game-over panel class already in src/hud/).
class FruitFactPageControl;

class FruitFactPage : public BaseScreen {
public:
    // Binary @ 0x0017c214 / 0x0017c250
    explicit FruitFactPage(FruitFactPageControl* pCtrl);
    virtual ~FruitFactPage();

    // HUDControl overrides
    void Update(float dt) override;              // Binary @ 0x0017c19c
    int  GetType() override { return 1; }

    // Page lifecycle virtuals (v1.6.1 vtable slots +0x40/+0x44)
    virtual void HidePage();                     // Binary @ 0x0017c1e8
    virtual void ShowPage();                     // Binary @ 0x0017c1b4

    // TODO: 0x00173760 -- ShowSubObjects: show all child HUD controls
    void ShowSubObjects();
    // TODO: 0x0017375c -- HideSubObjects: hide all child HUD controls
    void HideSubObjects();

protected:
    // +0x94: back-pointer to the owning page-book controller
    FruitFactPageControl* m_pController;  // word 0x25 in binary struct

    // Helper builders for subclass Init() -- all stubbed
    // TODO: 0x0017c3b4 -- CreateSenseisHead(float scale)
    void CreateSenseisHead(float scale);
    // TODO: 0x0017c4cc -- CreateTitleTextControl(const char* str)
    void CreateTitleTextControl(const char* str);
    // TODO: 0x0017c2d0 -- CreateHorizontalDivider()
    void CreateHorizontalDivider();
    // TODO: 0x0017c99c -- CreateSenseisFruitFactText()
    void CreateSenseisFruitFactText();
    // TODO: 0x0017c734 -- CreateSenseisFruitFactTitle()
    void CreateSenseisFruitFactTitle();
};

#endif // FN_SCREENS_FRUIT_FACT_PAGE_H
