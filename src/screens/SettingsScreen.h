#ifndef FN_SETTINGS_SCREEN_H
#define FN_SETTINGS_SCREEN_H

//
// SettingsScreen : HUDControl3d
//
// Port specific: in-game settings modal. NO binary counterpart -- v1.6.1 ships
// no settings/options screen (OptionsScreen's widget stack -- CheckBox /
// SliderControl / ComboBox / ListBox / VerticalScroller -- is dead code with
// no live call site; see each widget's header). This screen is a
// port-improvement that uses the port-only src/ui/ widget toolkit
// (UiCheckbox/UiSlider/UiDropdown) to give the port an actual settings UI,
// wiring it to host-only globals (FN::g_MotionMode / FN::g_MotionSpeedThreshold
// / FN::g_ShowFps) and game_work.languageFlag. Not fidelity-constrained; no
// // ASM-verified markers apply to this class (it has no binary counterpart).
//
// Usage: `SettingsScreen* s = new SettingsScreen(); game_work.mHud->AddControl(s,
// false); s->Init();` -- AddControl the SCREEN ITSELF before calling Init().
// The screen itself is the only thing AddControl'd to game_work.mHud (plus
// m_pCloseButton, see below) -- the four in-plate widgets (LANGUAGE
// UiDropdown, MOTION MODE / FPS COUNTER UiCheckbox, SENSITIVITY UiSlider)
// are owned directly and are NOT AddControl'd: UiWidget.h documents that the
// owning screen must call each widget's Update()/Draw() itself, and doing so
// here lets the screen wrap them in a scrolled + glScissor'd viewport (see
// UpdateScroll()/Draw()) so the plate's content can overflow its visible
// window and be dragged, like a scroll pane. Widgets poll Mortar::Touch
// directly (TouchInRegion/IsTouchDown/game_work.m_FingerSpawnPos), so a
// direct Update() call delivers input identically to being AddControl'd --
// the HUD's modal input-capture gate only matters for controls it walks
// itself.
// m_pCloseButton stays AddControl'd (TOP_MOST) since it sits outside the
// plate (bottom-right of the screen) and never scrolls.
//
// Lifecycle: call the static SettingsScreen::Toggle() to open/close the modal
// -- it owns the single file-static instance pointer, the AddControl/Init on
// open, and the SetInputModal(NULL)+SetPendingRemoval on close. Both the ESC
// key (src/GameSDL.cpp), the MainScreen settings button (src/screens/
// MainScreen.cpp), and m_pCloseButton's tap (CloseCallback) call Toggle() so
// there is exactly one open/close path.
// Release() (called by the HUD removal sweep, and by the dtor) tears down
// m_pCloseButton via SetPendingRemoval (it is still AddControl'd -- HUD's own
// Update sweep deletes it) and directly `delete`s the four un-AddControl'd
// widgets (they were never linked into game_work.mHud's control list, so
// there is no dangling-pointer risk), plus the shared injected
// textures/font.
//
// Scrolling: the four widgets' `pos.y` is rewritten every Update() (base Y,
// captured once in Init(), plus the live scroll offset) BEFORE each widget's
// own Update() runs, so their own touch hit-rects/PollTouch track the
// scrolled position; Draw() reuses the same rewritten pos.y. See
// UpdateScroll() for the kinetic drag/fling/spring-back model (mirrors
// UiDropdown's own, same constants) and vertical-vs-horizontal drag
// disambiguation (a scroll only "steals" the touch from a widget once the
// drag is predominantly vertical -- see kScrollVerticalBias).
//

#include "hud/HUDControl3d.h"
#include "math/Vec3.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include <cstdint>
#include <string>
#include <vector>

class UiCheckbox;
class UiSlider;
class UiDropdown;
class BSButton;
namespace Mortar { class Font; }

class SettingsScreen : public HUDControl3d {
public:
    SettingsScreen();
    virtual ~SettingsScreen();

    void Init()    override;
    void Release() override;
    void Update(float dt) override;
    void Draw(float* hudScaleRaw) override;

    // Port specific: kinetic scroll for the plate's content viewport --
    // drag/fling/spring-back model mirroring UiDropdown::Update's tail (same
    // constants: SCROLL_FRICTION/DRAG_DELTA_FACTOR/SPRING_BACK_COEF/
    // SPRING_FWD_COEF/DRAG_CANCEL_DIST). Called first thing in Update(),
    // before the four widgets' own Update(). See the header's Scrolling note
    // above for the pos.y-rewrite contract this feeds.
    void UpdateScroll(float dt);

