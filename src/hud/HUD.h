#ifndef FN_HUD_H
#define FN_HUD_H

#include "HUDControl.h"
#include <list>

struct Renderer;

// Matches original HUD class (~0x20 bytes)
// Manages a list of HUDControl* with layered draw
class HUD {
public:
    std::list<HUDControl*> controls;
    Vec3 scale;  // 6 floats in original, simplified to Vec3 + 1

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
    // Draws only controls whose layerMask matches
    void Draw(Renderer& r, int layerMask) {
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            HUDControl* ctrl = *it;
            if (ctrl->m_bActive && (layerMask & ctrl->m_LayerFlags)) {
                ctrl->PreDraw(r, scale);
                ctrl->Draw(r, scale, layerMask);
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

    // Matches HUD::Release (0x144c5c)
    void Release() {
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            delete *it;
        }
        controls.clear();
    }
};

#endif
