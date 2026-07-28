#ifndef FN_DOJO_SCREEN_H
#define FN_DOJO_SCREEN_H

//
// DojoScreen : HUDControl3d (BaseScreen subclass, binary sizeof 0xb8)
//
// Binary refs (v1.6.1):
//   Constructor    0x0016bad8
//   Update         0x0016b6a4
//   Draw           0x0016a004
//   Init           0x00169e80
//   Release        0x0016c7f8
//   Reset          0x0016b568
//   CreateButtons  0x0016ad9c
//
// Secondary menu shown after tapping the Dojo button on MainScreen.
// Has sub-buttons: Back (return to game), Shop (blade/power-up shop),
// About (credits), plus defunct social-share buttons (BSButton).
//
// State machine:
//   0 = transition-in: lerp m_TransitionAlpha -> 1.0 (step 0.25), -> state 1
//   1 = idle
//   2 = fade out, -> ShopScreen
//   3 = fade out, -> AboutScreen
//   6 = fade out, pending removal -> MainScreen STATE_SLIDE_IN
//
// Button creation: CreateButtons() is called from Reset() (v1.6.1 binary pattern).
// Buttons are re-created after state-2/3 fade-out nulls them and Reset() re-fires.
//
// BSButton creation: m_pBSButton0 (Facebook) and m_pBSButton1 (Twitter) are
// created in the ctor (v1.6.1 DojoScreen::DojoScreen @0x0016bad8) and added to
// the HUD immediately. They are defunct visible stubs -- drawn but do nothing.
//
// Version text: m_pVersionText (BakedStringBox) is created in the ctor and drawn
// in Draw() at the DrawBorders-returned title anchor + Vec3(0,5,0).
//
// Task #66 Phase 2 refinement (FN_BLOCK_PRELOAD only): the state-2 (SHOP)
// handoff no longer reveals the shop and then loads it -- it creates the
// ShopScreen (cheap ctor, chrome LoadContent deferred to the async queue,
// see ShopScreen.cpp) while the dojo is STILL fully covering the screen,
// arms the sliced m_pShopButton's loading spinner (SetLoadingSymbol) +
// HUD::SetInputModal, and holds m_TransitionAlpha (skips DS_DECAY_F) while
// BlockLoader::PreloadBlockStep drains the RES_BLOCK_SHOP queue one item per
// frame. Only once the queue drains does it null the buttons, zero the
// alpha (dojo Draw's `alpha<=0` early-return uncovers it), AddControl/Init
// the shop, and reveal. Mirrors GameModeScreen's m_bLoading INGAME hold
// (Phase 1). See m_bShopLoading / m_pPendingShop below and DojoScreen::Update
// case 2/3/4 in the .cpp. Sync path (flag off) is unchanged: reveal-then-
// construct-then-sync-load, same as the original binary handoff.
//
// Port specific:
//   - No sensei 3D animation (binary has an animated 3D model).
//

#include "BaseScreen.h"
#include "hud/BSButton.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

namespace Mortar { class BakedStringBox; }

class MenuButton;
class AboutScreen;
class ShopScreen;

// Free function (MenuButton.h): flings all live menu entities so the dojo
// entity-count transition gate clears. Forward-declared for the FN_TEST inline
// TestFire*Slice helpers below (avoids pulling MenuButton.h into this header).
void ClearMenuItems();
class DojoScreen : public BaseScreen {
public:
    // Binary ctor takes no arguments (v1.6.1 DojoScreen::DojoScreen @0x0016bad8;
    // spawned by MainScreen::Update cases 3/4 as operator new(0xb8) + 0-arg ctor).
    DojoScreen();
    ~DojoScreen();

    // HUDControl overrides
    void Init() override;
    void Release() override;
    void Reset() override;
    void Update(float dt) override;

#ifndef __bada__
    // Port specific: no binary counterpart -- see HUDControl::UpdateRealtime.
    // Eases m_TransitionAlpha (states 0/2/3/4/6) dt-scaled, once per PRESENTED
    // frame (Game::tickRealtimeUi via HUD::UpdateRealtime), so the dojo
    // fade-in/out tracks the display's actual present rate (60/90/120fps)
    // instead of the fixed 60Hz sim tick. Also repositions the defunct
    // BSButtons (UpdateBSButtons/UpdateBSButton -- pure-visual, reads
    // m_TransitionAlpha only, no state change) here for the same reason.
    // The STATE MACHINE itself (which state, when to transition, the
    // entity-clear + m_TransitionDelay gate, AddControl of the child
    // screen, state-6 pending-removal + MainScreen::SetState) stays in
    // Update() at 60Hz -- it reads the alpha this function advances and
    // fires threshold-crossing transitions there. See DojoScreen.cpp for
    // the DS_APPROACH_F/DS_DECAY_F macros (mirrors ShopScreen's
    // SS_APPROACH_F/SS_DECAY_F and ScrollingMenu's SM_DECAY_F/SM_SPRING_F).
    void UpdateRealtime(float dtSeconds) override;
#endif

