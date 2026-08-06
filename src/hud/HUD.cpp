// Analysed: 2026-05-04T00:00

#include "HUD.h"
#include "HUDLayer.h"
#include "ScrollingMenu.h"
#include "game/GameWork.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include <list>

// ASM-spec v1.6.1 HUD::HUD @0x0018c1a0 (C2; C1 @0x0018c1d4 instruction-identical):
//   bl std::list ctor @0x00113bcc, then SEVEN vstr of 1.0f, in binary order:
//   +0x14, +0x08, +0x18, +0x0c, +0x1c, +0x10, +0x24
//   = scales[0..5] (+0x08..+0x1f) and m_globalTimeScale (+0x24). Nothing else.
// DIFFERS: (1) m_DrawAlpha (+0x20) is left UNINITIALIZED by the binary ctor; the port
//          zero-inits it (extra vldr 0.0f + vstr). (2) The port inlines the std::list
//          ctor as two sentinel stores where the binary calls it out-of-line.
//          Together these are the entire 15p-vs-13b asm-verify delta.
HUD::HUD() : m_DrawAlpha(0.0f), m_globalTimeScale(1.0f)
#if !defined(__bada__)
    , m_pInputModal(nullptr)
#endif
{
    for (int i = 0; i < 6; ++i) scales[i] = 1.0f;
}

// TODO: HUD::~HUD -- v1.6.1 address unconfirmed (old marker 0x00144cd0 is stale v1.5.x; needs re-RE)
HUD::~HUD() {
    Release();
}

// ASM-spec v1.6.1 HUD::Init @0x0018c3bc: a single `b 0x00105e04` -- a 4-byte
// tail-call into the out-of-line std::list<HUDControl*>::clear. Body is exactly
// `controls.clear()`; the port inlines clear where the binary outlines it.
void HUD::Init() {
    controls.clear();
}

// ASM-spec v1.6.1 HUD::Release @0x0018c29c (the old marker cited 0x0018c2c0,
// which is `str r0,[sp,#4]` mid-body, not the prologue).
void HUD::Release() {
    // Binary v1.6.1 HUD::Release@0x18c2b8: game_work.m_bHudDestructing = 1 (HUD-teardown guard).
    game_work.m_bHudDestructing = 1;
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* ctrl = *it;
        if (!ctrl->m_bNoDestructor) {
            // Binary calls Delegate1::operator() unconditionally (@0x0018c2e0);
            // the delegate's own empty-check short-circuits. No port-side guard.
            ctrl->m_RemoveCallback(ctrl);
            ctrl = *it;            // re-read after callback
            if (ctrl != nullptr) {
                delete ctrl;       // binary vtable+0x04 deleting-dtor (Itanium ABI slot 1)
                *it = nullptr;
            }
        }
    }
    controls.clear();
    // Binary v1.6.1 HUD::Release@0x18c344: game_work.m_bHudDestructing = 0 (clear HUD-teardown guard).
    game_work.m_bHudDestructing = 0;
}

// TODO: HUD::AddControl -- v1.6.1 address unconfirmed (old marker 0x00144db0 is stale v1.5.x; needs re-RE)
void HUD::AddControl(HUDControl* ctrl, bool pushFront) {
    if (pushFront)
        controls.push_front(ctrl);
    else
        controls.push_back(ctrl);
}

// TODO: HUD::RemoveControl -- v1.6.1 address unconfirmed (old marker 0x00144c40 is stale v1.5.x; needs re-RE)
void HUD::RemoveControl(HUDControl* ctrl) {
    if (!ctrl) return;
    ctrl->m_RemoveCallback(ctrl);    // Delegate1::operator() -- internal null-check
    controls.remove(ctrl);
}

// TODO: HUD::BeginDraw -- v1.6.1 address unconfirmed (old marker 0x00144b28 is stale v1.5.x; needs re-RE)
void HUD::BeginDraw(float dt) {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        if ((*it)->m_Active)
            (*it)->BeginDraw(dt);
    }
}

