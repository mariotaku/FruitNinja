// Analysed: 2026-05-04T00:00

#include "HUD.h"
#include "ScrollingMenu.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include <list>

// Binary @ 0x00144bb0
// DIFFERS: binary doesn't init m_globalTimeScale in ctor (Update sets it each frame);
//          port initialises to 1.0 here which is harmless.
HUD::HUD() : m_globalTimeScale(1.0f) {
    for (int i = 0; i < 6; ++i) scales[i] = 1.0f;
}

// Binary @ 0x00144cd0
HUD::~HUD() {
    Release();
}

// Binary @ 0x00144d18 — controls.clear() only; does NOT reset scales/timeScale
void HUD::Init() {
    controls.clear();
}

// Binary @ 0x00144c5c
// Binary fires m_RemoveCallback + ctrl->Release() (vtable slot) +
// deleting-dtor only when m_bNoDestructor == 0. Controls with
// m_bNoDestructor != 0 are left untouched (HUD does not own them).
// The Release() call before delete is binary-faithful: each subclass's
// virtual Release() does heap-cleanup (delete child labels, null fruit-
// piece backrefs, etc.) so the dtor can be a pure subobject-teardown.
// Subclass dtors that still call Release() will see Release() run a
// second time -- their Release() impls are idempotent (post-conditions:
// pointers are nulled, lists cleared) so the second call is a no-op.
// TODO: set GameData[+0x34] in-Release guard when GameData struct is ported.
void HUD::Release() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* ctrl = *it;
        if (!ctrl->m_bNoDestructor) {
            if (ctrl->m_RemoveCallback)
                ctrl->m_RemoveCallback(ctrl);
            ctrl = *it;
            if (ctrl != nullptr) {
                ctrl->Release();   // binary calls this via vtable before delete
                delete ctrl;
                *it = nullptr;
            }
        }
    }
    controls.clear();
}

// Binary @ 0x00144db0 — bool is "true=push_front, false=push_back"
void HUD::AddControl(HUDControl* ctrl, bool pushFront) {
    if (pushFront)
        controls.push_front(ctrl);
    else
        controls.push_back(ctrl);
}

// Binary @ 0x00144c40
void HUD::RemoveControl(HUDControl* ctrl) {
    if (!ctrl) return;
    if (ctrl->m_RemoveCallback)
        ctrl->m_RemoveCallback(ctrl);
    controls.remove(ctrl);
}

// Binary @ 0x00144b28
void HUD::BeginDraw(float dt) {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        if ((*it)->m_Active)
            (*it)->BeginDraw(dt);
    }
}

// Binary @ 0x00144a90
// DIFFERS: port adds per-control world.Reset() for matrix discipline; binary leaves
//          matrix discipline to each control. Without this, controls that end with a
//          non-identity matrix (e.g. ShopScreen's 481x scale) corrupt the next control's draw.
// ASM-verified: 2026-04-29T03:29Z binary @ 0x00144a90 (HUD::Draw)
void HUD::Draw(int layerMask) {
    Vec3 hudScale(scales[0], scales[1], scales[2]);
    const Vec3 identityScale(1.0f, 1.0f, 1.0f);
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* ctrl = *it;
        if (ctrl->m_Active && (layerMask & ctrl->m_LayerFlags)) {
            // m_bUseHUDScales != 0 (default): receives gameplay-mutable tint window.
            // m_bUseHUDScales == 0: receives identity (1,1,1) — opted out of tint.
            const Vec3& scaleVec = ctrl->m_bUseHUDScales ? hudScale : identityScale;
            world.Reset();
            ctrl->PreDrawOrder(scaleVec, layerMask);
            world.Reset();
            ctrl->DrawOrder(scaleVec, layerMask);
        }
    }
}

// Binary @ 0x00144d20
// delete c matches binary's vtable+4 deleting-dtor (semantically equivalent).
void HUD::Update(float dt) {
    MissControl::PreUpdate(dt);  // global combo-decay pre-tick
    m_globalTimeScale = 1.0f;   // reset to normal speed each tick (vstr.32 s15,[r4,#0x20])

    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ) {
        HUDControl* ctrl = *it;
        if (ctrl->m_Active)
            ctrl->Update(dt);

        ctrl = *it;
        if (ctrl->m_bPendingRemoval) {
            if (ctrl->m_RemoveCallback)
                ctrl->m_RemoveCallback(ctrl);
            ctrl = *it;
            if (!ctrl->m_bNoDestructor) {
                ctrl->Release();   // binary's vtable Release before delete
                delete ctrl;
                *it = nullptr;
            }
            it = controls.erase(it);
        } else {
            ++it;
        }
    }
}

// Binary @ 0x00144b78 — unconditional Reset() dispatch on every control (no null-check in binary)
void HUD::ResetControls() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        (*it)->Reset();
    }
}

// Binary @ 0x00144c00
// Binary dispatches GetType() on every control (regardless of m_bActive)
// and calls ScrollingMenu::ClearTouch on the instance when GetType() == 8.
// Ghidra decompile showed no-arg call due to unknown-convention warning;
// disasm @ 0x00144c22-0x00144c26 confirms r0 (this) is reloaded before blx.
void HUD::OnPause() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        if ((*it)->GetType() == 8 /* TYPE_SCROLLING_MENU */) {
            static_cast<ScrollingMenu*>(*it)->ClearTouch();
        }
    }
}

// Binary @ 0x00144a20 — null-checked iterate, vtable+0x38 Save dispatch (HUDControl::Save slot 14)
void HUD::Save() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* c = *it;
        if (c) c->Save();
    }
}

// Binary @ 0x00144a58 — unconditional Skip() dispatch on every control (vtable+0x34, slot 13)
void HUD::Skip() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        (*it)->Skip();
    }
}

// Binary @ 0x00144dcc — two-pass: collect controls whose SetToMultiplayerState returned true,
//                       then RemoveControl each (avoids iterator invalidation)
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
