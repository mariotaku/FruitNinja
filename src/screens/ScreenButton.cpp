//
// ScreenButton — button descriptor for BaseScreen's lazy button system.
// See ScreenButton.h for struct layout and binary refs.
//
// Analysed: 2026-04-17T06:00
//

#include "ScreenButton.h"
#include "hud/MenuButton.h"
#include "hud/HUDControl.h"
#include "entities/Fruit.h"

// g_slideVec = Vec3(0, 1, 0) — from _GLOBAL__I_BaseScreen.cpp @ 0x00130694
static const Vec3 SLIDE_VEC(0.0f, 1.0f, 0.0f);

// DAT_00130fbc = -480.0 — off-screen Y for fruit teleport in ControlDeleted
static const float OFFSCREEN_Y = -480.0f;

// 0xC1200000 = -10.0 — downward velocity applied in ControlDeleted
static const float FLING_VEL   = -10.0f;

// ===================================================================
// Matches ScreenButton::ShrinkButtonCall @ 0x001300f0
//
// Called when the fruit shrink animation fires on the MenuButton.
// Saves current fruit pos into m_SecondPos (backup for ControlDeleted),
// then zeros vel, m_SecondVel, and m_Gravity to freeze the fruit in
// place while the shrink visual plays.
//
// The shrink target Vec3 is at GOT+0x73EC → BSS (zero-initialized).
// = Vec3(0, 0, 0) — stops all motion.
// ===================================================================
void ScreenButton::ShrinkButtonCall() {
    if (!m_pButton || !m_pButton->m_pFruitPiece) return;

    Fruit* fruit = m_pButton->m_pFruitPiece;

    // Save current pos → m_SecondPos (binary: +0xb8 = m_HalfB_pos)
    fruit->m_SecondPos = fruit->pos;

    m_bShrunk = 1;

    // Freeze: vel = m_SecondVel = m_Gravity = (0, 0, 0)
    // Binary reads from a BSS global (GOT+0x73EC), zero-initialized.
    Vec3 shrinkTarget(0.0f, 0.0f, 0.0f);
    fruit->vel        = shrinkTarget;
    fruit->m_SecondVel = shrinkTarget;
    fruit->m_Gravity   = shrinkTarget;
}

// ===================================================================
// Matches ScreenButton::ControlDeleted @ 0x00130f40
//
// Called when the HUD removes the MenuButton. If the fruit was shrunk,
// restores it to a "fling off screen" state:
//   - pos.y = m_SecondPos.y = -480 (below screen)
//   - m_Gravity = -g_slideVec = (0, -1, 0)
//   - m_SecondVel.y = -10, vel.y = -10
// Then fires m_deletedCb and nulls m_pButton.
// ===================================================================
void ScreenButton::ControlDeleted(HUDControl* ctrl) {
    if (m_pButton != (MenuButton*)ctrl) return;

    if (m_bShrunk && m_pButton->m_pFruitPiece) {
        Fruit* fruit = m_pButton->m_pFruitPiece;

        // Teleport both halves off screen (DAT_00130fbc = -480.0)
        fruit->pos.y        = OFFSCREEN_Y;
        fruit->m_SecondPos.y = OFFSCREEN_Y;

        // Set gravity to -g_slideVec = (0, -1, 0) — gentle downward
        fruit->m_Gravity = Vec3(-SLIDE_VEC.x, -SLIDE_VEC.y, -SLIDE_VEC.z);

        // Fling velocities (0xC1200000 = -10.0)
        // Binary @ 0x00130fa0: writes -10 to fruit+0xc8 = m_SecondVel.y
        fruit->m_SecondVel.y = FLING_VEL;  // binary: *(fruit + 0xC8)
        fruit->vel.y         = FLING_VEL;  // binary: *(fruit + 0x20)
    }

    if (m_deletedCb) {
        m_deletedCb((HUDControl*)m_pButton);
    }
    m_pButton = nullptr;
}