// ASM-verified: 2026-05-24 v1.6.1 HUD::Draw @ 0x0018bfc4 (re-analyst)
// DIFFERS: binary leaves matrix discipline to each control. Port resets the
//          world matrix between PreDrawOrder and DrawOrder of each control to
//          guard against leftover transforms (e.g. ShopScreen 481x scale).
void HUD::Draw(long layerMask) {
    _Vector3<float> hudScale(scales[0], scales[1], scales[2]);
    const _Vector3<float> identityScale(1.0f, 1.0f, 1.0f);
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* ctrl = *it;
        if (ctrl->m_Active && (layerMask & ctrl->m_LayerFlags)) {
            // m_bUseHUDScales != 0 (default): receives gameplay-mutable tint window.
            // m_bUseHUDScales == 0: receives identity (1,1,1) — opted out of tint.
            const _Vector3<float>& scaleVec = ctrl->m_bUseHUDScales ? hudScale : identityScale;
            world.Reset();
            ctrl->PreDrawOrder(const_cast<float*>(&scaleVec.x), layerMask);
            world.Reset();
            ctrl->DrawOrder(const_cast<float*>(&scaleVec.x), layerMask);
        }
    }
}

// ASM-verified: 2026-08-02T00:00Z v1.6.1 HUD::Update @ 0x0018c3c0 (asm-inspector)
// Binary body:
//   MissControl::PreUpdate(dt); *(this+0x20) = 1.0f;
//   for (it = controls.begin(); it != controls.end(); ) {
//       ctrl = *it;
//       if (ctrl->m_Active != 0 && ctrl->m_bPendingRemoval == 0) ctrl->Update(dt);  // vt+0x28
//       ctrl = *it;                                   // re-read
//       if (ctrl->m_bPendingRemoval == 0) { ++it; continue; }
//       ctrl->m_RemoveCallback(ctrl);                 // unconditional; Delegate1 self-guards
//       ctrl = *it;
//       if (ctrl->m_bNoDestructor == 0) { vt+0x04 deleting-dtor; *it = 0; }
//       it = controls.erase(it);
//   }
void HUD::Update(float dt) {
    MissControl::PreUpdate(dt);              // global combo-decay pre-tick
    m_DrawAlpha = 1.0f;                     // binary vstr.32 s15,[r4,#0x20] s15=1.0 (v1.6.1 @0x0018c3e0)

#if !defined(__bada__)
    // Port specific: a modal that closed/deactivated without going through
    // SetInputModal(NULL) never wedges input off (belt-and-suspenders; the
    // ESC handler also clears this explicitly).
    if (m_pInputModal != nullptr &&
        (!m_pInputModal->m_Active || m_pInputModal->m_bPendingRemoval)) {
        m_pInputModal = nullptr;
    }
#endif

    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ) {
        HUDControl* ctrl = *it;

#if !defined(__bada__)
        // Port specific: modal input capture. While a modal is registered
        // (settings), only it + top-most children (its dropdown ListBox/
        // VerticalScroller, also HUD_LAYER_TOP_MOST) get Update -- other
        // controls are frozen so touches don't pass through to menu buttons
        // behind the modal. No binary counterpart; when m_pInputModal is
        // NULL this is identical to the un-gated binary loop.
        bool partOfModal = (m_pInputModal == nullptr) ||
                            (ctrl == m_pInputModal) ||
                            (ctrl->m_LayerFlags == Mortar::HUD_LAYER_TOP_MOST);
        // Binary gate is two-term: ldrb [+0x30] / beq skip, then ldrb [+0x33] /
        // bne skip (v1.6.1 @0x0018c3f4..0x0018c408). A control already flagged
        // for removal gets NO final Update tick. `partOfModal` is the port-only
        // third term (see above); it is a no-op when no modal is registered.
        if (ctrl->m_Active && !ctrl->m_bPendingRemoval && partOfModal) ctrl->Update(dt);
#else
        // Two-term binary gate -- see the non-__bada__ branch above.
        if (ctrl->m_Active && !ctrl->m_bPendingRemoval) ctrl->Update(dt);
#endif

        ctrl = *it;                          // re-read after Update may have mutated *it
        if (!ctrl->m_bPendingRemoval) { ++it; continue; }

        // Binary calls Delegate1::operator() unconditionally (v1.6.1 @0x0018c438);
        // the delegate's own empty-check short-circuits. No port-side guard.
        ctrl->m_RemoveCallback(ctrl);
        ctrl = *it;
        if (!ctrl->m_bNoDestructor) {
            delete ctrl;                     // binary vtable+0x04 deleting-dtor
            *it = nullptr;
        }
        it = controls.erase(it);
    }
}

