// Analysed: 2026-05-04T00:00

#include "HUD.h"
#include "HUDLayer.h"
#include "ScrollingMenu.h"
#include "game/GameWork.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include <list>

// ASM-verified: 2026-05-24 v1.6.1 HUD::{ctor} @ 0x0018c1a0 (re-analyst)
// DIFFERS: binary ctor initialises scales[6] to 1.0f and writes 1.0f at +0x24
//          (m_globalTimeScale); m_DrawAlpha (+0x20) is left uninitialized by ctor.
//          Port zero-inits m_DrawAlpha and pre-inits m_globalTimeScale here to
//          avoid reading uninit floats before the first Update tick.
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

// TODO: HUD::Init -- v1.6.1 address unconfirmed (old marker 0x00144d18 is stale v1.5.x; needs re-RE)
void HUD::Init() {
    controls.clear();
}

// ASM-verified: 2026-05-24 v1.6.1 HUD::Release @ 0x0018c2c0 (re-analyst)
void HUD::Release() {
    // Binary v1.6.1 HUD::Release@0x18c2b8: game_work.m_bHudDestructing = 1 (HUD-teardown guard).
    game_work.m_bHudDestructing = 1;
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* ctrl = *it;
        if (!ctrl->m_bNoDestructor) {
            if (ctrl->m_RemoveCallback)
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

// ASM-verified: 2026-05-24 v1.6.1 HUD::Update @ 0x0018c44c (re-analyst)
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
        if (ctrl->m_Active && partOfModal) ctrl->Update(dt);
#else
        if (ctrl->m_Active) ctrl->Update(dt);
#endif

        ctrl = *it;                          // re-read after Update may have mutated *it
        if (!ctrl->m_bPendingRemoval) { ++it; continue; }

        if (ctrl->m_RemoveCallback) ctrl->m_RemoveCallback(ctrl);
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

// TODO: HUD::Skip -- v1.6.1 address unconfirmed (old marker 0x00144a58 is stale v1.5.x; needs re-RE)
void HUD::Skip() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        (*it)->Skip();
    }
}

// TODO: HUD::SetToMultiplayerState -- v1.6.1 address unconfirmed (old marker 0x00144dcc is stale v1.5.x; needs re-RE)
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
