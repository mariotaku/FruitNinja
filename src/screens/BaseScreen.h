#ifndef FN_BASE_SCREEN_H
#define FN_BASE_SCREEN_H

//
// BaseScreen : HUDControl3d (size = 0x94 / 148 bytes)
//
// Binary refs:
//   Constructor      0x00138dc0
//   Destructor       0x00131740 (deleting), 0x00138d60 (non-deleting)
//   LoadContent      0x001305cc (loads sml_title.tex + blurry_backing.tex)
//   DrawBorders      0x00130230 (shade triangles + deco quad + optional secondary tex)
//   UpdateButtons    0x00130ab4 (lazy ScreenButton creation + update loop)
//   Release          0x00130dd8 (marks HUD controls pending-removal)
//   RemoveButtons    0x00130eb8 (unconditional button teardown)
//
// Intermediate base for screens that use the ScreenButton system and
// shade-triangle borders. Only DojoScreen and GameModeScreen inherit
// from BaseScreen. Other screens inherit HUDControl3d directly.
//

#include "hud/HUDControl3d.h"
#include "ScreenButton.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "math/Vec3.h"
#include <list>

class BaseScreen : public HUDControl3d {
public:
    BaseScreen();
    virtual ~BaseScreen();

    // Static texture management — two global textures shared by all
    // BaseScreen subclasses (sml_title.tex + blurry_backing.tex).
    static void LoadContent();     // 0x001305cc
    static void UnloadContent();

    // DrawBorders @ 0x00130230 — shade triangles + deco quad.
    void DrawBorders(const SmartPtr<Mortar::Texture>& secondaryTex,
                     float alpha, const Vec3& secondaryTexPos);

    // UpdateButtons @ 0x00130ab4 — lazy ScreenButton creation + update.
    void UpdateButtons(float dt);

    // AddGenericControl — registers a HUDControl in m_HUDControls
    // so Release() marks it pending-removal on teardown.
    void AddGenericControl(HUDControl* ctrl);

    // Release @ 0x00130dd8 — marks m_HUDControls pending-removal,
    // clears list, disables ScreenButton MenuButtons.
    void Release() override;

    // RemoveButtons @ 0x00130eb8 — unconditional: marks all
    // ScreenButton MenuButtons pending-removal.
    void RemoveButtons();

protected:
    // +0x7C: button descriptors. UpdateButtons iterates this each frame
    // to lazily create/update MenuButtons.
    // Binary: std::list<ScreenButton>. Port: same.
    std::list<ScreenButton> m_ScreenButtons;

    // +0x84: secondary HUD controls for Release() cleanup.
    // Binary: std::list<HUDControl*>. Port: matches.
    std::list<HUDControl*> m_HUDControls;

    // +0x8C: transition alpha (lerped 0->1 on entry, decayed on exit)
    float m_TransitionAlpha;

    // +0x90: state machine index
    int m_State;

    // Static textures (binary: GOT-relative globals, module-level singletons)
    static SmartPtr<Mortar::Texture> s_TexSmlTitle;       // slot +0: sml_title.tex
    static SmartPtr<Mortar::Texture> s_TexBlurryBacking;  // slot +4: blurry_backing.tex
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(BaseScreen)                       == 0x94, "BaseScreen size mismatch");
static_assert(offsetof(BaseScreen, m_ScreenButtons)    == 0x7c, "m_ScreenButtons offset");
static_assert(offsetof(BaseScreen, m_HUDControls)      == 0x84, "m_HUDControls offset");
static_assert(offsetof(BaseScreen, m_TransitionAlpha)  == 0x8c, "m_TransitionAlpha offset");
static_assert(offsetof(BaseScreen, m_State)            == 0x90, "m_State offset");
#endif

#endif
