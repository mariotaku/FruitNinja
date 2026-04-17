//
// ScreenButton — button descriptor for BaseScreen's lazy button system.
// See ScreenButton.h for struct layout and binary refs.
//
// Analysed: 2026-04-17T05:00
//

#include "ScreenButton.h"
#include "hud/MenuButton.h"
#include "hud/HUDControl.h"

// ===================================================================
// Matches ScreenButton::ControlDeleted @ 0x00130f40
// Called when the HUD removes the MenuButton. Nulls m_pButton and
// fires m_deletedCb. If m_bShrunk, restores fruit piece state.
// ===================================================================
void ScreenButton::ControlDeleted(HUDControl* ctrl) {
    if (m_pButton != (MenuButton*)ctrl) return;

    // TODO: if m_bShrunk && m_pButton->m_pFruitPiece:
    //   restore fruit vel/pos from saved state (see binary @ 0x00130f40)
    //   fruit->vel = DAT_00130fc8 (negated global)
    //   fruit->field_0xC8 = -10.0
    //   fruit->field_0x20 = -10.0

    if (m_deletedCb) {
        m_deletedCb((HUDControl*)m_pButton);
    }
    m_pButton = NULL;
}

// ===================================================================
// Matches ScreenButton::ShrinkButtonCall @ 0x001300f0
// Called when the fruit shrink animation fires. Saves current fruit
// pos/scale and applies the "shrink target" offscreen position.
// ===================================================================
void ScreenButton::ShrinkButtonCall() {
    if (!m_pButton) return;
    // TODO: save fruit piece pos into vel fields for ControlDeleted restore
    // TODO: apply shrink target Vec3 (DAT_00130150) to fruit offscreen
    m_bShrunk = 1;
}