#ifndef __bada__
// Port specific: no binary counterpart (see HUD.h). Mirrors HUD::Update's
// active-only walk but calls UpdateRealtime instead of Update, and never
// removes/deletes controls (that stays HUD::Update's job at the fixed tick).
void HUD::UpdateRealtime(float dtSeconds) {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* ctrl = *it;
        if (ctrl->m_Active) ctrl->UpdateRealtime(dtSeconds);
    }
}
#endif

// TODO: HUD::ResetControls -- v1.6.1 address unconfirmed (old marker 0x00144b78 is stale v1.5.x; needs re-RE)
void HUD::ResetControls() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        (*it)->Reset();
    }
}

// ASM-spec v1.6.1 HUD::OnPause @0x0018c208: iterate controls; if GetType()==8 (ScrollingMenu) call ScrollingMenu::ClearTouch @0x001af6a8. BonusScreen GetType==1, not matched.
void HUD::OnPause() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        if ((*it)->GetType() == 8 /* TYPE_SCROLLING_MENU */) {
            static_cast<ScrollingMenu*>(*it)->ClearTouch();
        }
    }
}

// TODO: HUD::Save -- v1.6.1 address unconfirmed (old marker 0x00144a20 is stale v1.5.x; needs re-RE)
void HUD::Save() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* c = *it;
        if (c) c->Save();
    }
}

// ASM-spec v1.6.1 HUD::Skip @0x0018bf24: walks `controls` begin..end and calls
// vtable +0x34 (HUDControl::Skip, slot 13) on each. `ldr r3,[r3,#0x34]` @0x0018bf24.
void HUD::Skip() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        (*it)->Skip();
    }
}

// ASM-spec v1.6.1 HUD::SetToMultiplayerState @0x0018c4d8 (PLT thunk @0x00110524).
// Pass 1 walks `controls` and calls vtable +0x2c (HUDControl::SetToMultiplayerState,
// slot 11) on each, collecting the ones that return non-zero. Pass 2 removes them.
// The `ldr r3,[r3,#0x2c]` @0x0018c510 is the ONLY slot-11 dispatch in the program.
//
// Dead in v1.6.1: the only caller is Game::TellGameToStart @0x001206e8, which has
// no code xrefs of its own. See Game::TellGameToStart in src/Game.cpp for the
// evidence. Kept so the call graph matches the binary.
//
// DIFFERS-trivial: binary's literal shape has a defensive inner-loop that re-scans
//                  controls before each RemoveControl. Omitted here because list::remove
//                  is a no-op on missing elements, making the inner loop observable no-op.
void HUD::SetToMultiplayerState() {
    std::list<HUDControl*> toRemove;
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* c = *it;
        if (c && c->SetToMultiplayerState()) {
            toRemove.push_back(c);
        }
    }
    for (std::list<HUDControl*>::iterator it = toRemove.begin(); it != toRemove.end(); ++it) {
        RemoveControl(*it);
    }
}
