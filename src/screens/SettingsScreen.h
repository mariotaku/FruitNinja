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
// -- it owns the single file-static instance pointer. Open (s_pSettings ==
// NULL): new + AddControl + Init(), THEN starts the drop-in animation
// (m_AnimPhase = ANIM_OPENING, see below) -- the popup is laid out at rest
// but drawn/updated off-screen until the animation eases it in. Close
// (s_pSettings != NULL, phase == ANIM_OPEN): starts the drop-out animation
// (ANIM_CLOSING) instead of tearing down immediately; re-entrant close
// requests while already ANIM_CLOSING are ignored. The ACTUAL teardown
// (SetInputModal(NULL) + SetPendingRemoval + SaveSettings + the
// quit-on-language-change check) runs at the END of the CLOSING animation,
// in SettingsScreen::UpdateAnim() -- see its comment for why (deferred from
// Toggle() itself, same steps/order as before, just deferred). Both the
// MainScreen settings button (src/screens/MainScreen.cpp) and
// m_pCloseButton's tap (CloseCallback) call Toggle() so there is exactly one
// open/close path.
// Release() (called by the HUD removal sweep, and by the dtor) tears down
// m_pCloseButton via SetPendingRemoval (it is still AddControl'd -- HUD's own
// Update sweep deletes it) and directly `delete`s the four un-AddControl'd
// widgets (they were never linked into game_work.mHud's control list, so
// there is no dangling-pointer risk), plus the shared injected
// textures/font.
//
// Popup animation: AnimPhase (ANIM_OPENING/ANIM_OPEN/ANIM_CLOSING) +
// m_PopupOffsetY implement a drop-from-top-with-bounce open / slide-up close,
// with the backdrop dim fading in/out alongside. m_PopupOffsetY is a single
// world-space Y offset added to EVERYTHING the popup draws -- the plate, the
// glScissor clip band, labels/dividers/fade bands/scrollbar, and the four
// widgets' pos.y -- i.e. it translates the whole popup coordinate space
// vertically (see UpdateAnim()/Update()/Draw()). m_pCloseButton is OUTSIDE
// this coordinate space -- it never drops from the top with the plate.
// Instead it has its own bottom-right slide-in/out offset
// (m_CloseBtnOffX/Y), driven by the same AnimPhase/m_AnimTimer lifecycle
// (see m_CloseBtnOffX/Y field comment below + UpdateAnim()). Widget touch
// input (UpdateScroll() + the four widgets' own Update()) only runs while
// phase == ANIM_OPEN; HUD::SetInputModal(this) stays set for the entire
// opening+open+closing lifetime so the screen behind stays frozen
// throughout, not just while OPEN.
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

    // Port specific: popup open/close animation phase. OPENING/CLOSING drive
    // m_PopupOffsetY (see below) and gate widget input; only ANIM_OPEN accepts
    // touch. See .cpp UpdateAnim() for the easing/timing and Toggle()'s header
    // note for how this replaces the old immediate-open/immediate-close model.
    enum AnimPhase { ANIM_OPENING, ANIM_OPEN, ANIM_CLOSING };

    // Port specific: kinetic scroll for the plate's content viewport --
    // drag/fling/spring-back model mirroring UiDropdown::Update's tail (same
    // constants: SCROLL_FRICTION/DRAG_DELTA_FACTOR/SPRING_BACK_COEF/
    // SPRING_FWD_COEF/DRAG_CANCEL_DIST). Called first thing in Update(),
    // before the four widgets' own Update(). See the header's Scrolling note
    // above for the pos.y-rewrite contract this feeds.
    void UpdateScroll(float dt);

    // Port specific: advances m_AnimPhase/m_AnimTimer/m_PopupOffsetY/
    // m_BackdropAlpha each frame. Called first thing in Update(), before
    // UpdateScroll() and the widgets. See .cpp for the easing curves.
    void UpdateAnim(float dt);

    int GetType() override { return 1; }

    // Port specific: single open/close path for the modal, shared by the
    // MainScreen settings button (src/screens/MainScreen.cpp) and
    // m_pCloseButton's tap (CloseCallback). Owns the file-static instance
    // pointer; Toggle() opens (new + Init + AddControl + SetInputModal(this))
    // when closed, or closes (SetInputModal(NULL) + SetPendingRemoval, plus a
    // quit-to-apply-language-change check) when open.
    static void Toggle();
    static bool IsOpen();

    // Port specific: no binary counterpart. Toggle()'s close branch used to call
    // MainScreen::TriggerQuitFromSettings() SYNCHRONOUSLY, mid-teardown of this
    // modal (before SetInputModal(NULL)/SetPendingRemoval had taken effect for
    // the frame) -- QuitGamesCallback's m_State=STATE_QUIT_WAIT write landed, but
    // MainScreen::Update's teardown-order dependencies meant the quit sequence
    // never progressed. Toggle() now just sets this flag; MainScreen::Update
    // polls it once the modal has fully closed (s_pSettings==NULL and no input
    // modal owns the HUD) and fires the real quit trigger then. See
    // MainScreen::Update's poll block (src/screens/MainScreen.cpp).
    static bool s_QuitAfterClose;

    // Test-only: skip straight to ANIM_OPEN / offset 0 / full backdrop alpha
    // so render-test captures are deterministic without depending on how many
    // settle frames the harness happens to run (the open animation is
    // ~0.28s/~17 frames at 60fps, longer than the harness's usual settle
    // window). See test_settings_screen_render.cpp.
    void SetAnimOpenForTest();

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

    // Port specific: game_work.languageFlag captured at Init() time. Toggle()'s
    // close branch compares against the live value to detect a language change
    // and trigger the quit-to-apply-on-restart path (see .cpp).
    uint8_t m_InitialLanguageFlag;

    // Port specific: popup open/close animation state (see AnimPhase, .cpp
    // UpdateAnim()). m_PopupOffsetY (world/ortho Y units) is added to every
    // element the popup draws AND to the four in-plate widgets' pos.y -- i.e.
    // it translates the whole popup coordinate space vertically. +Y is up in
    // this centered ortho, so the popup starts ABOVE the screen (large
    // +offset) and animates down to 0 (rest). m_pCloseButton does NOT use
    // this offset -- see m_CloseBtnOffX/Y below.
    AnimPhase m_AnimPhase;
    float     m_AnimTimer;    // seconds elapsed in the current phase
    float     m_PopupOffsetY; // world units, added to popup Y everywhere (see above)
    float     m_BackdropAlpha; // 0..1, current backdrop dim fade (eased, not raw progress)

    // Port specific: m_pCloseButton is NOT part of the popup coordinate space
    // above -- it does not translate with m_PopupOffsetY (it never drops from
    // the top with the plate). Instead it has its own slide-in-from-bottom-
    // right-corner motion, driven by the SAME m_AnimPhase/m_AnimTimer
    // lifecycle: a 2D offset added on top of its fixed rest pos
    // (kCloseBtnX, kCloseBtnY), eased from an off-screen bottom-right start
    // (+X, -Y) to (0,0) on OPENING (ease-out-back, matching the plate's
    // bounce), and back out to the start on CLOSING (ease-in). See .cpp
    // UpdateAnim().
    float     m_CloseBtnOffX;
    float     m_CloseBtnOffY;

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

    // Port specific: relabels m_pCloseButton's baked text between "BACK" and
    // "QUIT" depending on whether game_work.languageFlag has diverged from
    // m_InitialLanguageFlag (i.e. whether closing the modal will trigger the
    // quit-to-apply-language-change path in Toggle()). BSButton has no
    // SetText -- its label is baked once into m_pLabelBox at Init() time --
    // so this reaches into m_pLabelBox->SetText() directly to rebake it.
    // Called once at the end of Init() and again from OnLangChanged() (the
    // only place languageFlag can change while the modal is open).
    void UpdateCloseButtonLabel();

public:
    // Delegate targets (Mortar::Delegate0<void> callees), installed on each
    // widget's SetOnChange in Init().
    void OnMotionToggle();
    void OnFpsToggle();
    void OnSensChanged();
    void OnLangChanged();

    // Port specific: m_pCloseButton's click callback. Calls Toggle() so
    // tapping Close runs the exact same close path (including the
    // quit-to-apply-language-change check) as the MainScreen settings gear.
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