    int GetType() override { return 1; }

    // Port specific: single open/close path for the modal, shared by the ESC
    // key (src/GameSDL.cpp) and the MainScreen settings button
    // (src/screens/MainScreen.cpp). Owns the file-static instance pointer;
    // Toggle() opens (new + Init + AddControl + SetInputModal(this)) when
    // closed, or closes (SetInputModal(NULL) + SetPendingRemoval) when open.
    static void Toggle();
    static bool IsOpen();

private:
    UiDropdown* m_LangDrop;
    UiCheckbox* m_MotionCb;
    UiSlider*   m_SensSlider;
    UiCheckbox* m_FpsCb;

    // Port specific: modal close button, bottom-right of the plate. Built the
    // same way PauseScreen builds m_QuitButton (see PauseScreen::Update
    // @0x001a5ebc) -- a BSButton showing quit_title.tex (bomb-with-X icon)
    // plus a separate BakedStringBox text label offset onto the bomb, so it
    // reads identically to the pause screen's "QUIT" button. No binary
    // counterpart for SettingsScreen itself; the construction is mirrored for
    // visual consistency only.
    BSButton* m_pCloseButton;

    Mortar::SmartPtr<Mortar::Texture> m_Plate;      // dialog_box.tex
    Mortar::SmartPtr<Mortar::Texture> m_Backdrop;   // solid black, modal dim overlay (placeholder art)
    Mortar::SmartPtr<Mortar::Texture> m_CloseTex;   // quit_title.tex, held for m_pCloseButton

    // TTF font for the language UiDropdown (native language names need
    // CJK/Hangul/Cyrillic glyphs the bitmap font_fruit_ninja.fnt lacks).
    // fontstruetype/gangofchinese.ttf. Held here so it outlives the dropdown;
    // released in Release().
    Mortar::SmartPtr<Mortar::Font> m_LangFont;

    // Shared widget textures injected into each src/ui/ widget instance --
    // loaded once here (real staged art from assets/ui-widgets/*.svg via
    // fn_asset_staging) and handed to every widget's Set*Texture setter.
    // Held here so they outlive every widget that references them and are
    // released in Release().
    Mortar::SmartPtr<Mortar::Texture> m_TexBox;    // box.tex -- shared NineSlice background
    Mortar::SmartPtr<Mortar::Texture> m_TexCheck;  // check.tex -- checkbox tick glyph
    Mortar::SmartPtr<Mortar::Texture> m_TexCaret;  // caret.tex -- dropdown caret glyph
    Mortar::SmartPtr<Mortar::Texture> m_TexKnob;   // slider_will.tex -- slider knob
    Mortar::SmartPtr<Mortar::Texture> m_TexFade;   // list_fade.tex -- UiDropdown open-list edge fade (dark wood tone; dropdown panel only)
    Mortar::SmartPtr<Mortar::Texture> m_TexItem;   // list_item.tex -- UiDropdown row highlight gloss
    Mortar::SmartPtr<Mortar::Texture> m_TexDivider; // scratch_deviders.tex -- row-group separator
    // Note: the plate viewport's own top/bottom scroll-edge fade (see
    // DrawScrollFade in the .cpp) reuses m_Plate directly -- it redraws a
    // thin per-vertex-alpha-ramped strip of the SAME medbacking texels the
    // background plate already shows at that screen position, so it
    // composites 1:1 over the plate instead of needing its own texture.

    std::vector<std::string> m_LangItems;

    // Port specific: base (unscrolled) content-space Y of each of the four
    // in-plate widgets, captured once in Init() from the existing kXxxY
    // layout constants. Update() rewrites each widget's live pos.y = baseY +
    // (+m_ScrollY) every frame; Draw()'s labels/dividers apply the same
    // offset. Keeping these as instance fields (rather than re-reading the
    // file-static kXxxY constants at every call site) is just a naming
    // convenience -- the values ARE those constants, snapshotted once.
    float m_LangBaseY;
    float m_MotionCbBaseY;
    float m_SensBaseY;
    float m_FpsCbBaseY;

