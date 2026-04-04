#ifndef FN_HUD_H
#define FN_HUD_H

#include "HUDControl.h"
#include <list>

// Matches original HUD class (~0x20 bytes)
// See docs/structs/hud.md for layout and function list.
class HUD {
public:
    // +0x00: control list
    std::list<HUDControl*> controls;

    // +0x08: scales (6 floats in original, all init 1.0; port uses Vec3)
    Vec3 scale;

    HUD() : scale(1.0f, 1.0f, 1.0f) {}
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
    // Original: loads globalPos from GOT = (1.0, 1.0, 1.0)
    // Calls PreDrawOrder then DrawOrder on each active, layer-matching control
    void Draw(int layerMask) {
        Vec3 globalPos(1.0f, 1.0f, 1.0f);
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            HUDControl* ctrl = *it;
            if (ctrl->m_bActive && (layerMask & ctrl->m_LayerFlags)) {
                ctrl->PreDrawOrder(globalPos, layerMask);
                ctrl->DrawOrder(globalPos, layerMask);
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
