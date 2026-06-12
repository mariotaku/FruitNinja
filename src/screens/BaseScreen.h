#ifndef FN_BASE_SCREEN_H
#define FN_BASE_SCREEN_H

//
// BaseScreen : HUDControl3d (size = 0x94 / 148 bytes)
//
// Binary refs (v1.6.1):
//   Constructor      0x0016cd40
//   Destructor D0    0x00161bb0 (deleting)
//   Destructor D1    0x00161b20 (non-deleting)
//   LoadContent      0x001305cc (loads sml_title.tex + blurry_backing.tex)
//   DrawBorders      0x00130230 (shade triangles + deco quad + optional secondary tex)
//   UpdateButtons    0x00130ab4 (lazy ScreenButton creation + update loop)
//   Release          0x00160d90 (marks HUD controls pending-removal)
//   Reset            0x00161860
//   RemoveButtons    0x00130eb8 (unconditional button teardown)
//
// BaseScreen is ABSTRACT: vtable slot 10 (Update) = __cxa_pure_virtual (0x360434).
// Concrete subclasses: DojoScreen, GameModeScreen, FruitFactPage (and its children).
// All other screens inherit HUDControl3d directly.
//

#include "hud/HUDControl3d.h"
#include "ScreenButton.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "math/Vec3.h"
#include <list>

// DIFFERS: binary vtable slot 10 (Update) = __cxa_pure_virtual (0x360434) — BaseScreen is abstract.
// v1.6.1 ctor @ 0x16cd40; Release @ 0x160d90; Reset @ 0x161860; D0 @ 0x161bb0; D1 @ 0x161b20.
class BaseScreen : public HUDControl3d {
public:
    BaseScreen();
    virtual ~BaseScreen();

    // vtable slot 10 — PURE in binary (0x00360434 = __cxa_pure_virtual).
    // Every concrete subclass (DojoScreen, GameModeScreen, FruitFactPage and its
    // derived pages) provides its own Update override.
    virtual void Update(float dt) = 0;

    // Static texture management — two global textures shared by all
    // BaseScreen subclasses (sml_title.tex + blurry_backing.tex).
    static void LoadContent();     // 0x001305cc
    static void UnloadContent();

    // DrawBorders @ 0x00130230 — shade triangles + deco quad.
    void DrawBorders(const Mortar::SmartPtr<Mortar::Texture>& secondaryTex,
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
    static Mortar::SmartPtr<Mortar::Texture> s_TexSmlTitle;       // slot +0: sml_title.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexBlurryBacking;  // slot +4: blurry_backing.tex

#ifdef __bada__
    friend struct BaseScreenLayoutAssert;
#endif
};

#ifdef __bada__
#include <cstddef>
struct BaseScreenLayoutAssert {
    static_assert(sizeof(BaseScreen)                       == 0x94, "BaseScreen size mismatch");
    static_assert(offsetof(BaseScreen, m_ScreenButtons)    == 0x7c, "m_ScreenButtons offset");
    static_assert(offsetof(BaseScreen, m_HUDControls)      == 0x84, "m_HUDControls offset");
    static_assert(offsetof(BaseScreen, m_TransitionAlpha)  == 0x8c, "m_TransitionAlpha offset");
    static_assert(offsetof(BaseScreen, m_State)            == 0x90, "m_State offset");
};
#endif

#endif