    void Draw(float* hudScaleRaw) override;
    int  GetType() override { return 1; }

    // Binary stores textures at GOT-relative static storage.
    static void LoadContent();    // 0x00137a20
    static void UnLoadContent();  // 0x00137c04

    // True when the screen has completed its fade-out and wants to
    // be removed from the HUD. MainScreen polls this to transition
    // back to STATE_SLIDE_IN.
    bool IsPendingRemoval() const { return m_bPendingRemoval != 0; }

    // Matches DojoScreen::ButtonDeleted @ 0x00169e94 (v1.6.1).
    // Binary nulls only m_pShopButton; no RemoveCallback for back/about.
    void ButtonDeleted(HUDControl* ctrl);

    // Port-specific: remove callback helper for AboutScreen child pointer.
    void AboutScreenRemoved(HUDControl*) { m_pAboutScreen = nullptr; }

private:
    // Binary own fields (BaseScreen base = 0x94; own fields 0x94..0xb4):
    MenuButton* m_pBackButton;   // +0x94 (binary name; port used m_pPlayButton)
    MenuButton* m_pShopButton;   // +0x98
    MenuButton* m_pAboutButton;  // +0x9c
    // Defunct: Facebook social share -- visible stub; v1.6.1 DojoScreen ctor @0x0016bad8
    BSButton*              m_pBSButton0;    // +0xa0
    // Defunct: Twitter social share -- visible stub; v1.6.1 DojoScreen ctor @0x0016bad8
    BSButton*              m_pBSButton1;    // +0xa4
    void*                  m_pButton4;      // +0xa8 (HUDControl*)
    int                    m_ResetValue;    // +0xac
    float                  m_TransitionDelay; // +0xb0
    Mortar::BakedStringBox* m_pVersionText; // +0xb4

    // Port-only tail (beyond binary 0xb8 boundary, does not shift binary fields):
    // Child AboutScreen when state==3 triggers. nullptr when not shown.
    AboutScreen* m_pAboutScreen;

#if !defined(__bada__) && defined(FN_BLOCK_PRELOAD)
    // Task #66 Phase 2 refinement -- true while BlockLoader::PreloadBlockStep()
    // is still draining the SHOP work-queue after the dojo state-2 handoff has
    // created (but not yet AddControl/Init'd) the ShopScreen. Holds the dojo
    // panel at its covering alpha (skips DS_DECAY_F) and keeps the sliced
    // shop-ring button's loading-symbol + HUD input-modal armed until the
    // queue drains, mirroring GameModeScreen::m_bLoading (Phase 1).
    bool m_bShopLoading;
    // The ShopScreen created at the start of the hold; AddControl/Init'd and
    // handed off (reveal) only once BlockLoader::PreloadBlockStep drains.
    // nullptr when not mid-hold.
    ShopScreen* m_pPendingShop;
#endif

    // Binary stores textures at GOT-relative globals, not per-instance.
    // Port mirrors this with static members so LoadContent/UnLoadContent
    // can be called from GameInitialise/GameDestroy independently of
    // any DojoScreen instance.
    static Mortar::SmartPtr<Mortar::Texture> s_TexDojo;        // +0x0c: dojo.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexSensei;      // +0x10: dojo_sensei.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexShop;        // +0x14: senseis_swag.tex (loaded but not drawn as button texture; button uses m_RingTex[7])
    static Mortar::SmartPtr<Mortar::Texture> s_TexAbout;       // +0x18: about.tex (loaded but not drawn as button texture; button uses m_RingTex[12])

    // --- CreateButtons (v1.6.1 CreateButtons @0x0016ad9c) ---
    // Creates m_pBackButton, m_pShopButton, m_pAboutButton with null guards.
    // Called from Reset() (binary pattern: Reset calls CreateButtons unconditionally).
    void CreateButtons();

    // ASM-spec v1.6.1 DojoScreen::UpdateBSButton @0x0016a2f4 + T_1162 @0x0016a274:
    //   slides btn to anchor=(152, 100-46*idx, 0) along dir=norm(8,-1,0)
    //   by offset (1 - m_TransitionAlpha) * 248. Called every frame.
    void UpdateBSButton(BSButton* btn, float dt, unsigned long idx);

    // ASM-spec v1.6.1 DojoScreen::UpdateBSButtons @0x0016b580:
    //   loops m_pBSButton0/1 (idx 0/1) and defunct m_pButton4/m_ResetValue
    //   (idx 2/3, always null). Repositions all non-null BSButtons each frame.
    void UpdateBSButtons(float dt);