    // Port specific: kinetic scroll state for the plate's content viewport --
    // same drag/fling/spring-back model as UiDropdown's open-panel scroll
    // (src/ui/UiDropdown.cpp UiDropdown::Update tail), re-scoped to a whole
    // screen's worth of widgets instead of a dropdown's rows.
    //
    // Sign convention matches UiDropdown: m_ScrollY >= 0, 0 = content top
    // (nothing scrolled), +m_MaxScroll = content bottom (last row visible).
    // Draw offset is `off = +m_ScrollY` (increasing m_ScrollY shifts content
    // UP, revealing lower rows) -- same content-follows-finger convention,
    // same un-negated sign as UiDropdown's own `off`/`delta` (src/ui/
    // UiDropdown.cpp ~148).
    float m_ScrollY;
    float m_ScrollVel;
    float m_MaxScroll;         // computed once in Init(): max(0, contentH - viewportH)
    float m_AnchorScroll;      // m_ScrollY latched at touch-press
    Vec3  m_ScrollAnchorPos;   // finger (x, y) latched at touch-press
    int   m_ScrollTouchId;     // -1 = no touch owned/tracked by the scroll acquire scan
    uint8_t m_ScrollDragging;  // 1 once the held touch has committed to being a scroll (not a widget tap/drag)
    uint8_t m_ScrollOwnsTouch; // 1 once disambiguation has picked "scroll" for the CURRENT touch -- gates widget Update() out
    float m_ScrollDragDist;    // accumulated |dy| since press, mirrors UiDropdown's m_DragDist

    // Drag/fling/spring-back constants -- ported verbatim from UiDropdown
    // (src/ui/UiDropdown.h/.cpp), itself ported from ScrollingMenu
    // (src/hud/ScrollingMenu.cpp). See UiDropdown.h for the binary DAT
    // provenance of each value.
    static const float SCROLL_FRICTION;    // 0.9f  -- per-frame velocity decay
    static const float DRAG_DELTA_FACTOR;  // -0.5f -- damped-follow easing
    static const float SPRING_BACK_COEF;   // 0.75f -- spring multiplier past top
    static const float SPRING_FWD_COEF;    // 0.25f -- spring multiplier past bottom
    static const float DRAG_CANCEL_DIST;   // 5.0f  -- accumulated drag dist that cancels a tap
    // Port specific: no UiDropdown counterpart (a dropdown panel has no
    // sibling widgets to disambiguate against) -- scroll-vs-widget gate for
    // SettingsScreen's whole-plate drag: a held touch becomes a SCROLL only
    // once |dy| clears a small dead zone AND |dy| >= |dx| * this bias
    // (predominantly vertical). Until then the touch is left alone so a tap
    // or a SENSITIVITY slider's horizontal drag proceeds through the
    // widget's own PollTouch. 1.0f = no bias toward either axis beyond the
    // dead zone itself.
    static const float kScrollVerticalBias; // 1.0f
    static const float kScrollDeadZone;     // 4.0f -- |dy| before a held touch can become a scroll

    // Port specific: draws one horizontal scratch_deviders.tex separator
    // centred at the given row-space Y, spanning the left-column label edge
    // to the right-column widget edge. See DrawDivider's own comment (.cpp)
    // for the width/height derivation. Needs m_TexDivider (instance state),
    // so unlike DrawSettingsLabel/DrawSettingsDesc (file-static free
    // functions in the .cpp) this is a private method.
    void DrawDivider(float centerY);

public:
    // Delegate targets (Mortar::Delegate0<void> callees), installed on each
    // widget's SetOnChange in Init().
    void OnMotionToggle();
    void OnFpsToggle();
    void OnSensChanged();
    void OnLangChanged();

    // Port specific: m_pCloseButton's click callback. Runs the same
    // open/close path as Toggle() (SetInputModal(NULL) + SetPendingRemoval)
    // so tapping Close behaves exactly like the ESC key / settings gear.
    void CloseCallback();

    // Test-only: expose the dropdown so render tests can force it open
    // (UiDropdown::SetOpenForTest) without a synthetic touch sequence.
    UiDropdown* GetLangDropForTest() const { return m_LangDrop; }

    // Test-only: force the scroll pane to an arbitrary offset (clamped to
    // [0, m_MaxScroll], same range UpdateScroll()'s spring-back enforces) so
    // a render test can capture the scrolled-to-bottom state (FPS COUNTER
    // row) without a synthetic drag/fling touch sequence.
    void SetScrollForTest(float scrollY) {
        m_ScrollY = scrollY < 0.0f ? 0.0f : (scrollY > m_MaxScroll ? m_MaxScroll : scrollY);
    }
};

#endif // FN_SETTINGS_SCREEN_H
