#ifndef FN_HUD_H
#define FN_HUD_H

// Analysed: 2026-04-30T00:00

#include "HUDControl.h"
#include "MissControl.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include <list>

// Matches original HUD class (0x24 bytes)
// See docs/structs/hud-class.md for layout and function list.
class HUD {
public:
    // +0x00: control list (8 bytes on this libstdc++ build)
    std::list<HUDControl*> controls;

    // +0x08..+0x1F: six tint multipliers, all init 1.0f.
    //   scales[0..2] = gameplay-mutable tint window (passed to HUDControl::Draw)
    //   scales[3..5] = world tint window (passed to SplatEntity::DrawSplat)
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x00144a90 (HUD::Draw)
    float scales[6];

    // +0x20: written 1.0f by HUD::Update start (vstr.32 s15,[r4,#0x20]).
    // Semantics TBD — not initialised by ctor, only written by Update.
    // TODO: identify readers; may be a per-frame tint accumulator.
    float field_0x20;

    HUD() : field_0x20(0.0f) {
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

    // Matches HUD::Draw (0x00144a90)
    // Per-control matrix discipline: reset the world matrix between controls
    // so each control inherits identity. The binary's HUD::Draw has the same
    // discipline (e.g. ShopScreen::Draw ends with SetCurrentMatrix(Scale44(
    // 481, 321, 0)) for the BG_store quad and that scale must NOT bleed into
    // the next control's first draw call). Without this, e.g. ShopListItem's
    // Font::DrawString sees a 481x scale already in the stack and glyph
    // quads land thousands of pixels off-screen.
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x00144a90 (HUD::Draw)
    void Draw(int layerMask) {
        Vec3 hudScale(scales[0], scales[1], scales[2]);
        const Vec3 identityScale(1.0f, 1.0f, 1.0f);
        Mortar::MatrixStack& world = Mortar::MatrixManager::GetInstance().GetWorldStack();
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            HUDControl* ctrl = *it;
            if (ctrl->m_bActive && (layerMask & ctrl->m_LayerFlags)) {
                // field_0x60 != 0 (default): receives gameplay-mutable tint window.
                // field_0x60 == 0: receives identity (1,1,1) — opted out of tint.
                const Vec3& scaleVec = ctrl->field_0x60 ? hudScale : identityScale;
                world.Reset();
                ctrl->PreDrawOrder(scaleVec, layerMask);
                world.Reset();
                ctrl->DrawOrder(scaleVec, layerMask);
            }
        }
    }

    // Matches HUD::Update (0x00144d20)
    void Update(float dt) {
        MissControl::PreUpdate(dt);  // global combo-decay pre-tick
        field_0x20 = 1.0f;          // reset per-frame field (vstr.32 s15,[r4,#0x20])

        for (auto it = controls.begin(); it != controls.end(); ) {
            HUDControl* ctrl = *it;
            if (ctrl->m_bActive)
                ctrl->Update(dt);

            ctrl = *it;
            if (ctrl->m_bPendingRemoval) {
                if (ctrl->m_RemoveCallback)
                    ctrl->m_RemoveCallback(ctrl);
                ctrl = *it;
                if (!ctrl->m_bNoDestructor) {
                    delete ctrl;
                    *it = nullptr;
                }
                it = controls.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Matches HUD::OnPause (0x00144c00)
    // Binary dispatches GetType() on every control (regardless of m_bActive)
    // and only calls ScrollingMenu::ClearTouch when GetType() == 8.
    void OnPause() {
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            if ((*it)->GetType() == 8 /* TYPE_SCROLLING_MENU */) {
                // TODO: ScrollingMenu::ClearTouch when ScrollingMenu is fully
                // ported. For now this is a no-op stub.
                // static_cast<ScrollingMenu*>(*it)->ClearTouch();
            }
        }
    }

    // Matches HUD::Release (0x00144c5c)
    // Binary fires m_RemoveCallback + deleting-dtor only when m_bNoDestructor == 0.
    // Controls with m_bNoDestructor != 0 are left untouched (HUD does not own them).
    // TODO: set GameData[+0x34] in-Release guard when GameData struct is ported.
    void Release() {
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            HUDControl* ctrl = *it;
            if (!ctrl->m_bNoDestructor) {
                if (ctrl->m_RemoveCallback)
                    ctrl->m_RemoveCallback(ctrl);
                ctrl = *it;
                if (ctrl != nullptr) {
                    delete ctrl;
                    *it = nullptr;
                }
            }
        }
        controls.clear();
    }
};

#endif
