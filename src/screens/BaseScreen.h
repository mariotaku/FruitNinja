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
//   DrawBorders(SmartPtr)       0x0015fcec / DrawBorders(BakedStringBox*) 0x0015f878
//   UpdateButtons    0x001602cc (lazy ScreenButton creation + update loop)
//   Release          0x00160d90 (marks HUD controls pending-removal)
//   Reset            0x00161860
//   RemoveButtons    0x00160ee8 (unconditional button teardown)
//
// BaseScreen is ABSTRACT: vtable slot 10 (Update) = __cxa_pure_virtual (0x360434).
// Concrete subclasses: DojoScreen, GameModeScreen, FruitFactPage (and its children).
// All other screens inherit HUDControl3d directly.
//

#include "hud/HUDControl3d.h"
#include "ScreenButton.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "math/_Vector3.h"
#include <list>

namespace Mortar { class BakedStringBox; }
class GenericHUDControl;

// DIFFERS: binary vtable slot 10 (Update) = __cxa_pure_virtual (0x360434) — BaseScreen is abstract.
// v1.6.1 ctor @ 0x16cd40; Release @ 0x160d90; Reset @ 0x161860; D0 @ 0x161bb0; D1 @ 0x161b20.
class BaseScreen : public HUDControl3d {
public:
    BaseScreen();
    virtual ~BaseScreen();

    // vtable slot 10 — PURE in binary (0x00360434 = __cxa_pure_virtual).
    // Every concrete subclass (DojoScreen, GameModeScreen, FruitFactPage and its
    // derived pages) provides its own Update override.
    virtual void Update(float dt) override = 0;

    // Static texture management — two global textures shared by all
    // BaseScreen subclasses (sml_title.tex + blurry_backing.tex).
    static void LoadContent();     // 0x001305cc
    static void UnloadContent();

    // DrawBorders — shade triangles + deco quad.
    // Binary has two overloads:
    //   DrawBorders(Mortar::SmartPtr<Mortar::Texture>, ...) @ v1.6.1 @0x0015fcec
    //   DrawBorders(Mortar::BakedStringBox*, ...) @ v1.6.1 @0x0015f878
    void DrawBorders(Mortar::SmartPtr<Mortar::Texture> secondaryTex,
                     float alpha, _Vector3<float> secondaryTexPos);

    // v1.6.1 BakedStringBox* overload @ 0x0015f878.
    // Draws shade triangles + sml_title deco (same geometry as SmartPtr overload,
    // no secondary texture). If box != nullptr, calls box->SetTranslation/Draw.
    // Returns the anchor Vec3 used to position the box.
    // ASM-verified lhs-rhs: v1.6.1 @0x15fc80
    _Vector3<float> DrawBorders(Mortar::BakedStringBox* box,
                                float alpha, _Vector3<float> arg3);

    // UpdateButtons v1.6.1 @0x001602cc — lazy ScreenButton creation + update.
    void UpdateButtons(float dt);

    // SetExtraControlsDefaultPos v1.6.1 @0x0015f618 — propagates this screen's
    // origin (pos) into every registered child control's m_BasePos2 (+0x1bc)
    // so they follow the page when the page is translated.
    void SetExtraControlsDefaultPos();

    // AddGenericControl — registers a GenericHUDControl in m_HUDControls
    // so Release() marks it pending-removal on teardown.
    void AddGenericControl(GenericHUDControl* ctrl);

    // Release @ 0x00130dd8 — marks m_HUDControls pending-removal,
    // clears list, disables ScreenButton MenuButtons.
    void Release() override;

    // RemoveButtons v1.6.1 @0x00160ee8 — unconditional: marks all
    // ScreenButton MenuButtons pending-removal.
    void RemoveButtons();

#ifndef __bada__
    // Port specific: see HUDControl::GetTransitionAlpha. Gates the desktop
    // ESC-as-back route while this screen is sliding in/out.
    float GetTransitionAlpha() const override { return m_TransitionAlpha; }
#endif

protected:
    // +0x7C: button descriptors. UpdateButtons iterates this each frame
    // to lazily create/update MenuButtons.
    // Binary: std::list<ScreenButton>. Port: same.
    std::list<ScreenButton> m_ScreenButtons;

    // +0x84: secondary HUD controls for Release() cleanup.
    // Binary: std::list<GenericHUDControl*> (proven by SetExtraControlsDefaultPos
    // iterating _List_iterator<GenericHUDControl*> and writing m_BasePos2 with
    // no cast). Port: matches.
    std::list<GenericHUDControl*> m_HUDControls;

#ifdef FN_TEST
public:
    // Port specific: test-only read accessor for m_HUDControls.
    // Used by test_sensei_teardown to snapshot child control pointers for
    // teardown verification (by pointer identity, not GetType(), since
    // GenericHUDControl::GetType()==1 is shared with many other controls).
    const std::list<GenericHUDControl*>& GetHUDControlsForTest() const { return m_HUDControls; }
protected:
#endif

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
