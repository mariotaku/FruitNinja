// Analysed: 2026-05-04T00:00

#include "HUD.h"
#include "ScrollingMenu.h"
#include "game/GameWork.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include <list>

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144bb0 (re-analyst)
// DIFFERS: binary ctor initialises scales[6] to 1.0f and writes 1.0f at +0x24
//          (m_globalTimeScale); m_reserved20 (+0x20) is left uninitialized by ctor.
//          Port zero-inits m_reserved20 and pre-inits m_globalTimeScale here to
//          avoid reading uninit floats before the first Update tick.
HUD::HUD() : m_reserved20(0.0f), m_globalTimeScale(1.0f) {
    for (int i = 0; i < 6; ++i) scales[i] = 1.0f;
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144cd0 (re-analyst)
HUD::~HUD() {
    Release();
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144d18 (re-analyst)
void HUD::Init() {
    controls.clear();
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144c5c (re-analyst)
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

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144db0 (re-analyst)
void HUD::AddControl(HUDControl* ctrl, bool pushFront) {
    if (pushFront)
        controls.push_front(ctrl);
    else
        controls.push_back(ctrl);
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144c40 (re-analyst)
void HUD::RemoveControl(HUDControl* ctrl) {
    if (!ctrl) return;
    ctrl->m_RemoveCallback(ctrl);    // Delegate1::operator() -- internal null-check
    controls.remove(ctrl);
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144b28 (re-analyst)
void HUD::BeginDraw(float dt) {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        if ((*it)->m_Active)
            (*it)->BeginDraw(dt);
    }
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144a90 (re-analyst)
// DIFFERS: binary leaves matrix discipline to each control. Port resets the
//          world matrix between PreDrawOrder and DrawOrder of each control to
//          guard against leftover transforms (e.g. ShopScreen 481x scale).
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
            ctrl->PreDrawOrder(const_cast<float*>(&scaleVec.x), layerMask);
            world.Reset();
            ctrl->DrawOrder(const_cast<float*>(&scaleVec.x), layerMask);
        }
    }
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144d20 (re-analyst)
void HUD::Update(float dt) {
    MissControl::PreUpdate(dt);              // global combo-decay pre-tick
    m_globalTimeScale = 1.0f;               // binary stores 1.0 to +0x24 each tick

    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ) {
        HUDControl* ctrl = *it;
        if (ctrl->m_Active) ctrl->Update(dt);

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

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144b78 (re-analyst)
void HUD::ResetControls() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        (*it)->Reset();
    }
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144c00 (re-analyst)
void HUD::OnPause() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        if ((*it)->GetType() == 8 /* TYPE_SCROLLING_MENU */) {
            static_cast<ScrollingMenu*>(*it)->ClearTouch();
        }
    }
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144a20 (re-analyst)
void HUD::Save() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* c = *it;
        if (c) c->Save();
    }
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144a58 (re-analyst)
void HUD::Skip() {
    for (std::list<HUDControl*>::iterator it = controls.begin(); it != controls.end(); ++it) {
        (*it)->Skip();
    }
}

// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x00144dcc (re-analyst)
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
