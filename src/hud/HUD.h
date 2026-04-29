#ifndef FN_HUD_H
#define FN_HUD_H

#include "HUDControl.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include <list>

// Matches original HUD class (~0x20 bytes)
// See docs/structs/hud.md for layout and function list.
class HUD {
public:
    // +0x00: control list
    std::list<HUDControl*> controls;

    // HUD struct +0x08..+0x1F: six tint multipliers, all init 1.0f.
    //   scales[0..2] = UI tint window  (passed to HUDControl::Draw via param_1)
    //   scales[3..5] = world tint window (passed to SplatEntity::DrawSplat)
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x00144a90 (HUD::Draw)
    float scales[6];

    HUD() {
        for (int i = 0; i < 6; ++i) scales[i] = 1.0f;
    }
    ~HUD() { Release(); }

    // Matches HUD::AddControl (0x105b40)
    void AddControl(HUDControl* ctrl, bool pushFront = false) {
        if (pushFront)
            controls.push_front(ctrl);
        else
            controls.push_back(ctrl);
    }

    // Matches HUD::RemoveControl (0x144c40)
    void RemoveControl(HUDControl* ctrl) {
        if (ctrl->m_RemoveCallback)
            ctrl->m_RemoveCallback(ctrl);
        controls.remove(ctrl);
    }

    // Matches HUD::BeginDraw (0x144b28)
    void BeginDraw(float dt) {
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            if ((*it)->m_bActive)
                (*it)->BeginDraw(dt);
        }
    }

    // Matches HUD::Draw (0x144a90)
    // Original: passes &scales[0] as param_1 when ctrl[+0x60] != 0, else identity.
    // SPEC GAP: HUDControl+0x60 tint-flag semantic not yet fully RE'd.
    //           Port always passes &scales[0] (safe default: scales=1,1,1 = no-op).
    //
    // Per-control matrix discipline: reset the world matrix between controls
    // so each control inherits identity. The binary's HUD::Draw has the same
    // discipline (e.g. ShopScreen::Draw ends with SetCurrentMatrix(Scale44(
    // 481, 321, 0)) for the BG_store quad and that scale must NOT bleed into
    // the next control's first draw call). Without this, e.g. ShopListItem's
    // Font::DrawString sees a 481x scale already in the stack and glyph
    // quads land thousands of pixels off-screen.
    void Draw(int layerMask) {
        // Vec3 aliases the first 3 floats of scales[] — &scales[0] == &scaleVec.x
        Vec3 scaleVec(scales[0], scales[1], scales[2]);
        Mortar::MatrixStack& world = Mortar::MatrixManager::GetInstance().GetWorldStack();
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            HUDControl* ctrl = *it;
            if (ctrl->m_bActive && (layerMask & ctrl->m_LayerFlags)) {
                world.Reset();
                ctrl->PreDrawOrder(scaleVec, layerMask);
                world.Reset();
                ctrl->DrawOrder(scaleVec, layerMask);
            }
        }
    }

    // Matches HUD::Update (0x144d40)
    void Update(float dt) {
        for (auto it = controls.begin(); it != controls.end(); ) {
            HUDControl* ctrl = *it;
            if (ctrl->m_bActive)
                ctrl->Update(dt);

            if (ctrl->m_bPendingRemoval) {
                if (ctrl->m_RemoveCallback)
                    ctrl->m_RemoveCallback(ctrl);
                if (!ctrl->m_bNoDestructor)
                    delete ctrl;
                it = controls.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Matches HUD::OnPause (0x144c00)
    void OnPause() {
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            if ((*it)->m_bActive)
                (*it)->Init(); // OnPause dispatches to Init in some cases
        }
    }

    // Matches HUD::Release (0x144c5c)
    void Release() {
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            (*it)->Release();
            delete *it;
        }
        controls.clear();
    }
};

#endif