    // --- Callbacks ---
    void PlayCallback();
    void ShopCallback();
    void AboutCallback();
    // FacebookPressed/TwitterPressed: binary uses FREE functions bound via
    // Delegate0<void>::MakeFree, not DojoScreen members -- see game/Social.h
    // and CreateButtons() in DojoScreen.cpp.

public:
    // Defunct: more-games/online dashboard upsell -- no-op stub; v1.6.1 DojoScreen::MoreGamesCallback @0x00169eec
    void MoreGamesCallback();
    // Defunct: iOS Quit-from-Dojo callback -- no-op stub; binary symbol exists
    // with ZERO Bada call-site xrefs (iPhone/iPad-variant leftover .o).
    //
    // NOTE: the Back button click delegate (Play -> state 6) is implemented as
    // DojoScreen::PlayCallback() in DojoScreen.cpp (not a separate QuitCallback).
    void QuitCallback();
    // Defunct: online network-provider selection -- no-op stub; v1.6.1 DojoScreen::SwitchCallback @0x00169ea8
    void SwitchCallback();
    // Defunct: online network-provider button -- no-op stub; TODO: re-verify v1.6.1 DojoScreen::SwitchNetworkButton address (no named symbol)
    void SwitchNetworkButton(MenuButton*, float, ScreenButton&);
    // Defunct: Twitter/Facebook social-share button layout/animation -- no-op stub; TODO: re-verify v1.6.1 DojoScreen::TwitterFacbookButtons address (no named symbol)
    void TwitterFacbookButtons(MenuButton*, float, ScreenButton&);

#ifdef __bada__
    friend struct DojoScreenLayoutAssert;
#endif

#ifdef FN_TEST
public:
    // Test-only: simulate slicing the About ring.
    // Calls ClearMenuItems() (same as MenuButton::Update's slice path) then
    // AboutCallback(), reproducing the exact call sequence the ring-slice fires.
    // Inline so the body links into the FN_TEST test exe (DojoScreen.cpp builds
    // into fruit-ninja-game.lib WITHOUT FN_TEST, so out-of-line bodies vanish).
    void TestFireAboutSlice() { ::ClearMenuItems(); AboutCallback(); }
    // Test-only: simulate slicing the Shop ring (ClearMenuItems + ShopCallback).
    void TestFireShopSlice()  { ::ClearMenuItems(); ShopCallback();  }
    // Read current state machine index (BaseScreen::m_State, protected member).
    int  TestGetState()        const { return m_State; }
    // True once the Update state-2/3/4 transition gate fired (m_pBackButton nulled).
    // Binary: compare-only m_pBackButton != NULL at @0x0016b6a4; the pointer dangles
    // after back button self-removes but is never dereferenced in the gate.
    bool TestTransitionFired() const { return m_pBackButton == nullptr; }
    // The AboutScreen child pushed during state-3 transition; null otherwise.
    AboutScreen* TestGetAboutScreen() const { return m_pAboutScreen; }
private:
#endif
};

#ifdef __bada__
#include <cstddef>
// Binary sizeof(DojoScreen) == 0xb8 (v1.6.1 DojoScreen ctor @0x0016bad8).
// Port-only tail (m_pAboutScreen) pushes sizeof past 0xb8, so we assert
// offsetof on the binary-faithful prefix only (same pattern as MainScreen).
// Friend struct: GCC 4.4 rejects offsetof on private members from namespace scope.
struct DojoScreenLayoutAssert {
static_assert(__builtin_offsetof(DojoScreen, m_pBackButton)      == 0x94, "m_pBackButton offset");
static_assert(__builtin_offsetof(DojoScreen, m_pShopButton)      == 0x98, "m_pShopButton offset");
static_assert(__builtin_offsetof(DojoScreen, m_pAboutButton)     == 0x9c, "m_pAboutButton offset");
static_assert(__builtin_offsetof(DojoScreen, m_pBSButton0)       == 0xa0, "m_pBSButton0 offset");
static_assert(__builtin_offsetof(DojoScreen, m_pBSButton1)       == 0xa4, "m_pBSButton1 offset");
static_assert(__builtin_offsetof(DojoScreen, m_pButton4)         == 0xa8, "m_pButton4 offset");
static_assert(__builtin_offsetof(DojoScreen, m_ResetValue)       == 0xac, "m_ResetValue offset");
static_assert(__builtin_offsetof(DojoScreen, m_TransitionDelay)  == 0xb0, "m_TransitionDelay offset");
static_assert(__builtin_offsetof(DojoScreen, m_pVersionText)     == 0xb4, "m_pVersionText offset");
};
#endif

#endif
