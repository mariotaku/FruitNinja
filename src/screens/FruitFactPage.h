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
//     CreateSenseisHead(float)      0x0017c3b4  -> GenericHUDControl*
//     CreateTitleTextControl(char*) 0x0017c4cc  -> GenericHUDControl*
//     CreateHorizontalDivider()     0x0017c2d0  -> void
//     CreateSenseisFruitFactText()  0x0017c99c  -> GenericHUDControl*
//     CreateSenseisFruitFactTitle() 0x0017c734  -> GenericHUDControl*
//
// Binary layout (ARM32, 4-byte ptrs):
//   +0x00..+0x93 : BaseScreen base (0x94 bytes)
//   +0x94        : FruitFactControl* m_pController  (word 0x25)
//   Total: at least 0x98
//
// NOTE: v1.6.1 addresses. This stub is added for compile-clean call-graph
// completeness.
//

#include "BaseScreen.h"

class FruitFactControl;
class GenericHUDControl;

// Port specific: releases FruitFactPage.cpp's file-scope g_SenseisHeadTex global
// ("sensei_head.tex", loaded lazily by CreateSenseisHead @0x0017c3b4). The binary's
// FruitFactControl::UnLoadContent @0x00171a4c nulls exactly three texture slots
// (g_factTex0/1/2); this sensei-head global is an uncovered fourth, left to
// __aeabi_atexit. The port cannot follow that -- its atexit runs after
// SDL_GL_DeleteContext, so the GL texture name would leak. Called from GameDestroy,
// before MeshManager::Destroy().
//
// No-op when no fruit-fact page was ever built. Idempotent; CreateSenseisHead
// re-loads on demand.
void FruitFactPage_UnloadStatics();

class FruitFactPage : public BaseScreen {
public:
    // Binary @ 0x0017c214 / 0x0017c250
    explicit FruitFactPage(FruitFactControl* pCtrl);
    virtual ~FruitFactPage();

    // HUDControl overrides
    void Update(float dt) override;              // Binary @ 0x0017c19c
    int  GetType() override { return 1; }

    // Page lifecycle virtuals (v1.6.1 vtable slots +0x40/+0x44)
    virtual void HidePage();                     // Binary @ 0x0017c1e8
    virtual void ShowPage();                     // Binary @ 0x0017c1b4

    void ShowSubObjects();  // Binary @ 0x00173760 -- empty base hook (overridden by concrete pages)
    void HideSubObjects();  // Binary @ 0x0017375c -- empty base hook (overridden by concrete pages)

protected:
    // +0x94: back-pointer to the owning page-book controller
    FruitFactControl* m_pController;  // word 0x25 in binary struct

    // Helper builders for subclass Init() -- binary @ addresses in comment
    GenericHUDControl* CreateSenseisHead(float scale);      // 0x0017c3b4
    GenericHUDControl* CreateTitleTextControl(const char* str); // 0x0017c4cc
    void               CreateHorizontalDivider();           // 0x0017c2d0
    GenericHUDControl* CreateSenseisFruitFactText();        // 0x0017c99c
    GenericHUDControl* CreateSenseisFruitFactTitle();       // 0x0017c734
};

#endif // FN_SCREENS_FRUIT_FACT_PAGE_H
