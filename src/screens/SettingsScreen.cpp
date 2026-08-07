//
// SettingsScreen -- Port specific in-game settings modal (see header note).
// Binds the port-only src/ui/ widget toolkit (UiDropdown/UiCheckbox/UiSlider)
// to host-only globals. No binary counterpart; not fidelity-constrained.
//

#include "SettingsScreen.h"
#include "ui/UiCheckbox.h"
#include "ui/UiSlider.h"
#include "ui/UiDropdown.h"
#include "hud/BSButton.h"
#include "hud/WidgetPlaceholderArt.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "game/GameWork.h"
#include "game/SettingsSave.h"
#include "screens/MainScreen.h"
#include "engine/util/Localisation.h"
#include "engine/util/Delegate.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "render/Utf8StringIterator.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "render/NineSlice.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "debug/DebugFlags.h"
#include "util/StringTable.h"
#include "render/BakedStringBox.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "engine/input/Touch.h"
#include "render/Renderer.h"
#include "render/Layout.h"
#include "Game.h"

#include <cmath>

using namespace fn_widget_art;

// ---------------------------------------------------------------------------
// Language display list in languageFlag order. Same list as the interactive
// harness (tests/test_settings_interactive.cpp kLanguageNames). Only 0..13
// have shipped .str data; higher flags fall back to english_us inside
// Localisation::Load.
//
// Native names, rendered with the TTF font (fontstruetype/gangofchinese.ttf,
// see m_LangFont) which covers Latin (incl. lowercase/accents), CJK, Hangul,
// and Cyrillic. gangofchinese.ttf has no Arabic glyphs, so entry 20 falls
// back to the English word "Arabic" rather than Arabic script.
// Port-improvement screen, no fidelity constraint on display casing/script.
// Keep in sync with NATIVE_LANG_NAMES in tools/assets/stage-assets.py
// (web font-subset charset).
//
// The binary's langId 21 ("Debug") is StringTableUtilLoadStringsTable's
// literal "fake debug language" (@0x2815c5) -- a dev-only entry unreachable
// via the binary's own locale detection (GetLanguage @0x001eebec caps at
// langId 0x14/20). This picker is entirely port-invented (v1.6.1 has no
// language UI; locale is auto-detected), so Debug is excluded rather than
// exposed as a user-selectable option.
// ---------------------------------------------------------------------------
static const char* const kLanguageNames[] = {
    "English (US)", "English (UK)", "Fran\303\247ais", "Espa\303\261ol", "Deutsch", "Italiano",
    "Nederlands", "Svenska", "Dansk", "Norsk", "Suomi", "\355\225\234\352\265\255\354\226\264",
    "\346\227\245\346\234\254\350\252\236", "\344\270\255\346\226\207", "\347\271\201\351\253\224\344\270\255\346\226\207", "Espa\303\261ol (LA)", "Polski",
    "Portugu\303\252s (PT)", "Portugu\303\252s (BR)", "\320\240\321\203\321\201\321\201\320\272\320\270\320\271", "Arabic"
};
static const int kLanguageCount = 21;

static int ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Panel text colour -- reuses game_work.m_TitleColour (GameWork+0x6a0), the
// brown Zen-plate colour PreloadRings @0x0011cd44 sets (RGB 0x6F,0x46,0x1E).
// Same constant MainScreen uses for "SLICE FRUIT TO BEGIN" and AboutScreen/
// PauseScreen use for their wooden-panel body text -- one source of truth
// rather than a re-guessed brown literal.
static const Colour& SettingsTextColour() {
    return game_work.m_TitleColour;
}

// ---------------------------------------------------------------------------
// Layout constants (game space, ortho x[-240,240] y[-160,160]).
// ---------------------------------------------------------------------------
// Port specific: shared left margin for every left-column label -- all four
// (Language / Motion Mode / Sensitivity / FPS Counter) left-align from this
// same x instead of each being individually centred on its row, which left
// their left edges ragged (each string's own width shifted its visual start).
// Chosen just inside the plate's content left bound (-220 + kPlateDestBorderX
// 44 = -176, see kPlateHalfW note below) with a small margin, mirroring
// kRightEdge's margin on the opposite side.
static const float kLabelX = -168.0f;
// Port specific: SENSITIVITY alone nudges slightly right (indented) of the
// other three labels -- visually balances against its wider row content
// (the slider track) the way the other rows' shorter widgets don't need.
static const float kSensLabelX = kLabelX + 11.0f;
// Label font scale (Font::DrawString's scale param). Labels draw with the
// TTF font (m_LangFont, gangofchinese.ttf) rather than the bitmap
// font_fruit_ninja.fnt -- the bitmap face is CAPS-ONLY (no lowercase
// glyphs), and the description line under MOTION MODE needs lowercase, so
// both labels and description share the TTF for one consistent look.
// gangofchinese.ttf's Latin glyphs are the Gang of Three display face,
// which is itself caps-only (no true lowercase forms -- lowercase input
// renders as small caps), so the label strings stay written in caps.
// The TTF's glyph metrics render lighter/smaller than the bitmap face at
// the same nominal size. 19.0f (up from an earlier 17.0f pass) fills the
// left column more confidently -- slightly above kComboTextScale (18.0f,
// the collapsed LANGUAGE dropdown's value text) since the left labels are
// the primary row heading and can afford to read a touch bolder/larger
// than the right-column widget text, without yet crowding the widgets.
static const float kLabelScale = 19.0f;

// Port specific: SCROLLABLE layout -- content height is allowed to (and
// does) EXCEED the plate's visible viewport; SettingsScreen wraps the four
// widgets + labels + dividers in a scrolled + glScissor'd viewport (see
// UpdateScroll()/Draw()) rather than compressing everything to fit, like the
// old non-scrolling layout did. Row Ys below are laid out top-to-bottom in
// CONTENT space (i.e. the position each element sits at when m_ScrollY==0,
// which shows the content TOP flush with the viewport top) using a running
// cursor, not solved backward from a fixed content half-height.
//
// Divider clearance: each divider (kDividerHeight 15 tall, see DrawDivider)
// gets a DEDICATED kDividerPad(10)-unit gap from the ADJACENT WIDGET BOX
// EDGE on each side -- e.g. Divider 1 sits 10 units below the LANGUAGE
// dropdown bar's own bottom edge (kComboY - kComboScaleY*0.5), and 10 units
// above the MOTION MODE label's top. This is real widget-box clearance, not
// a label-baseline gap (the old layout measured from label baselines only,
// which is why it never overflowed the plate -- widget boxes are taller
// than a text line). MOTION MODE's row is the tallest allocation (label +
// 2-line description + SENSITIVITY, 4 lines total) laid out with uniform
// kRowLineGap spacing between each of its 4 lines -- unchanged internal
// spacing, no divider inside this block.
//
// kContentPad is the shared top-of-content / bottom-of-content padding
// (above LANGUAGE's dropdown box, below FPS COUNTER's checkbox box). Top and
// bottom padding are otherwise symmetric -- kContentTop is solved from
// kContentTopPad alone -- except the bottom gets an extra
// kContentBottomFadeClearance (see below) so FPS COUNTER's own box clears
// DrawScrollFade's bottom band at max scroll.
static const float kRowLineGap  = 24.0f;
static const float kDividerPad  = 10.0f;
static const float kDividerHeight = 15.0f;  // see DrawDivider
static const float kContentPad = 12.0f;
static const float kContentTopPad = kContentPad;
// At m_ScrollY == m_MaxScroll, the content-space screen Y of the FPS box's
// own bottom edge works out to (kContentBottomPad - kContentTopPad -
// kViewportHalfH) -- see kContentBottom/kContentH/m_MaxScroll derivation
// below. With kContentBottomPad == kContentTopPad that lands EXACTLY at
// -kViewportHalfH, i.e. flush with the viewport bottom -- which is also
// where DrawScrollFade's bottom band (kFadeHeight == 10, see Draw()) starts,
// so the FPS box's bottom sliver dissolves into the fade. Padding the
// bottom by kFadeHeight plus a small clearance margin pushes the FPS box's
// bottom edge up above the fade band's own top edge, leaving it fully
// legible at max scroll instead of dissolving into the fade.
static const float kContentBottomFadeClearance = 14.0f;  // kFadeHeight(10) + 4 margin
static const float kContentBottomPad = kContentPad + kContentBottomFadeClearance;
// kPlateHalfH(130) - kPlateDestBorderY(29) = 101 is the plate's exact
// 9-slice content-cell edge (see kPlateHalfH note below): NineSlice::Draw's
// centre-cell quad is a hard geometric seam there, with the border cell
// (beveled wooden frame art) starting immediately past it. kViewportHalfH
// MUST stay inside that seam -- DrawScrollFade's opaque outer edge is drawn
// exactly AT kViewportHalfH (see its cellTop/edgeY use below), so parking
// the constant AT 101 would land that opaque edge pixel-exactly on the
// seam; 1 world unit (2 HD texels, see kPlateHalfW note's "1 world unit =
// 2 texels") of headroom keeps the fully-opaque fade pixels on parchment,
// never on the bevel.
static const float kViewportHalfH = 100.0f; // kPlateHalfH(130) - kPlateDestBorderY(29) = 101 seam, minus 1-unit fade safety margin

// LANGUAGE: dropdown bar's top edge sits kContentTopPad below the viewport
// top; kComboY (bar centre) follows, kLangLabelY keeps the existing -2
// downward-nudge convention (label baseline sits 2 above the bar centre --
// see kComboY's original note) so the label still visually centres on the
// taller bar.
// Port specific: kRightEdge is the shared right edge (x) every right-column
// widget's visible art aligns to, so their right edges line up in a column
// instead of each widget's own centre-anchored x drifting independently.
// Chosen just inside the plate's content right bound (~+178, see kPlateHalfW
// note below) with a small margin.
//
// Unlike the old resurrected-binary-widget layout, the src/ui/ toolkit's hit
// rect and drawn box are BOTH exactly pos +/- half-extent (UiWidget::SetSize)
// -- no transparent-padding / stretched-texture quirks to back-solve around.
// Each widget's own visible right edge is therefore just pos.x + halfWidth,
// where halfWidth is the widget's own w/2 (checkbox side, slider track,
// dropdown bar):
//   UiCheckbox:  right edge = pos.x + side*0.5f            (DrawBox(side,side))
//   UiSlider:    right edge = pos.x + trackW*0.5f           (DrawBox(trackW,trackH);
//                the knob's travel is clamped to [0, trackW-knobD] so its
//                right edge never exceeds pos.x + trackW*0.5f, see
//                UiSlider::ComputeKnobX)
//   UiDropdown:  right edge = pos.x + barW*0.5f              (DrawBox(barW,barH);
//                the caret glyph is clamped inside the bar, see
//                UiDropdown::Draw's caretHalf clamp -- it never overhangs)
static const float kRightEdge = 175.0f;

// UiDropdown bar. Wider than the other controls + same height as the checkbox
// (kCheckboxSide) so it reads as a chunky field. x is centre of the bar;
// back-solved so the bar's right edge (pos.x + kComboScaleX*0.5f) == kRightEdge.
static const float kComboScaleX =  150.0f, kComboScaleY =   36.0f;
static const uint8_t kComboVisibleRows = 6;
// Combo value/row text scale (Font::DrawString's scale param, font-native
// pixel size) -- fits the longest native name -- "PORTUGUES (BR)" -- without
// spilling into the caret.
static const float kComboTextScale = 18.0f;
// UiCheckbox: side 36; x back-solved so the box's right edge
// (pos.x + side*0.5f) == kRightEdge.
static const float kCheckboxSide = 36.0f;
// Motion Mode description -- smaller, dimmer TWO-line caption drawn under
// the MOTION MODE label (same kLabelX left margin). Manually split at the
// natural comma break ("Slow move aims, fast flick cuts" / "(pointer
// only)") rather than word-wrapped, since Font has no wrap-measurement path
// wired for this TTF path (see FindAdvanceOfNextWord TODO in Font.h). Only
// this row gets a sub-description; the other three rows stay single-line.
static const float kMotionDescScale = 11.0f;
// Port specific: gap between the two description lines only -- tighter than
// kRowLineGap. The label->desc0 gap looks visually tighter than a plain
// numeric kRowLineGap because the label glyphs are drawn at kLabelScale (19)
// vs the desc lines' much smaller kMotionDescScale (11), so the label's
// larger glyph height eats into that gap while both desc lines (same small
// scale) left the full 24 units open, reading as loose. Tighten just the
// desc0->desc1 gap so the two-line caption reads as one visual block.
static const float kMotionDescLineGap = 12.6f;
// Dimmer than SettingsTextColour()'s brown (0x6F,0x46,0x1E) -- same hue,
// lower alpha so it reads as secondary/caption text under the bold label.
static const Colour kMotionDescColour(0x6F, 0x46, 0x1E, 0xA0);
// UiSlider track -- wide, thin horizontal groove (NineSlice box.tex).
static const float kSensTrackW = 120.0f, kSensTrackH = 16.0f;
static const int   kSensMin = 0, kSensMax = 100;

// ---------------------------------------------------------------------------
// Content-space Y layout -- a top-to-bottom running cursor, NOT solved
// backward from a fixed content half-height (the plate's viewport is now
// smaller than the content, by design -- see UpdateScroll()/Draw()). Each
// step advances the cursor by the PREVIOUS element's own half-height (a real
// widget-box edge) plus a fixed gap, not by a shared uniform row height.
//
// kContentTopPad below the viewport top (kViewportHalfH) is the dropdown
// bar's TOP edge; kComboY (bar centre) and kLangLabelY (label, kept at the
// bar centre +2 -- the same small downward-nudge convention the old layout
// used so the label visually centres on the taller bar) follow from it.
//
// Port specific: on FRUIT_PLATFORM_WII the LANGUAGE row (dropdown bar +
// Divider 1) is compiled out entirely -- see m_LangDrop's header comment --
// so MOTION MODE becomes the first content row instead. kComboY/kLangLabelY/
// kDividerY1 stay defined unconditionally (Draw()/Init() skip using them on
// Wii, see their own `#if !defined(FRUIT_PLATFORM_WII)` guards below) so the
// non-Wii formulas above are untouched; only kMotionLabelY's derivation
// branches.
// ---------------------------------------------------------------------------
static const float kComboY      = kViewportHalfH - kContentTopPad - kComboScaleY * 0.5f;
static const float kComboX      = kRightEdge - kComboScaleX * 0.5f;
static const float kLangLabelY  = kComboY + 2.0f;

// Divider 1: kDividerPad below the dropdown bar's own BOTTOM edge (a real
// widget-box edge, not the label baseline).
static const float kDividerY1 = (kComboY - kComboScaleY * 0.5f) - kDividerPad - kDividerHeight * 0.5f;

#if !defined(FRUIT_PLATFORM_WII)
// Motion Mode row: TALLEST allocation -- label, then a clear gap, then a
// 2-line description, then SENSITIVITY, all spaced kRowLineGap apart (the
// same uniform line spacing used throughout this 4-line block; no divider
// inside this block, per design). kMotionLabelY sits kDividerPad below
// Divider 1's own bottom edge.
static const float kMotionLabelY = (kDividerY1 - kDividerHeight * 0.5f) - kDividerPad;
#else
// Port specific: Wii reflow -- LANGUAGE is hidden, so MOTION MODE's label
// becomes the first content row: its top edge sits kContentTopPad below the
// viewport top, the SAME "content top" anchor kComboY's own derivation uses
// for the dropdown bar's top edge (kViewportHalfH - kContentTopPad), just
// with no widget-box half-height term to subtract since a label line has no
// box of its own (mirrors how kWideScreenLabelY/kNativeFpsLabelY/
// kFpsLabelY -- the other single-line label rows -- also anchor directly off
// a preceding element's edge with no extra half-height term for themselves).
static const float kMotionLabelY = kViewportHalfH - kContentTopPad;
#endif
static const float kMotionDescY0    = kMotionLabelY - kRowLineGap;
static const float kMotionDescY1    = kMotionDescY0 - kMotionDescLineGap;

// kMotionCbY: vertically centred on the MOTION MODE label + 2-line
// description sub-block (kMotionLabelY..kMotionDescY1).
static const float kMotionCbX = kRightEdge - kCheckboxSide * 0.5f;
static const float kMotionCbY = (kMotionLabelY + kMotionDescY1) * 0.5f;

// SENSITIVITY is the 4th and last line of Motion Mode's block -- kRowLineGap
// below the description's 2nd line, same spacing as every other step in
// this block.
static const float kSensLabelY = kMotionDescY1 - kRowLineGap;
// UiSlider: x is centre of the track; back-solved so the track's right edge
// (pos.x + trackW*0.5f) == kRightEdge, same pattern every other
// right-column widget uses (checkbox, combo bar). Centred on the
// SENSITIVITY label alone.
static const float kSensX      =  kRightEdge - kSensTrackW * 0.5f, kSensY      = kSensLabelY;

// Divider 1b: NEW group divider right after the MOTION MODE block (label +
// 2-line description + SENSITIVITY) -- same kDividerPad clearance off the
// SENSITIVITY track's own BOTTOM edge that every other inter-group divider
// uses (see kDividerY1/kDividerY2's own "real widget-box edge" convention).
// WIDESCREEN gets its own one-row group immediately below this divider,
// ahead of NATIVE FRAME RATE / FPS COUNTER (which shift down by one row +
// one divider's worth of space, see kDividerY2 below).
static const float kDividerY1b = (kSensY - kSensTrackH * 0.5f) - kDividerPad - kDividerHeight * 0.5f;

// WIDESCREEN: its own single-row group, right after MOTION MODE (moved down
// from its old spot below FPS COUNTER -- see Layout::WideLayoutRestartPending,
// SettingsScreen.h). Structurally identical to the FPS COUNTER / NATIVE FRAME
// RATE rows (single-line, no sub-description) -- so it uses the SAME full
// kRowLineGap clearance off Divider 1b's own bottom edge that those two rows
// use, not the tighter kDividerPad (kDividerPad is only for a divider's gap
// to a MULTI-line block's first label, e.g. kMotionLabelY off kDividerY1 --
// see the kNativeFpsLabelY comment on why an isolated single-line row needs
// the wider gap instead).
static const float kWideScreenLabelY = (kDividerY1b - kDividerHeight * 0.5f) - kRowLineGap;
static const float kWideScreenCbX    = kRightEdge - kCheckboxSide * 0.5f, kWideScreenCbY = kWideScreenLabelY;

#if defined(FRUIT_PLATFORM_WII)
// Port specific: LETTERBOX -- Wii-only row, immediately below WIDESCREEN
// (same single-row/no-description shape), gated FRUIT_PLATFORM_WII because
// host/web fit-viewport behaviour is unconditional (Layout::g_Letterbox
// defaults true there with no UI to change it -- see Layout.h). Same full
// kRowLineGap clearance off WIDESCREEN's own bottom edge that WIDESCREEN
// itself uses off Divider 1b.
static const float kLetterboxLabelY = (kWideScreenCbY - kCheckboxSide * 0.5f) - kRowLineGap;
static const float kLetterboxCbX    = kRightEdge - kCheckboxSide * 0.5f, kLetterboxCbY = kLetterboxLabelY;
#endif

// Divider 2: a full kRowLineGap below the WIDESCREEN checkbox's own BOTTOM
// edge (a real widget-box edge, not the label baseline) -- same wider
// clearance WIDESCREEN's own top gap uses (see kWideScreenLabelY), so the
// row is evenly padded on both sides rather than cramped against Divider 2
// while airy against Divider 1b. Previously measured off SENSITIVITY
// directly; now off WIDESCREEN since it sits between them.
// Port specific: on FRUIT_PLATFORM_WII, Divider 2 instead measures off the
// LETTERBOX row (which sits between WIDESCREEN and Divider 2 there) -- same
// "full kRowLineGap below the preceding row's own bottom edge" formula.
#if !defined(FRUIT_PLATFORM_WII)
static const float kDividerY2 = (kWideScreenCbY - kCheckboxSide * 0.5f) - kRowLineGap - kDividerHeight * 0.5f;
#else
static const float kDividerY2 = (kLetterboxCbY - kCheckboxSide * 0.5f) - kRowLineGap - kDividerHeight * 0.5f;
#endif

#if !defined(FRUIT_PLATFORM_WII)
// NATIVE FRAME RATE: a FULL kRowLineGap below Divider 2's own bottom edge
// (real widget-box edge, not a label baseline) -- wider than the usual
// kDividerPad divider clearance, since this row and FPS COUNTER below it were
// previously cramped; both its own gaps (above, to Divider 2; below, to FPS
// COUNTER) use the full kRowLineGap instead. Port specific: no binary
// counterpart (see DebugFlags.h g_FpsCap60) -- caps render/present rate
// to 60fps; sim stays the fixed 60Hz accumulator regardless (FixedStepDriver,
// untouched by this checkbox). Structurally identical to the MOTION MODE
// row: bold kLabelScale label ("NATIVE FRAME RATE") + a 2-line dimmer
// kMotionDescColour description ("Smoother graphics for screens" / "with
// higher frame rate"), same kMotionDescY0/kMotionDescY1 spacing formula.
// kNativeFpsCbY centres the checkbox on the label+2-line-desc sub-block, same
// formula kMotionCbY uses for MOTION MODE's block.
//
// Port specific: on FRUIT_PLATFORM_WII this entire row is compiled out --
// the Wii build has no "cap to 60fps" concept (FN::g_FpsCap60 is forced
// false, see SettingsSave.cpp LoadSettings' Wii override); native/display
// refresh is the only supported mode, so the toggle+its label/description
// are hidden rather than shown-but-inert. FPS COUNTER below reflows up to
// take this row's place (see kFpsLabelY's FRUIT_PLATFORM_WII branch).
static const float kNativeFpsLabelY = (kDividerY2 - kDividerHeight * 0.5f) - kRowLineGap;
static const float kNativeFpsDescY0 = kNativeFpsLabelY - kRowLineGap;
static const float kNativeFpsDescY1 = kNativeFpsDescY0 - kMotionDescLineGap;
static const float kNativeFpsCbX = kRightEdge - kCheckboxSide * 0.5f;
static const float kNativeFpsCbY = (kNativeFpsLabelY + kNativeFpsDescY1) * 0.5f;

// FPS COUNTER: a FULL kRowLineGap below the native row's own checkbox box
// bottom edge (real widget-box edge, not a label baseline -- same "measure
// to the box edge" convention kFpsLabelY always used, just re-anchored off
// the native row's box instead of Divider 2 directly now that the native row
// sits between them). Now the BOTTOM-MOST row (WIDESCREEN moved above,
// after MOTION MODE).
static const float kFpsLabelY = (kNativeFpsCbY - kCheckboxSide * 0.5f) - kRowLineGap - kCheckboxSide * 0.5f;
#else
// Port specific: Wii reflow -- NATIVE FRAME RATE (and Divider 2's role as its
// top clearance) is removed entirely (see above), so FPS COUNTER becomes the
// row directly below Divider 2, using the SAME "full kRowLineGap below a
// divider's own bottom edge" formula WIDESCREEN itself uses off Divider 1b
// (see kWideScreenLabelY) -- not a hand-tuned offset, just the row-pitch
// formula already established by every other single-line row in this file,
// re-anchored one divider earlier.
static const float kFpsLabelY = (kDividerY2 - kDividerHeight * 0.5f) - kRowLineGap;
#endif
static const float kFpsCbX    =   kRightEdge - kCheckboxSide * 0.5f, kFpsCbY     = kFpsLabelY;

// Content height (dropdown box top .. FPS COUNTER checkbox box bottom, plus
// the top/bottom pads -- bottom padded extra by kContentBottomFadeClearance,
// see kContentBottomPad above) and the derived viewport/scroll extents. The
// viewport is the plate's usable content window (kViewportHalfH*2); content
// EXCEEDS it by design (kContentH > kViewportH), which is the whole point of
// this scrollable layout -- UpdateScroll()/Draw() clip + scroll it.
//
// kContentTop is the FIRST content row's own TOP edge -- kViewportHalfH MINUS
// the reserved top pad (matches kComboY + kComboScaleY*0.5f == kViewportHalfH
// - kContentTopPad by construction, see kComboY above; on FRUIT_PLATFORM_WII
// the first row is MOTION MODE's label instead, whose kMotionLabelY Wii
// branch anchors to this SAME kViewportHalfH - kContentTopPad point -- see
// its own comment -- so this formula needs no platform branch of its own).
// Previously this was written as plain kViewportHalfH (no pad subtracted),
// which silently dropped kContentTopPad's worth of height from kContentH --
// undercounting m_MaxScroll by the same amount and leaving the bottom row
// permanently unreachable at the bottom of the scroll range.
//
// kContentBottom is now measured off FPS COUNTER (the new bottom-most row)
// instead of WIDESCREEN -- WIDESCREEN moved up into its own group after
// MOTION MODE, so it no longer defines the content's bottom edge.
static const float kContentTop    = kViewportHalfH - kContentTopPad;
static const float kContentBottom = (kFpsCbY - kCheckboxSide * 0.5f) - kContentBottomPad;
static const float kContentH      = kContentTop - kContentBottom;
static const float kViewportH     = kViewportHalfH * 2.0f;

// Plate panel -- medbacking.tex drawn as a 9-slice (see Draw()), full texture
// (no UV cropping) so the wooden corner joints/lashing/log-end decor that
// protrudes past the frame renders whole rather than being clipped.
//
// medbacking.tex is 512x256 -- an HD (2x) backing; its logical size is half
// (256x128), i.e. 1 world unit = 2 texels. 9-patch border thickness (Android
// .9 semantics): 88px x / 58px y in texture pixels -- the corners (88x58) hold
// the joint+lashing+log-end art; centre 336x140 + edges stretch. In HD world
// units that is destBorder 44x29 (texels/2). Content padding for widget
// layout is 84x/56y texels -> 42x/28y world units per side.
//
// Outer footprint: kept at 440x220 (unchanged from the old stretched-quad
// panel) so the widget layout below is undisturbed. The top row (LANGUAGE
// combo, half-height 15 at y=82) reaches y=97, well clear of the panel's
// content top bound (kPlateHalfH(130) - kPlateDestBorderY(29) = 101).
static const float kPlateHalfW = 220.0f;
static const float kPlateHalfH = 130.0f;
static const float kPlateSrcBorderXPx = 88.0f, kPlateSrcBorderYPx = 58.0f;
static const float kPlateDestBorderX  = 44.0f, kPlateDestBorderY  = 29.0f;

// ---------------------------------------------------------------------------
// Popup open/close animation -- port specific, no binary counterpart.
// m_PopupOffsetY translates the ENTIRE popup coordinate space (plate,
// scissor band, labels/dividers/scrollbar, the four widgets, close button)
// vertically; see SettingsScreen.h AnimPhase note and UpdateAnim() below.
// ---------------------------------------------------------------------------
// Off-screen start offset -- large enough that the plate (kPlateHalfH*2 tall)
// is fully above the 320-tall ortho screen with margin (a screen height is
// generously more than enough clearance regardless of the plate's own size).
static const float kPopupStartOffsetY = 320.0f;
static const float kAnimOpenDuration  = 0.28f; // OPENING duration, seconds
static const float kAnimCloseDuration = 0.20f; // CLOSING duration, seconds

// Port specific: m_pCloseButton's own slide-in-from-bottom-right-corner
// offset (see header note) -- decoupled from kPopupStartOffsetY/
// m_PopupOffsetY entirely. Rests at (kCloseBtnX, kCloseBtnY), near the
// screen's bottom-right corner (below); the off-screen start pushes it
// further right (+X) and further down (-Y, since +Y is up in this centered
// ortho) by enough to clear the 320x480 screen with margin regardless of the
// bomb's own sprite size.
static const float kCloseBtnStartOffX = 120.0f;
static const float kCloseBtnStartOffY = -120.0f;
// Ease-out-back overshoot constant (standard "back" easing family, e.g.
// easings.net easeOutBack, whose usual literature value is 1.70158 -- NOT
// used here). Tuned down from that default so the resulting dip, which
// scales with kPopupStartOffsetY (320), lands around 8-12 world units past 0
// at the curve's peak (measured empirically: c1=1.0 -> peak overshoot
// fraction ~0.037 -> 320*0.037 ~= 11.85 units).
static const float kBackOvershoot = 1.0f;

// Standard ease-out-back: overshoots past 1.0 then settles back to 1.0.
// f(t) = 1 + c3*(t-1)^3 + c1*(t-1)^2, c3 = c1+1.
static float EaseOutBack(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const float c1 = kBackOvershoot;
    const float c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// Simple ease-in (quadratic accelerate) -- no bounce, used for CLOSING.
static float EaseInQuad(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t;
}

// Port specific: plain ease-out-cubic (decelerate, no overshoot) -- used for
// the close button's OPENING slide-in so it settles straight to rest with no
// bounce, unlike the plate's EaseOutBack. f(t) = 1 - (1-t)^3.
static float EaseOutCubic(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

// Smoothstep -- used for the backdrop dim fade so it isn't a raw linear
// timer ratio (matches neither easing family used for the offset; the dim
// just needs to feel soft on both ends, ease-out on open / ease-in on close).
// f(t) = 3t^2 - 2t^3.
static float SmoothStep(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

// Target backdrop alpha at full OPEN (matches the Colour(255,255,255,160)
// tint Draw() already applied unconditionally before this animation existed).
static const float kBackdropTargetAlpha = 160.0f / 255.0f;

// Close button at the SCREEN's bottom-right corner (not the panel corner) --
// same on-screen position as PauseScreen's quit button (bomb at (215,-135),
// text offset (-29,3) pulling the label onto the bomb face), so the two read
// identically. Sits outside/below the plate, on the modal backdrop.
static const float kCloseBtnX = 215.0f;
static const float kCloseBtnY = -135.0f;
static const _Vector3<float> kCloseTextOffset(-29.0f, 3.0f, 0.0f);

// ---------------------------------------------------------------------------
// Sensitivity slider <-> FN::g_MotionSpeedThreshold mapping.
// The slider is a generic [0,100] int widget; g_MotionSpeedThreshold is a
// px-per-sim-tick cut speed (see debug/DebugFlags.h; tuned live via F6/F8 in
// range roughly [0,30] elsewhere in the port). Map the slider linearly onto
// [0,30] px/tick, INVERTED so higher slider value == more sensitive == LOWER
// threshold (a smaller px/tick cut speed triggers on gentler flicks):
//   threshold = 30 * (100 - sliderValue) / 100
//   sliderValue = 100 - (threshold / 30) * 100, clamped to [0,100]
// ---------------------------------------------------------------------------
static const float kSensThresholdMax = 20.0f;

static int ThresholdToSlider(float threshold) {
    float t = ClampInt((int)threshold, 0, (int)kSensThresholdMax) / kSensThresholdMax;
    return ClampInt((int)(100.0f - t * 100.0f + 0.5f), kSensMin, kSensMax);
}

static float SliderToThreshold(int sliderValue) {
    float t = (float)ClampInt(sliderValue, kSensMin, kSensMax) / 100.0f;
    return (1.0f - t) * kSensThresholdMax;
}

// Port specific: single open/closed instance -- NULL when closed. Owned by
// Toggle(); see the header note for the open/close contract.
static SettingsScreen* s_pSettings = NULL;

// Port specific: see header note. Set by Toggle()'s close branch, cleared and
// acted on by MainScreen::Update's poll block once the modal has fully closed.
bool SettingsScreen::s_QuitAfterClose = false;

// Port specific: single open/close path, shared by the MainScreen settings
// button (src/screens/MainScreen.cpp::SettingsCallback) and m_pCloseButton's
// tap (CloseCallback). No binary counterpart -- this screen has none.
void SettingsScreen::Toggle() {
    if (s_pSettings == NULL) {
        s_pSettings = new SettingsScreen();
        // Port specific: AddControl the screen itself BEFORE Init() so it is
        // first in game_work.mHud's control list -- Init() AddControl's the
        // widgets (see Init()), and HUD::Draw has no per-control sort key,
        // only list order. Screen-first means the plate/backdrop (this
        // screen's own Draw()) paints behind every widget added afterward.
        if (game_work.mHud) {
            game_work.mHud->AddControl(s_pSettings, false);
        }
        s_pSettings->Init();
        // Port specific: start the drop-in animation off-screen -- Init()
        // above already laid out/AddControl'd everything at rest; UpdateAnim()
        // (called from Update() every frame) eases m_PopupOffsetY down to 0.
        s_pSettings->m_AnimPhase     = ANIM_OPENING;
        s_pSettings->m_AnimTimer     = 0.0f;
        s_pSettings->m_PopupOffsetY  = kPopupStartOffsetY;
        s_pSettings->m_BackdropAlpha = 0.0f;
        s_pSettings->m_CloseBtnOffX  = kCloseBtnStartOffX;
        s_pSettings->m_CloseBtnOffY  = kCloseBtnStartOffY;
        if (game_work.mHud) {
            // Port specific: capture input while the settings modal is open --
            // see HUD::SetInputModal (src/hud/HUD.h). Stays set for the whole
            // opening+open+closing lifetime so the blade/menu behind stays
            // frozen until the popup has fully closed (see CLOSING branch).
            game_work.mHud->SetInputModal(s_pSettings);
        }
    } else if (s_pSettings->m_AnimPhase == ANIM_OPEN) {
        // Port specific: don't tear down immediately -- start the exit
        // animation; the real teardown (SetInputModal(NULL)/SetPendingRemoval/
        // SaveSettings/s_QuitAfterClose, previously all done right here) now
        // runs at the END of the CLOSING animation, see UpdateAnim(). Ignore
        // further close requests while already ANIM_CLOSING (re-entry guard --
        // the else-if above only matches ANIM_OPEN).
        s_pSettings->m_AnimPhase = ANIM_CLOSING;
        s_pSettings->m_AnimTimer = 0.0f;
    }
}

bool SettingsScreen::IsOpen() {
    return s_pSettings != NULL;
}

#ifndef __bada__
SettingsScreen* SettingsScreen::GetInstance() {
    return s_pSettings;
}
#endif

// Scroll constants -- ported verbatim from UiDropdown (see header doc).
const float SettingsScreen::SCROLL_FRICTION    = 0.9f;
const float SettingsScreen::DRAG_DELTA_FACTOR  = -0.5f;
const float SettingsScreen::SPRING_BACK_COEF   = 0.75f;
const float SettingsScreen::SPRING_FWD_COEF    = 0.25f;
const float SettingsScreen::DRAG_CANCEL_DIST   = 5.0f;
const float SettingsScreen::kScrollVerticalBias = 1.0f;
const float SettingsScreen::kScrollDeadZone     = 4.0f;

SettingsScreen::SettingsScreen()
    : m_LangDrop(0)
    , m_MotionCb(0)
    , m_SensSlider(0)
    , m_FpsCb(0)
    , m_NativeFpsCb(0)
    , m_WideScreenCb(0)
#if defined(FRUIT_PLATFORM_WII)
    , m_LetterboxCb(0)
#endif
    , m_pCloseButton(0)
    , m_LangBaseY(0.0f)
    , m_MotionCbBaseY(0.0f)
    , m_SensBaseY(0.0f)
    , m_FpsCbBaseY(0.0f)
    , m_NativeFpsCbBaseY(0.0f)
    , m_WideScreenBaseY(0.0f)
#if defined(FRUIT_PLATFORM_WII)
    , m_LetterboxBaseY(0.0f)
#endif
    , m_ScrollY(0.0f)
    , m_ScrollVel(0.0f)
    , m_MaxScroll(0.0f)
    , m_AnchorScroll(0.0f)
    , m_ScrollAnchorPos(0.0f, 0.0f, 0.0f)
    , m_ScrollTouchId(-1)
    , m_ScrollDragging(0)
    , m_ScrollOwnsTouch(0)
    , m_ScrollDragDist(0.0f)
    , m_InitialLanguageFlag(0)
    , m_AnimPhase(ANIM_OPEN)
    , m_AnimTimer(0.0f)
    , m_PopupOffsetY(0.0f)
    , m_BackdropAlpha(kBackdropTargetAlpha)
    , m_CloseBtnOffX(0.0f)
    , m_CloseBtnOffY(0.0f)
{
    // TOP_MOST (0x800): the modal must draw over ALL main-screen HUD. This
    // screen owns the four in-plate widgets directly (NOT AddControl'd, see
    // header note) and drives their Update()/Draw() itself, so only this
    // screen and m_pCloseButton (still AddControl'd, sits outside the
    // scrolled plate) need HUD::Update's modal input-capture gate (see
    // HUD.h SetInputModal / HUD.cpp partOfModal check: ctrl == m_pInputModal
    // || ctrl->m_LayerFlags == HUD_LAYER_TOP_MOST).
    m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;
}

SettingsScreen::~SettingsScreen() {
    Release();
}

void SettingsScreen::Init() {
    m_Active = 1;

    // ---- shared widget textures: real art, staged at build time from ----
    // ---- assets/ui-widgets/*.svg by fn_asset_staging (mandatory -- the ----
    // ---- build fails if generation fails, see svg-to-webp.mjs). ----
    // box.tex is the binary's single shared field/row/track texture -- reused
    // here as the NineSlice background for every src/ui/ widget (checkbox,
    // slider track, dropdown bar+panel).
    m_TexBox   = Mortar::TextureManager::LoadLocalisedTexture("box.tex");
    m_TexCheck = Mortar::TextureManager::LoadLocalisedTexture("check.tex");
    m_TexCaret = Mortar::TextureManager::LoadLocalisedTexture("caret.tex");
    m_TexKnob  = Mortar::TextureManager::LoadLocalisedTexture("slider_will.tex");
    m_TexFade  = Mortar::TextureManager::LoadLocalisedTexture("list_fade.tex");
    m_TexItem  = Mortar::TextureManager::LoadLocalisedTexture("list_item.tex");
    m_TexDivider = Mortar::TextureManager::LoadLocalisedTexture("scratch_deviders.tex");
    // Port specific: modal dim backdrop -- solid black, alpha applied via vertex
    // tint (Colour(0,0,0,160) in Draw()), not baked into the texture. No real
    // widget counterpart.
    m_Backdrop = MakeSolidTex(0, 0, 0, 255, 8, 8);

    m_Plate = Mortar::TextureManager::LoadLocalisedTexture("medbacking.tex");

    // ---- language model ----
    m_LangItems.clear();
    for (int i = 0; i < kLanguageCount; ++i) {
        m_LangItems.push_back(std::string(kLanguageNames[i]));
    }

    m_InitialLanguageFlag = game_work.languageFlag;

    // Native language names need CJK/Hangul/Cyrillic glyphs the bitmap
    // font_fruit_ninja.fnt doesn't ship; switch the label/dropdown font to
    // the TTF font. Loaded unconditionally (even on FRUIT_PLATFORM_WII,
    // where m_LangDrop itself is never built) -- MOTION MODE/SENSITIVITY/etc
    // labels use m_LangFont too (see Draw()'s labelFont).
    m_LangFont = Mortar::Font::Create("fontstruetype/gangofchinese.ttf");

#if !defined(FRUIT_PLATFORM_WII)
    // Port specific: hidden entirely on FRUIT_PLATFORM_WII -- see m_LangDrop's
    // header comment. game_work.languageFlag is set once at boot from the
    // console system language (GameInitialise.cpp GetWiiSystemLanguageFlag())
    // and cannot be changed in-session there, so there is no picker to show.
    int langDefault = (int)(game_work.languageFlag < kLanguageCount ? game_work.languageFlag : 0);

    m_LangDrop = new UiDropdown(_Vector3<float>(kComboX, kComboY, 0.0f), m_LangItems, langDefault,
                                kComboVisibleRows, kComboScaleX, kComboScaleY);
    m_LangDrop->SetBoxTexture(m_TexBox);
    m_LangDrop->SetCaretTexture(m_TexCaret);
    m_LangDrop->SetFadeTexture(m_TexFade);
    m_LangDrop->SetItemTexture(m_TexItem);
    // Port specific: white to match the row text colour (m_RowTextColour,
    // set via SetRowColours below) rather than SettingsTextColour()'s dark
    // brown -- the collapsed-bar value and the open-list rows read as one
    // consistent text treatment instead of the bar looking mismatched/amber
    // against the dark wood box.
    m_LangDrop->SetTextColour(Colour::White);
    m_LangDrop->SetTextScale(kComboTextScale);
    if (m_LangFont.IsValid()) {
        m_LangDrop->SetFont(m_LangFont.Get());
    }
    // Port specific: no binary counterpart -- wood-amber theme for the dropdown
    // rows. Row text uses a light parchment/cream rather than
    // SettingsTextColour()'s dark brown (0x6F,0x46,0x1E) -- that brown is
    // low-contrast against both the dark wood row background AND the amber
    // selected/hover tints, whereas a light cream reads clearly on all three.
    m_LangDrop->SetRowColours(
        Colour(0xF2, 0xC4, 0x00, 0xFF),   // bright gold -- selected row
        Colour(0xC9, 0x9A, 0x3A, 0xFF),   // softer warm amber -- hover row
        Colour(0xEA, 0xD8, 0xB0, 0xFF));  // parchment/cream -- row text
    m_LangDrop->SetOnChange(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnLangChanged));
    m_LangDrop->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;
#else
    m_LangDrop = NULL;
#endif

    // ---- checkboxes / slider, seeded from live globals ----
    m_MotionCb = new UiCheckbox(_Vector3<float>(kMotionCbX, kMotionCbY, 0.0f), kCheckboxSide, FN::g_MotionMode);
    m_MotionCb->SetBoxTexture(m_TexBox);
    m_MotionCb->SetCheckGlyph(m_TexCheck);
    m_MotionCb->SetOnChange(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnMotionToggle));
    m_MotionCb->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;

#if !defined(FRUIT_PLATFORM_WII)
    // Port specific: checkbox polarity is INVERTED vs FN::g_FpsCap60 -- checked
    // means "use native/display refresh" (the default, g_FpsCap60==false), so
    // seed with !g_FpsCap60. OnFpsCapToggle() applies the same inversion on
    // write. Persistence (SettingsSave) is untouched -- it still reads/writes
    // the global directly, never the checkbox's own polarity.
    //
    // Port specific: hidden on FRUIT_PLATFORM_WII -- native/display refresh is
    // forced always-on there (see SettingsSave.cpp LoadSettings' Wii override
    // of FN::g_FpsCap60), so there is no user-facing choice to expose.
    m_NativeFpsCb = new UiCheckbox(_Vector3<float>(kNativeFpsCbX, kNativeFpsCbY, 0.0f), kCheckboxSide, !FN::g_FpsCap60);
    m_NativeFpsCb->SetBoxTexture(m_TexBox);
    m_NativeFpsCb->SetCheckGlyph(m_TexCheck);
    m_NativeFpsCb->SetOnChange(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnFpsCapToggle));
    m_NativeFpsCb->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;
#endif

    m_FpsCb = new UiCheckbox(_Vector3<float>(kFpsCbX, kFpsCbY, 0.0f), kCheckboxSide, FN::g_ShowFps);
    m_FpsCb->SetBoxTexture(m_TexBox);
    m_FpsCb->SetCheckGlyph(m_TexCheck);
    m_FpsCb->SetOnChange(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnFpsToggle));
    m_FpsCb->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;

    // Port specific: seeded from the PREF (not the active value) -- see
    // m_WideScreenCb's header comment / OnWideScreenToggle().
    m_WideScreenCb = new UiCheckbox(_Vector3<float>(kWideScreenCbX, kWideScreenCbY, 0.0f), kCheckboxSide, Layout::IsWideLayoutPref());
    m_WideScreenCb->SetBoxTexture(m_TexBox);
    m_WideScreenCb->SetCheckGlyph(m_TexCheck);
    m_WideScreenCb->SetOnChange(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnWideScreenToggle));
    m_WideScreenCb->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;

#if defined(FRUIT_PLATFORM_WII)
    // Port specific: live toggle -- seeded from the CURRENT value (not a
    // pref/active split like WIDESCREEN, see m_LetterboxCb's header comment).
    m_LetterboxCb = new UiCheckbox(_Vector3<float>(kLetterboxCbX, kLetterboxCbY, 0.0f), kCheckboxSide, Layout::IsLetterbox());
    m_LetterboxCb->SetBoxTexture(m_TexBox);
    m_LetterboxCb->SetCheckGlyph(m_TexCheck);
    m_LetterboxCb->SetOnChange(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnLetterboxToggle));
    m_LetterboxCb->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;
#endif

    int sens0 = ThresholdToSlider(FN::g_MotionSpeedThreshold);
    m_SensSlider = new UiSlider(_Vector3<float>(kSensX, kSensY, 0.0f), kSensMin, kSensMax, sens0);
    m_SensSlider->SetBoxTexture(m_TexBox);
    m_SensSlider->SetKnobTexture(m_TexKnob);
    m_SensSlider->SetTrackSize(kSensTrackW, kSensTrackH);
    m_SensSlider->SetSteps(20);
    m_SensSlider->SetDetent(ThresholdToSlider(10.0f));
    m_SensSlider->SetOnChange(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnSensChanged));
    m_SensSlider->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;

    // ---- close button: BSButton built the same way PauseScreen builds
    // ---- m_QuitButton (bomb-with-X icon + separate text label) ----
    m_CloseTex = Mortar::TextureManager::LoadLocalisedTexture("quit_title.tex");
    m_pCloseButton = new BSButton(
        _Vector3<float>(MapX(kCloseBtnX, "settings.back"), kCloseBtnY, 0.0f),
        // Port specific: no dedicated "CLOSE" string table entry exists (this
        // screen has no binary counterpart). LSTR_DJ_BACK_BUTTON ("BACK") is
        // reused instead of LSTR_QUIT ("QUIT") -- other screens (AboutScreen,
        // GameModeScreen, DojoScreen) already reuse this same id for their
        // back/exit buttons, and "BACK" reads correctly for closing a modal.
        // Relabelled to LSTR_QUIT ("QUIT") reactively by UpdateCloseButtonLabel()
        // once the language selection has changed (see Toggle()'s
        // quit-to-apply-language-change path) -- this ctor arg is just the
        // initial bake, immediately overwritten by the UpdateCloseButtonLabel()
        // call at the end of Init() below. Port specific: English literal (this
        // port-only screen keeps its Quit/Back label English regardless of locale).
        "BACK",
        _Vector3<float>(1.0f, 1.0f, 1.0f)
    );
    m_pCloseButton->Init();
    m_pCloseButton->SetCallback(
        Mortar::Delegate0<void>::Make(this, &SettingsScreen::CloseCallback));
    if (m_pCloseButton->m_pLabelBox) {
        m_pCloseButton->m_pLabelBox->SetGradient(
            Colour(0xff, 0x00, 0x00, 0xff),
            Colour(0x40, 0x00, 0x00, 0xff),
            false);
        m_pCloseButton->m_pLabelBox->ReshapeBounds(0x36, 0x14, 1, 0);
        m_pCloseButton->m_pLabelBox->SetStroke(1.0f, Colour::Black);
        m_pCloseButton->m_pLabelBox->SetFontSize(14.0f);
        m_pCloseButton->m_pLabelBox->FitIntoVerticalBounds();
    }
    m_pCloseButton->SetTexture(m_CloseTex, true);
    m_pCloseButton->SetTextOffset(kCloseTextOffset);
    m_pCloseButton->SetDrawOrder(8);
    // TOP_MOST so HUD::Update's modal input-capture gate still lets touches
    // through to this button (see m_LayerFlags note in the ctor above).
    // SetDrawOrder(8) above writes m_LayerFlags=8 (BSButton::SetDrawOrder is
    // a misnomer -- it overwrites the layer mask, not a sort key); this
    // assignment must run AFTER it to win.
    m_pCloseButton->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;

    // ---- AddControl ONLY the close button ----
    // The four in-plate widgets are owned directly (see header note) --
    // this screen calls their Update()/Draw() itself every frame instead of
    // linking them into game_work.mHud's control list, so a scrolled +
    // glScissor'd viewport can wrap them (see UpdateScroll()/Draw()).
    // m_pCloseButton sits outside the plate and never scrolls, so it keeps
    // the old AddControl'd path.
    if (game_work.mHud) {
        game_work.mHud->AddControl(m_pCloseButton, false);
    }

    // ---- capture each widget's base (unscrolled) content-space Y ----
    // Update() rewrites pos.y = baseY + off (off = m_ScrollY + m_PopupOffsetY)
    // every frame; these are the values pos.y already holds right now
    // (kLangLabelY's row uses kComboY, not kLangLabelY, since the DROPDOWN
    // BAR is the widget -- kLangLabelY is only the text label's baseline,
    // drawn separately).
    m_LangBaseY        = kComboY;
    m_MotionCbBaseY    = kMotionCbY;
    m_SensBaseY        = kSensY;
    m_FpsCbBaseY       = kFpsCbY;
#if !defined(FRUIT_PLATFORM_WII)
    m_NativeFpsCbBaseY = kNativeFpsCbY;
#endif
    m_WideScreenBaseY  = kWideScreenCbY;
#if defined(FRUIT_PLATFORM_WII)
    m_LetterboxBaseY   = kLetterboxCbY;
#endif

    // ---- scroll extents ----
    m_MaxScroll = kContentH - kViewportH;
    if (m_MaxScroll < 0.0f) m_MaxScroll = 0.0f;
    m_ScrollY = 0.0f;
    m_ScrollVel = 0.0f;
    m_ScrollTouchId = -1;
    m_ScrollDragging = 0;
    m_ScrollOwnsTouch = 0;
    m_ScrollDragDist = 0.0f;

    // m_InitialLanguageFlag was just captured above, so langChanged is false
    // here -- this bakes the initial "BACK" label via the same path
    // OnLangChanged() uses later, rather than duplicating the SetText call.
    UpdateCloseButtonLabel();
}

void SettingsScreen::Release() {
    // m_pCloseButton is still AddControl'd -- torn down via SetPendingRemoval
    // (HUD's own Update sweep deletes it next tick, matching GameModeScreen::
    // RemoveButtons' pattern) rather than a direct delete here, since
    // deleting synchronously while it's still linked into game_work.mHud's
    // control list would leave a dangling pointer in that list.
    //
    // The four in-plate widgets were NEVER AddControl'd (see header note) --
    // no HUD control list holds a pointer to them, so a direct delete here
    // is safe and is the only way they get torn down (there is no HUD sweep
    // to do it for them).
    delete m_LangDrop;    m_LangDrop    = 0;
    delete m_MotionCb;    m_MotionCb    = 0;
    delete m_SensSlider;  m_SensSlider  = 0;
    delete m_FpsCb;       m_FpsCb       = 0;
    delete m_NativeFpsCb; m_NativeFpsCb = 0;
    delete m_WideScreenCb; m_WideScreenCb = 0;
#if defined(FRUIT_PLATFORM_WII)
    delete m_LetterboxCb; m_LetterboxCb = 0;
#endif
    if (m_pCloseButton) { m_pCloseButton->SetPendingRemoval(); m_pCloseButton = 0; }

    m_TexBox.SetNull();
    m_TexCheck.SetNull();
    m_TexCaret.SetNull();
    m_TexKnob.SetNull();
    m_TexFade.SetNull();
    m_TexItem.SetNull();
    m_TexDivider.SetNull();
    m_Plate.SetNull();
    m_Backdrop.SetNull();
    m_LangFont.SetNull();
    m_CloseTex.SetNull();

    HUDControl3d::Release();
}

void SettingsScreen::Update(float dt) {
    // ---- popup open/close animation first -- may perform the deferred
    // ---- teardown and return early (this instance can be mid-destruction
    // ---- or SetPendingRemoval'd by the time it returns; nothing below is
    // ---- safe to touch after that). ----
    UpdateAnim(dt);
    if (s_pSettings != this) {
        // Teardown ran this frame (see UpdateAnim's ANIM_CLOSING-complete
        // branch) -- s_pSettings was cleared. Don't touch widget state below.
        return;
    }

    // ---- close button: independent of scroll (its own bottom-right
    // ---- slide-in offset, m_CloseBtnOffX/Y, see header note + UpdateAnim()),
    // ---- so it can be positioned any time before the widgets are ticked. ----
    if (m_pCloseButton) {
        m_pCloseButton->pos.x = MapX(kCloseBtnX, "settings.back") + m_CloseBtnOffX;
        m_pCloseButton->pos.y = kCloseBtnY + m_CloseBtnOffY;
    }

    // ---- rewrite each widget's live pos.y from its base Y + scroll offset,
    // ---- BEFORE ticking it, so its own hit-rect/PollTouch tracks the
    // ---- scrolled position. off = +m_ScrollY: increasing m_ScrollY shifts
    // ---- content UP (see header Scrolling note). m_PopupOffsetY (the popup
    // ---- drop-in/out animation, see UpdateAnim()) stacks on top so widget
    // ---- hit-rects track the animated plate position too. Runs here (before
    // ---- the ANIM_OPEN gate) so widgets still track the plate's drop-in/out
    // ---- motion during OPENING/CLOSING, using this frame's m_PopupOffsetY. ----
    {
        float off = m_ScrollY + m_PopupOffsetY;
        if (m_LangDrop)    m_LangDrop->pos.y    = m_LangBaseY        + off;
        if (m_MotionCb)    m_MotionCb->pos.y    = m_MotionCbBaseY    + off;
        if (m_SensSlider)  m_SensSlider->pos.y  = m_SensBaseY        + off;
        if (m_FpsCb)       m_FpsCb->pos.y       = m_FpsCbBaseY       + off;
        if (m_NativeFpsCb) m_NativeFpsCb->pos.y = m_NativeFpsCbBaseY + off;
        if (m_WideScreenCb) m_WideScreenCb->pos.y = m_WideScreenBaseY + off;
#if defined(FRUIT_PLATFORM_WII)
        if (m_LetterboxCb) m_LetterboxCb->pos.y = m_LetterboxBaseY + off;
#endif
    }

    // ---- kinetic scroll (owns/tracks the touch that drives it) + widget
    // ---- input -- ONLY while fully open; suppressed during
    // ---- OPENING/CLOSING so nothing is interactive mid-animation (the modal
    // ---- input capture, see Toggle(), stays set throughout so the screen
    // ---- behind stays frozen regardless). ----
    if (m_AnimPhase != ANIM_OPEN) {
        return;
    }

    UpdateScroll(dt);
    // UpdateScroll() only sets m_ScrollVel now (the velocity->position
    // integration runs in UpdateRealtime(), see SettingsScreen.h), so this
    // second rewrite is normally a no-op duplicate of the one above -- kept
    // for structural symmetry with the pre-split code and because a widget's
    // own Update() below must see pos.y already settled either way.
    {
        float off = m_ScrollY + m_PopupOffsetY;
        if (m_LangDrop)    m_LangDrop->pos.y    = m_LangBaseY        + off;
        if (m_MotionCb)    m_MotionCb->pos.y    = m_MotionCbBaseY    + off;
        if (m_SensSlider)  m_SensSlider->pos.y  = m_SensBaseY        + off;
        if (m_FpsCb)       m_FpsCb->pos.y       = m_FpsCbBaseY       + off;
        if (m_NativeFpsCb) m_NativeFpsCb->pos.y = m_NativeFpsCbBaseY + off;
        if (m_WideScreenCb) m_WideScreenCb->pos.y = m_WideScreenBaseY + off;
#if defined(FRUIT_PLATFORM_WII)
        if (m_LetterboxCb) m_LetterboxCb->pos.y = m_LetterboxBaseY + off;
#endif
    }

    // Port specific: while the dropdown panel is open, gate out the other
    // widgets (checkboxes/slider/close) so they neither receive input nor
    // draw over the panel. The dropdown itself stays always-active.
    // Restored to active the frame the panel closes.
    uint8_t othersActive = m_LangDrop && m_LangDrop->IsOpen() ? 0 : 1;
    if (m_pCloseButton) m_pCloseButton->m_Active = othersActive;

    // Port specific: while the scroll drag owns the current touch, skip the
    // three plain widgets' own Update() -- letting a vertical scroll drag
    // also fall through to PollTouch would toggle a checkbox / scrub the
    // slider under the finger at the same time. The dropdown ALWAYS ticks
    // (it owns its own full-screen modal latch when open, see UiDropdown::
    // Update, and must still open/close on a simple tap when closed).
    if (!m_ScrollOwnsTouch) {
        if (othersActive) {
            if (m_MotionCb)   m_MotionCb->Update(dt);
            if (m_SensSlider) m_SensSlider->Update(dt);
            if (m_FpsCb)      m_FpsCb->Update(dt);
            if (m_NativeFpsCb) m_NativeFpsCb->Update(dt);
            if (m_WideScreenCb) m_WideScreenCb->Update(dt);
#if defined(FRUIT_PLATFORM_WII)
            if (m_LetterboxCb) m_LetterboxCb->Update(dt);
#endif
        }
    }
    if (m_LangDrop) m_LangDrop->Update(dt);
}

// Port specific: evaluates the continuous popup easing curves (m_PopupOffsetY/
// m_BackdropAlpha/m_CloseBtnOffX/Y) at a given phase + timer-ratio `t01`
// (m_AnimTimer/duration, UNCLAMPED -- pass a value >1.0 and this still
// produces a valid (if past-target) sample; callers clamp for their own
// purposes). Shared by UpdateRealtime() (continuous per-present eval, the
// normal path) and UpdateAnim()'s transition-complete snap (which calls this
// once more with t01 pinned to exactly 1.0 to land on the precise rest
// values rather than trusting float accumulation to hit them). OPENING eases
// m_PopupOffsetY from kPopupStartOffsetY down to 0 with EASE-OUT-BACK (a
// small overshoot PAST 0 to negative before settling -- the "bounce"),
// CLOSING eases back up to kPopupStartOffsetY with a plain EASE-IN (no
// bounce). m_BackdropAlpha fades 0->target / target->0 on a SmoothStep of the
// same ratio (an easing curve of its own, not the raw linear ratio) so the
// dim ramps up quickly then settles on open, and eases off on close.
// m_pCloseButton is NOT part of the m_PopupOffsetY popup space -- it rides
// its own (m_CloseBtnOffX, m_CloseBtnOffY) offset on the same timer/phase,
// sliding in from off-screen bottom-right on OPENING and back out on CLOSING
// (see header note).
static void EvalPopupAnim(SettingsScreen::AnimPhase phase, float t01,
                           float& popupOffsetY, float& backdropAlpha,
                           float& closeBtnOffX, float& closeBtnOffY) {
    float tc = t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01);
    switch (phase) {
    case SettingsScreen::ANIM_OPENING: {
        backdropAlpha = SmoothStep(tc) * kBackdropTargetAlpha;
        // EaseOutBack(t) runs 0->1 with an overshoot PAST 1 (peaking above 1,
        // per the "back" family), which maps to offset running
        // kPopupStartOffsetY -> 0 with an overshoot PAST 0 to NEGATIVE (the
        // plate briefly drops below rest before settling back) -- exactly
        // the requested bounce.
        popupOffsetY = kPopupStartOffsetY * (1.0f - EaseOutBack(tc));
        // Close button: plain ease-out-cubic (decelerate slide, no
        // overshoot) -- own start/rest pair (bottom-right off-screen ->
        // (0,0)), unlike the plate's overshoot-then-settle bounce.
        float bt = 1.0f - EaseOutCubic(tc);
        closeBtnOffX = kCloseBtnStartOffX * bt;
        closeBtnOffY = kCloseBtnStartOffY * bt;
        break;
    }
    case SettingsScreen::ANIM_OPEN:
        break;
    case SettingsScreen::ANIM_CLOSING: {
        backdropAlpha = (1.0f - SmoothStep(tc)) * kBackdropTargetAlpha;
        popupOffsetY = kPopupStartOffsetY * EaseInQuad(tc);
        // Close button slides back out to its bottom-right off-screen start
        // over the same close window, plain ease-in (no bounce), matching
        // the plate.
        closeBtnOffX = kCloseBtnStartOffX * EaseInQuad(tc);
        closeBtnOffY = kCloseBtnStartOffY * EaseInQuad(tc);
        break;
    }
    }
}

// Port specific: advances the popup open/close animation PHASE STATE MACHINE
// (OPENING->OPEN, CLOSING->teardown) at the fixed 60Hz sim rate -- kept here
// (not moved to UpdateRealtime()) so the deferred teardown (SetInputModal
// (NULL)/SetPendingRemoval/SaveSettings/s_QuitAfterClose, see below) fires at
// a deterministic sim tick rather than a variable-rate present. The
// CONTINUOUS easing math itself (m_PopupOffsetY/m_BackdropAlpha/
// m_CloseBtnOffX/Y) has moved to UpdateRealtime() (once per PRESENTED frame,
// dt-scaled -- see its own comment) so the drop/slide motion tracks the
// display refresh instead of the sim tick; m_AnimTimer is likewise now
// advanced ONLY there (real seconds, finer-grained than the 60Hz tick at
// >60fps) -- this function just INSPECTS it against each phase's duration to
// decide when to transition, mirroring UpdateScroll()/UpdateRealtime()'s own
// split (UpdateScroll() tracks touch only; the integration that used to run
// inline moved out to the per-present tick).
// When a CLOSING animation completes, performs the ACTUAL teardown that used
// to run synchronously in Toggle()'s close branch (SaveSettings/
// SetInputModal(NULL)/SetPendingRemoval/s_pSettings=NULL/s_QuitAfterClose),
// in the same order, just deferred to here.
void SettingsScreen::UpdateAnim(float dt) {
    (void)dt;
    switch (m_AnimPhase) {
    case ANIM_OPENING: {
        if (m_AnimTimer >= kAnimOpenDuration) {
            m_AnimPhase = ANIM_OPEN;
            m_AnimTimer = 0.0f;
            m_PopupOffsetY = 0.0f;
            m_BackdropAlpha = kBackdropTargetAlpha;
            m_CloseBtnOffX = 0.0f;
            m_CloseBtnOffY = 0.0f;
        }
        break;
    }
    case ANIM_OPEN:
        break;
    case ANIM_CLOSING: {
        if (m_AnimTimer >= kAnimCloseDuration) {
            // Snap to the exact rest values (t01 pinned to 1.0) rather than
            // trusting whatever UpdateRealtime() last computed at a
            // slightly-under-1.0 ratio.
            EvalPopupAnim(ANIM_CLOSING, 1.0f, m_PopupOffsetY, m_BackdropAlpha,
                          m_CloseBtnOffX, m_CloseBtnOffY);

            // ---- deferred teardown -- was Toggle()'s close branch ----
            bool langChanged = (game_work.languageFlag != m_InitialLanguageFlag);
            // Port specific: mirrors langChanged -- a widescreen pref change
            // also needs an app restart to apply (see Layout.h ACTIVE vs
            // PREF split), so it triggers the same quit-to-apply path.
            bool wideScreenRestartPending = Layout::WideLayoutRestartPending();

            if (game_work.mHud) {
                game_work.mHud->SetInputModal(NULL);
            }
            SetPendingRemoval();
            s_pSettings = NULL;

            SaveSettings();

            if (langChanged || wideScreenRestartPending) {
                // Port specific: quitting is how the new language / widescreen
                // layout takes effect on restart (no live-reload of
                // already-baked UI strings/fonts, nor of already-built
                // screens' MapX()'d positions). See the original header note
                // on s_QuitAfterClose for why this is latched rather than
                // triggered synchronously -- still true here, deferred one
                // step further (end of the CLOSING animation rather than end
                // of Toggle()).
                s_QuitAfterClose = true;
            }
        }
        break;
    }
    }
}

void SettingsScreen::SetAnimOpenForTest() {
    m_AnimPhase = ANIM_OPEN;
    m_AnimTimer = 0.0f;
    m_PopupOffsetY = 0.0f;
    m_BackdropAlpha = kBackdropTargetAlpha;
    m_CloseBtnOffX = 0.0f;
    m_CloseBtnOffY = 0.0f;
}

// Port specific: kinetic drag-to-velocity TOUCH tracking for the plate's
// content viewport, ported (with the integrate/spring-back tail moved out,
// see UpdateRealtime() below) from UiDropdown::Update's scroll tail (src/ui/
// UiDropdown.cpp ~234-280) -- same constants, re-scoped from a dropdown's row
// list to the whole plate's four widgets. Runs at the 60Hz sim rate (reads
// live touch state); only sets m_ScrollVel, does not integrate m_ScrollY.
void SettingsScreen::UpdateScroll(float dt) {
    (void)dt;

    // Locked while the dropdown panel is open -- its own modal latch already
    // owns every touch full-screen (see UiDropdown::Update), so a scroll
    // drag underneath it would never see a touch anyway; explicitly zeroing
    // velocity here just avoids a stale fling resuming the instant it closes.
    if (m_LangDrop && m_LangDrop->IsOpen()) {
        m_ScrollVel = 0.0f;
        return;
    }

    // Plate content rect, in SCREEN space (the viewport doesn't move, only
    // its content does) -- X spans the plate's inner content bound, Y spans
    // the fixed viewport half-height (kViewportHalfH).
    const float plateLeft   = -kPlateHalfW + kPlateDestBorderX;
    const float plateRight  =  kPlateHalfW - kPlateDestBorderX;
    const float plateTop    =  kViewportHalfH;
    const float plateBottom = -kViewportHalfH;

    if (m_ScrollTouchId == -1) {
        // --- Acquire (press-edge only) ---
        int slot = TouchInRegion(plateLeft, plateRight, plateBottom, plateTop, -1);
        if (slot != -1 && IsTouchDown(slot) == 2) {
            const Mortar::TouchState* ts = Mortar::Touch::GetInstance().GetSlot(slot);
            if (ts) {
                m_ScrollTouchId = slot;
                m_ScrollAnchorPos.x = ts->currX;
                m_ScrollAnchorPos.y = ts->currY;
                m_AnchorScroll = m_ScrollY;
                m_ScrollDragging = 0;
                m_ScrollOwnsTouch = 0;
                m_ScrollDragDist = 0.0f;
            }
        }
    } else if (IsTouchDown(m_ScrollTouchId) == 0) {
        // --- Release ---
        m_ScrollTouchId = -1;
        m_ScrollOwnsTouch = 0;
        m_ScrollDragging = 0;
        // Velocity/offset keep coasting/springing below -- a fling release
        // must NOT reset m_ScrollVel.
    } else {
        // --- Held: disambiguate scroll-vs-widget (60Hz) ---
        // Port specific: task #13 -- the damped-follow m_ScrollVel compute
        // (Held-branch tail below UiDropdown's formula) moved to
        // UpdateRealtime() so it recomputes at native present rate via
        // GetLivePos instead of once per 60Hz tick via ts->currY (see
        // SettingsScreen::UpdateRealtime). This branch keeps ONLY the
        // scroll-vs-widget disambiguation (drag distance accumulation +
        // m_ScrollOwnsTouch latch), which the widget-dispatch gate below
        // needs at 60Hz.
        const Mortar::TouchState* ts = Mortar::Touch::GetInstance().GetSlot(m_ScrollTouchId);
        float currentX = ts ? ts->currX : m_ScrollAnchorPos.x;
        float currentY = ts ? ts->currY : m_ScrollAnchorPos.y;
        float dx = currentX - m_ScrollAnchorPos.x;
        float dy = currentY - m_ScrollAnchorPos.y;
        float absDx = dx < 0.0f ? -dx : dx;
        float absDy = dy < 0.0f ? -dy : dy;

        float prevAbsDy = m_ScrollDragging ? m_ScrollDragDist : 0.0f;
        m_ScrollDragDist += (absDy > prevAbsDy) ? (absDy - prevAbsDy) : 0.0f;

        if (!m_ScrollOwnsTouch) {
            // Become a scroll only once the drag is clearly, predominantly
            // vertical -- until then leave the touch alone so a tap or a
            // SENSITIVITY slider's horizontal drag proceeds through the
            // widget's own PollTouch (called after this, in Update()).
            if (absDy > kScrollDeadZone && absDy >= absDx * kScrollVerticalBias) {
                m_ScrollOwnsTouch = 1;
                m_ScrollDragging = 1;
                // Ownership just transferred: the widgets stop being ticked
                // below, so cancel any that is still latched on this slot
                // rather than leaving it to resolve a dead capture as a
                // release later (phantom toggle). Mirrors the binary's own
                // hand-off -- ScrollingMenu::Update @0x001b03b4 calls the
                // collided item's cancel slot once the drag passes 5.0.
                CancelWidgetTouches(m_ScrollTouchId);
            }
        }
    }

    // Integrate + friction + spring-back moved to UpdateRealtime() (see
    // below) -- runs once per PRESENTED frame instead of once per 60Hz sim
    // step, dt-scaled so it's rate-independent. UpdateScroll() only tracks
    // touch and sets m_ScrollVel now; it does NOT touch m_ScrollY.
}

// Port specific: see the header. Covers every widget Update() gates out on
// m_ScrollOwnsTouch, plus m_LangDrop -- the dropdown keeps ticking, but while
// CLOSED it latches through the same UiWidget::PollTouch, so a scroll that
// starts on its bar would otherwise open the panel on release.
void SettingsScreen::CancelWidgetTouches(int slot) {
    if (m_LangDrop)     m_LangDrop->CancelTouch(slot);
    if (m_MotionCb)     m_MotionCb->CancelTouch(slot);
    if (m_SensSlider)   m_SensSlider->CancelTouch(slot);
    if (m_FpsCb)        m_FpsCb->CancelTouch(slot);
    if (m_NativeFpsCb)  m_NativeFpsCb->CancelTouch(slot);
    if (m_WideScreenCb) m_WideScreenCb->CancelTouch(slot);
#if defined(FRUIT_PLATFORM_WII)
    if (m_LetterboxCb)  m_LetterboxCb->CancelTouch(slot);
#endif
}

#ifndef __bada__
// Port specific: no binary counterpart (see SettingsScreen.h). Runs the
// scroll velocity->position integration (friction decay + spring-back) that
// UpdateScroll()'s tail used to run inline at the fixed 60Hz sim rate.
// Called once per PRESENTED frame (native/120 or capped/60, see
// Game::tickRealtimeUi), with dtSeconds = real elapsed time since the last
// present -- dt-SCALED below so the same on-screen motion results at 60 and
// 120 fps alike:
//   f = dtSeconds * 60                     -- frames-equivalent (1.0 at 60fps)
//   m_ScrollVel *= SCROLL_FRICTION^f        -- exponential decay, rate-independent
//   m_ScrollY   += m_ScrollVel * f           -- velocity integrated by frame-equivalents
//   spring-back: m_ScrollY += (target - m_ScrollY) * (1 - SPRING_COEF^f)
// (SCROLL_FRICTION/SPRING_BACK_COEF/SPRING_FWD_COEF are themselves PER-60Hz-
// FRAME decay factors, e.g. velocity *= 0.9 each 1/60s tick; raising to the
// f-th power reproduces the same decay over f frame-equivalents regardless of
// how many times per second this is actually called.)
// Only integrates while the modal is fully open and not mid-touch --
// mirrors UpdateScroll()'s own gates (dropdown-open lock, active touch owns
// the value directly while held). Re-applies the widgets' pos.y from the
// freshly-integrated m_ScrollY afterward so Draw() (which also runs at
// present rate) and the widgets' own hit-rects agree this frame.
void SettingsScreen::UpdateRealtime(float dtSeconds) {
    if (dtSeconds < 0.0f) dtSeconds = 0.0f;
    if (dtSeconds > 0.1f) dtSeconds = 0.1f;   // clamp across stalls/tab-switches
    float f = dtSeconds * 60.0f;

    // --- Popup drop-in/out easing (OPENING/CLOSING only; a no-op sample
    // --- while ANIM_OPEN -- see EvalPopupAnim's ANIM_OPEN case) ---
    // m_AnimTimer is real elapsed seconds in the current phase, advanced HERE
    // (finer-grained than the 60Hz UpdateAnim() tick at >60fps) rather than
    // in UpdateAnim(), which now only INSPECTS it for the phase transition --
    // see UpdateAnim()'s comment. Not clamped to the phase duration: letting
    // it run slightly past 1.0 is harmless (EvalPopupAnim clamps its own
    // ratio) and UpdateAnim() resets it to 0.0f the instant it observes the
    // transition.
    if (m_AnimPhase != ANIM_OPEN) {
        m_AnimTimer += dtSeconds;
        float duration = (m_AnimPhase == ANIM_OPENING) ? kAnimOpenDuration : kAnimCloseDuration;
        float t01 = duration > 0.0f ? m_AnimTimer / duration : 1.0f;
        EvalPopupAnim(m_AnimPhase, t01, m_PopupOffsetY, m_BackdropAlpha,
                      m_CloseBtnOffX, m_CloseBtnOffY);

        // Widget touch/scroll stays gated to ANIM_OPEN (see Update()'s own
        // gate) -- but the widgets still ride the popup's drop-in/out motion,
        // so re-rewrite pos.y from the freshly-eased m_PopupOffsetY (scroll
        // offset is unchanged/0-ish during OPENING/CLOSING since UpdateScroll
        // never runs then).
        float off = m_ScrollY + m_PopupOffsetY;
        if (m_LangDrop)    m_LangDrop->pos.y    = m_LangBaseY        + off;
        if (m_MotionCb)    m_MotionCb->pos.y    = m_MotionCbBaseY    + off;
        if (m_SensSlider)  m_SensSlider->pos.y  = m_SensBaseY        + off;
        if (m_FpsCb)       m_FpsCb->pos.y       = m_FpsCbBaseY       + off;
        if (m_NativeFpsCb) m_NativeFpsCb->pos.y = m_NativeFpsCbBaseY + off;
        if (m_WideScreenCb) m_WideScreenCb->pos.y = m_WideScreenBaseY + off;
#if defined(FRUIT_PLATFORM_WII)
        if (m_LetterboxCb) m_LetterboxCb->pos.y = m_LetterboxBaseY + off;
#endif
        return;
    }
    if (m_LangDrop && m_LangDrop->IsOpen()) {
        // Mirrors UpdateScroll()'s own lock -- the dropdown panel owns all
        // touch full-screen while open, so nothing here should coast/spring.
        // Also forward the per-present tick to the open dropdown's own scroll
        // physics -- it is NOT AddControl'd to the HUD (a child widget driven
        // directly by this screen, see UiDropdown.h/SettingsScreen.h usage
        // notes), so HUD::UpdateRealtime's control-list walk never reaches
        // it; this screen must hand it the tick explicitly.
        if (m_LangDrop) m_LangDrop->UpdateRealtime(dtSeconds);
        return;
    }

    // --- Held-drag (moved): per-present damped-follow velocity compute ---
    // Task #13 -- byte-copy of UpdateScroll()'s Held-branch m_ScrollVel
    // formula (see the comment at that call site), but read at native present
    // rate via GetLivePos instead of once per 60Hz tick via ts->currY. Gated
    // to m_ScrollOwnsTouch (the SAME gate UpdateScroll()'s Held branch uses
    // to decide the scroll, not a sibling widget, owns this drag); if no
    // touch owns the scroll, m_ScrollVel is left as-is so a release fling
    // keeps decaying via the integrate step below, untouched.
    if (m_ScrollTouchId != -1 && m_ScrollOwnsTouch) {
        float liveX, liveY;
        if (Mortar::Touch::GetInstance().GetLivePos(m_ScrollTouchId, liveX, liveY)) {
            float delta = liveY - m_ScrollAnchorPos.y;
            m_ScrollVel = (m_ScrollY - (m_AnchorScroll + delta)) * DRAG_DELTA_FACTOR;
        }
    }

    // --- Integrate + friction (rate-independent) ---
    m_ScrollVel *= powf(SCROLL_FRICTION, f);
    m_ScrollY   += m_ScrollVel * f;

    // --- Spring-back at bounds (only while not touching) ---
    if (m_ScrollTouchId == -1) {
        if (m_ScrollY < 0.0f) {
            m_ScrollY += (0.0f - m_ScrollY) * (1.0f - powf(SPRING_BACK_COEF, f));
        } else if (m_ScrollY > m_MaxScroll) {
            m_ScrollY += (m_MaxScroll - m_ScrollY) * (1.0f - powf(1.0f - SPRING_FWD_COEF, f));
        }
    }

    // Re-rewrite pos.y from the freshly-integrated m_ScrollY, same formula
    // Update() uses, so Draw() and the widgets' own hit-rects/PollTouch this
    // present agree with the value just computed above.
    float off = m_ScrollY + m_PopupOffsetY;
    if (m_LangDrop)    m_LangDrop->pos.y    = m_LangBaseY        + off;
    if (m_MotionCb)    m_MotionCb->pos.y    = m_MotionCbBaseY    + off;
    if (m_SensSlider)  m_SensSlider->pos.y  = m_SensBaseY        + off;
    if (m_FpsCb)       m_FpsCb->pos.y       = m_FpsCbBaseY       + off;
    if (m_NativeFpsCb) m_NativeFpsCb->pos.y = m_NativeFpsCbBaseY + off;
    if (m_WideScreenCb) m_WideScreenCb->pos.y = m_WideScreenBaseY + off;
}
#endif

// Port specific: left-aligned (FONT_ALIGN_LEFT). All four top-level labels
// (LANGUAGE / MOTION MODE / SENSITIVITY / FPS COUNTER) anchor from the
// shared kLabelX at kLabelScale. Uses the TTF font (m_LangFont,
// gangofchinese.ttf -- same font UiDropdown's rows use), NOT
// game_work.pFontMain: pFontMain is the bitmap font_fruit_ninja.fnt, which
// is CAPS-ONLY (no lowercase glyphs). Switching to the TTF lets these
// render in natural mixed case instead of all-caps, and lets the
// sub-description below share the same font/look.
static void DrawSettingsLabel(Mortar::Font* font, const char* s, float x, float y,
                              float scale = kLabelScale) {
    if (!font) return;
    Mortar::Utf8StringIterator it(s);
    font->DrawString(it, x, y, 0.0f,
                     SettingsTextColour(), scale,
                     0.0f, 0.0f, Mortar::FONT_ALIGN_LEFT, NULL, 0.0f);
}

// Motion Mode sub-description -- smaller, dimmer caption drawn under the
// MOTION MODE label only. Same left anchor + font as the labels, own
// scale/colour.
static void DrawSettingsDesc(Mortar::Font* font, const char* s, float x, float y) {
    if (!font) return;
    Mortar::Utf8StringIterator it(s);
    font->DrawString(it, x, y, 0.0f,
                     kMotionDescColour, kMotionDescScale,
                     0.0f, 0.0f, Mortar::FONT_ALIGN_LEFT, NULL, 0.0f);
}

// Port specific: thin vertical scrollbar on the plate's right inner edge --
// height proportional to viewport/content ratio, position tracking
// m_ScrollY/m_MaxScroll. Drawn UNCLIPPED (no content scissor active), after
// the content scissor is disabled, using the same raw MatrixManager+Mesh
// scaled-quad idiom DrawDivider uses (a plain quad, no NineSlice). Reuses
// m_TexDivider (already loaded, no dedicated scrollbar art) tinted like the
// divider. No-op when content doesn't overflow the viewport
// (m_MaxScroll <= 0).
static void DrawScrollbar(Mortar::Texture* tex, float trackTop, float trackBottom,
                          float viewportH, float contentH, float scrollY, float maxScroll) {
    if (!tex || maxScroll <= 0.0f) return;

    static const float kBarX = kPlateHalfW - kPlateDestBorderX * 0.5f;
    static const float kBarW = 4.0f;
    static const Colour kBarColour(0x8A, 0x6A, 0x4A, 0xC0);

    float trackH = trackTop - trackBottom;
    float thumbH = trackH * (viewportH / contentH);
    if (thumbH < 8.0f) thumbH = 8.0f;
    if (thumbH > trackH) thumbH = trackH;

    float t = scrollY / maxScroll;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float thumbTop = trackTop - t * (trackH - thumbH);
    float thumbCy = thumbTop - thumbH * 0.5f;

    MatrixManager& mm = MatrixManager::GetInstance();
    Matrix44 mat = Matrix44::MakeScale(kBarW, thumbH, 0.0f);
    mat.GlobalTranslate44(kBarX, thumbCy, 0.0f);
    mm.GetWorldStack().Reset();
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();
    tex->Set();
    Mortar::Mesh::DrawQuadUnCached(kBarColour, NULL);
    tex->UnSet();
}

// Port specific: soft top/bottom edge fade for the scrolled content
// viewport, replacing a hard glScissor cut. Redraws a thin strip of the
// medbacking plate texture itself (m_Plate) directly on top of the just-
// scissored content, at the SAME screen position the background plate
// already shows there, with a per-vertex alpha ramp (opaque at the outer
// edge next to the frame, alpha 0 at the inner edge). Because the strip
// samples the identical medbacking texels the background plate draws at
// that spot, it composites 1:1 over the background and the scrolled
// content dissolves into the real parchment texture/vignette instead of a
// flat colour band (a flat fill never blends against the plate's own
// textured gradient).
//
// UV derivation mirrors the plate's own NineSlice::Draw call in Draw()
// (same kPlateSrcBorder*Px / kPlateDestBorder* / kPlateHalfW / kPlateHalfH
// constants) so the sampled texels track the plate rect exactly:
//   - The scroll viewport's content area is the plate's NineSlice CENTER
//     cell (inside the 9-slice borders): screen X in
//     [-kPlateHalfW+kPlateDestBorderX, +kPlateHalfW-kPlateDestBorderX],
//     screen Y in [-kViewportHalfH, +kViewportHalfH] (viewport half-height
//     equals the center cell's half-height by construction, see
//     kViewportHalfH's own note above).
//   - The center cell's UV rectangle (per NineSlice::Draw's u1/u2/v1/v2
//     split) is [fu, 1-fu] x [fv, 1-fv], where fu/fv are the border
//     fractions from the texture's own pixel dimensions.
//   - Each band's screen Y range maps LINEARLY onto that center V range;
//     U spans the full center U range (the plate's center cell stretches
//     horizontally to fill destW, so this strip stretches the same way).
//
// Per-vertex alpha (not a texture alpha channel -- medbacking.tex is
// opaque) via Mesh::DrawTriStrip's QUADCUSTOMVERTEX::colour, MODULATEd
// against the plate texture by Renderer::DrawTriStrip -- same convention
// SpeedControl::Draw uses for its own per-vertex-tinted textured strip.
static void DrawScrollFade(Mortar::Texture* plateTex,
                           float leftX, float rightX,
                           float edgeY, float height, bool topEdge) {
    if (!plateTex || height <= 0.0f || rightX <= leftX) return;

    float texW = (float)plateTex->GetWidth();
    float texH = (float)plateTex->GetHeight();
    if (texW <= 0.0f || texH <= 0.0f) return;

    // Center-cell UV rect, same fu/fv derivation as NineSlice::Draw.
    float fu = kPlateSrcBorderXPx / texW;
    float fv = kPlateSrcBorderYPx / texH;
    if (fu > 0.5f) fu = 0.5f;
    if (fv > 0.5f) fv = 0.5f;
    float centerU0 = fu, centerU1 = 1.0f - fu;
    float centerV0 = fv, centerV1 = 1.0f - fv;

    // Center-cell screen Y range (== the scroll viewport, by construction).
    float cellTop = kViewportHalfH, cellBottom = -kViewportHalfH;

    // Map this band's screen Y span linearly into the center cell's V range.
    // Screen Y and V run opposite directions here: NineSlice's midV0 (at
    // cellTop, high screen Y) == centerV0 (texture row just past the top
    // border, LOW v); midV1 (at cellBottom) == centerV1. So v = centerV0 +
    // (cellTop - y) / (cellTop - cellBottom) * (centerV1 - centerV0).
    float bandTop    = topEdge ? edgeY : (edgeY + height);
    float bandBottom = topEdge ? (edgeY - height) : edgeY;
    float vSpan = centerV1 - centerV0;
    float vAtTop    = centerV0 + (cellTop - bandTop)    / (cellTop - cellBottom) * vSpan;
    float vAtBottom = centerV0 + (cellTop - bandBottom) / (cellTop - cellBottom) * vSpan;

    // Outer edge (nearest the frame) is opaque, inner edge fades to 0.
    uint8_t aTop    = topEdge ? 255 : 0;
    uint8_t aBottom = topEdge ? 0   : 255;
    uint32_t packedTop    = Colour(255, 255, 255, aTop).PlatformColour();
    uint32_t packedBottom = Colour(255, 255, 255, aBottom).PlatformColour();

    QUADCUSTOMVERTEX v[4];
    // BL, BR, TL, TR -- matches Renderer::DrawQuad's own vertex order/UV
    // convention (quad top/high-Y samples vMin).
    v[0].x = leftX;  v[0].y = bandBottom; v[0].z = 0.0f; v[0].nx = 0.0f; v[0].ny = 0.0f; v[0].nz = 1.0f;
    v[0].u = centerU0; v[0].v = vAtBottom; v[0].colour = packedBottom;
    v[1].x = rightX; v[1].y = bandBottom; v[1].z = 0.0f; v[1].nx = 0.0f; v[1].ny = 0.0f; v[1].nz = 1.0f;
    v[1].u = centerU1; v[1].v = vAtBottom; v[1].colour = packedBottom;
    v[2].x = leftX;  v[2].y = bandTop;    v[2].z = 0.0f; v[2].nx = 0.0f; v[2].ny = 0.0f; v[2].nz = 1.0f;
    v[2].u = centerU0; v[2].v = vAtTop;    v[2].colour = packedTop;
    v[3].x = rightX; v[3].y = bandTop;    v[3].z = 0.0f; v[3].nx = 0.0f; v[3].ny = 0.0f; v[3].nz = 1.0f;
    v[3].u = centerU1; v[3].v = vAtTop;    v[3].colour = packedTop;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();
    plateTex->Set();
    Mortar::Mesh::DrawTriStrip(v, 4, true, NULL);
    plateTex->UnSet();
}

void SettingsScreen::Draw(float* hudScale) {
    (void)hudScale;
    MatrixManager& mm = MatrixManager::GetInstance();

    // ---- modal dark backdrop, full-screen (unclipped). Alpha tracks
    // ---- m_BackdropAlpha (eased fade in/out with the popup anim, see
    // ---- UpdateAnim()) instead of the old constant 160 -- 160/255 IS
    // ---- kBackdropTargetAlpha, so ANIM_OPEN rest state is unchanged. Not
    // ---- offset by m_PopupOffsetY -- the backdrop dims the whole screen
    // ---- regardless of where the plate currently sits. ----
    if (m_Backdrop.IsValid()) {
        mm.GetWorldStack().Reset();
        m_Backdrop->Set();
        // DIFFERS: widescreen widens the full-screen dim quad to
        // Layout::HalfWidth()*2 so the game/menu behind doesn't show through
        // at the sides on a widened field. Identity (480.0f) when
        // !Layout::IsWideLayout() / under __bada__.
        float backdropW = 480.0f;
#ifndef __bada__
        if (Layout::IsWideLayout()) {
            backdropW = Layout::HalfWidth() * 2.0f;
        }
#endif
        Matrix44 bgMat = Matrix44::MakeScale(backdropW, 320.0f, 1.0f);
        bgMat.GlobalTranslate44(_Vector3<float>(0.0f, 0.0f, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(bgMat);
        mm.UploadModelViewOnly();
        uint8_t backdropA = (uint8_t)(m_BackdropAlpha * 255.0f + 0.5f);
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, backdropA), NULL);
        m_Backdrop->UnSet();
    }

    // ---- plate panel: medbacking.tex, drawn as a 9-slice (full texture, no UV
    // cropping -- see kPlateSrcBorder*/kPlateDestBorder* above), UNCLIPPED.
    // Y offset by m_PopupOffsetY -- the whole plate translates with the popup
    // drop-in/out animation (see UpdateAnim()). ----
    if (m_Plate.IsValid()) {
        Mortar::NineSlice::Draw(m_Plate.Get(), 0.0f, m_PopupOffsetY,
                                kPlateHalfW * 2.0f, kPlateHalfH * 2.0f,
                                kPlateSrcBorderXPx, kPlateSrcBorderYPx,
                                kPlateDestBorderX, kPlateDestBorderY,
                                Colour::White);
    }

    // ---- content scissor: the plate's fixed viewport window (full ortho
    // ---- width, kViewportHalfH tall), SHIFTED by m_PopupOffsetY so the clip
    // ---- band moves WITH the plate during the drop-in/out animation --
    // ---- otherwise content would clip at the plate's REST screen position
    // ---- while the plate itself is still animating. Mirrors UiDropdown::
    // ---- Draw's own worldspace->pixel scissor mapping verbatim (see
    // ---- Renderer::SetClipRect for the centered-ortho -> viewport-pixel
    // ---- derivation); Renderer::SetClipRect no-ops on __bada__ / FN_GL_STUB
    // ---- so this call is unconditional here. ----
    if (Renderer* r = Renderer::GetInstance()) {
        r->SetClipRect(-240.0f, kViewportHalfH + m_PopupOffsetY,
                       240.0f, -kViewportHalfH + m_PopupOffsetY);
    }

    float off = m_ScrollY + m_PopupOffsetY;

    // ---- row-group dividers: Language/Motion Mode, Motion Mode/Widescreen,
    // ---- and Widescreen/Native Frame Rate (see kDividerY1/kDividerY1b/
    // ---- kDividerY2), scrolled. On FRUIT_PLATFORM_WII, Divider 2 instead
    // ---- separates Widescreen/FPS Counter directly -- NATIVE FRAME RATE is
    // ---- compiled out (see kFpsLabelY's FRUIT_PLATFORM_WII branch above),
    // ---- but Divider 2's own formula is unchanged/unconditional. Divider 1
    // ---- (Language/Motion Mode) is hidden on FRUIT_PLATFORM_WII along with
    // ---- the LANGUAGE row itself -- MOTION MODE is the first content row
    // ---- there, with no divider above it (mirrors the plate's own top
    // ---- padding, not a divider). ----
#if !defined(FRUIT_PLATFORM_WII)
    DrawDivider(kDividerY1 + off);
#endif
    DrawDivider(kDividerY1b + off);
    DrawDivider(kDividerY2 + off);

    // ---- left-column labels, scrolled ----
    // +7 vertical: DrawString positions text below the given y, so a label
    // centred on a row uses rowY + 7. Keeps each label vertically centred on
    // its widget. All four labels share kLabelX (left-aligned) so their left
    // edges line up, and share the TTF font (m_LangFont) for mixed-case
    // rendering.
    Mortar::Font* labelFont = m_LangFont.Get();
#if !defined(FRUIT_PLATFORM_WII)
    // Port specific: hidden along with the dropdown itself -- see
    // m_LangDrop's header comment.
    DrawSettingsLabel(labelFont, "LANGUAGE",     kLabelX, kLangLabelY   + 7.0f + off);
#endif
    DrawSettingsLabel(labelFont, "MOTION MODE",  kLabelX, kMotionLabelY + 7.0f + off);
    DrawSettingsDesc(labelFont, "Slow move aims, fast flick cuts",
                      kLabelX, kMotionDescY0 + 7.0f + off);
    DrawSettingsDesc(labelFont, "(pointer only)",
                      kLabelX, kMotionDescY1 + 7.0f + off);
    DrawSettingsLabel(labelFont, "SENSITIVITY", kSensLabelX, kSensLabelY + 7.0f + off);
    DrawSettingsLabel(labelFont, "WIDESCREEN", kLabelX, kWideScreenLabelY + 7.0f + off);
#if defined(FRUIT_PLATFORM_WII)
    DrawSettingsLabel(labelFont, "LETTERBOX", kLabelX, kLetterboxLabelY + 7.0f + off);
#endif
#if !defined(FRUIT_PLATFORM_WII)
    // Port specific: NATIVE FRAME RATE label+description hidden on
    // FRUIT_PLATFORM_WII along with the checkbox itself (see Init()'s
    // m_NativeFpsCb creation guard) -- native/display refresh is forced
    // always-on there, so there is nothing to label.
    DrawSettingsLabel(labelFont, "NATIVE FRAME RATE", kLabelX, kNativeFpsLabelY + 7.0f + off);
    DrawSettingsDesc(labelFont, "Smoother graphics for screens",
                      kLabelX, kNativeFpsDescY0 + 7.0f + off);
    DrawSettingsDesc(labelFont, "with higher frame rate",
                      kLabelX, kNativeFpsDescY1 + 7.0f + off);
#endif
    DrawSettingsLabel(labelFont, "FPS COUNTER", kLabelX, kFpsLabelY    + 7.0f + off);

    // ---- the four plain widgets, still inside the content scissor.
    // ---- pos.y was already rewritten (baseY + off) in Update(), so no
    // ---- offset math here. ----
    if (m_MotionCb)   m_MotionCb->Draw(hudScale);
    if (m_SensSlider) m_SensSlider->Draw(hudScale);
    if (m_WideScreenCb) m_WideScreenCb->Draw(hudScale);
#if defined(FRUIT_PLATFORM_WII)
    if (m_LetterboxCb) m_LetterboxCb->Draw(hudScale);
#endif
#if !defined(FRUIT_PLATFORM_WII)
    if (m_NativeFpsCb) m_NativeFpsCb->Draw(hudScale);
#endif
    if (m_FpsCb)      m_FpsCb->Draw(hudScale);
    // Bar draws inside the content scissor -- whether open or closed -- so
    // it clips/fades with the rest of the scrolling content, same as any
    // other plate row. Only the OPEN panel (drawn below, after the scissor
    // is disabled) is allowed to overflow the plate.
    if (m_LangDrop) {
        m_LangDrop->DrawBar(hudScale);
    }

    if (Renderer* r = Renderer::GetInstance()) r->ClearClipRect();

    // ---- top/bottom edge fade: soft dissolve over the just-scissored
    // ---- content, replacing the hard glScissor cut. Drawn AFTER content +
    // ---- AFTER the scissor is disabled (so it composites over whatever
    // ---- content was clipped, unclipped itself) but BEFORE the open
    // ---- dropdown panel (drawn last, below) so it can never cover it --
    // ---- the panel already draws unclipped on top of everything else in
    // ---- this function. No-op if content doesn't overflow the viewport
    // ---- (nothing is being cut, so nothing needs to fade). Each band is
    // ---- Port specific: both bands are now unconditional (only gated on
    // ---- the outer m_MaxScroll > 0 overflow check) rather than also gating
    // ---- each edge on m_ScrollY reaching that extreme -- avoids a visible
    // ---- pop of the fade appearing/disappearing right at the scroll limits.
    // ---- Harmless when there's nothing to fade: it just redraws the real
    // ---- parchment texels underneath. ----
    if (m_MaxScroll > 0.0f) {
        static const float kFadeHeight = 10.0f;
        static const float kFadeLeftX  = -kPlateHalfW + kPlateDestBorderX;
        static const float kFadeRightX =  kPlateHalfW - kPlateDestBorderX;
        DrawScrollFade(m_Plate.Get(), kFadeLeftX, kFadeRightX,
                      kViewportHalfH + m_PopupOffsetY, kFadeHeight, true);
        DrawScrollFade(m_Plate.Get(), kFadeLeftX, kFadeRightX,
                      -kViewportHalfH + m_PopupOffsetY, kFadeHeight, false);
    }

    // ---- scrollbar: thin quad on the plate's right inner edge, unclipped,
    // ---- drawn after the content scissor is disabled. No-op if content
    // ---- doesn't overflow the viewport. ----
    DrawScrollbar(m_TexDivider.Get(), kViewportHalfH + m_PopupOffsetY, -kViewportHalfH + m_PopupOffsetY,
                  kViewportH, kContentH, m_ScrollY, m_MaxScroll);

    // ---- open dropdown panel: drawn LAST, scissor already disabled above --
    // ---- UiDropdown::DrawPanel manages its OWN internal row-viewport
    // ---- glScissor (see UiDropdown.cpp) -- GL scissor state is not
    // ---- stacked, so SettingsScreen's own content scissor must already be
    // ---- off before this call, never nested around it. The bar was
    // ---- already drawn (clipped, above); this only draws the popup list.
    // ---- m_LangDrop->pos.y was already rewritten with m_PopupOffsetY in
    // ---- Update() (see the `off` there), so the panel follows the bar. ----
    if (m_LangDrop && m_LangDrop->IsOpen()) {
        m_LangDrop->DrawPanel(hudScale);
    }
}

// Port specific: draws one scratch_deviders.tex separator centred at
// centerY, spanning from the left-column label edge (kLabelX) to the
// right-column widget edge (kRightEdge + kCheckboxSide*0.5f -- the
// checkbox's own right edge, the widest right-column extent). Follows the
// same NineSlice-free scaled-quad idiom as ShopListItem::DrawDividers
// (src/hud/ShopListItem.cpp) -- MakeScale + GlobalTranslate44 + reset/set
// world stack + UploadModelViewOnly + DrawQuadUnCached. Native texture is
// 257x17; height 15 (slightly under native 17) avoids visibly stretching
// the art vertically.
void SettingsScreen::DrawDivider(float centerY) {
    if (!m_TexDivider.IsValid()) return;

    // Port specific: trims 10 world units off the right end only (left end
    // unchanged) -- the untrimmed span draws slightly too far into the
    // plate's right fade/edge.
    static const float kDividerRightX = kRightEdge + kCheckboxSide * 0.5f - 10.0f;
    static const float kDividerCenterX = (kLabelX + kDividerRightX) * 0.5f;
    static const float kDividerWidth   = kDividerRightX - kLabelX;
    // Muted brown-grey, alpha 0xB0 -- reads as a soft separator on the
    // parchment/wood plate rather than a hard rule line.
    static const Colour kDividerColour(0x8A, 0x6A, 0x4A, 0xB0);

    MatrixManager& mm = MatrixManager::GetInstance();
    Matrix44 mat = Matrix44::MakeScale(kDividerWidth, kDividerHeight, 0.0f);
    mat.GlobalTranslate44(kDividerCenterX, centerY, 0.0f);
    mm.GetWorldStack().Reset();
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();
    m_TexDivider->Set();
    Mortar::Mesh::DrawQuadUnCached(kDividerColour, NULL);
    m_TexDivider->UnSet();
}

void SettingsScreen::OnMotionToggle() {
    FN::g_MotionMode = m_MotionCb->IsChecked();
}

void SettingsScreen::OnFpsToggle() {
    FN::g_ShowFps = m_FpsCb->IsChecked();
}

void SettingsScreen::OnFpsCapToggle() {
    // checked == native/display refresh -> g_FpsCap60 (true == cap to 60) is
    // the checkbox's INVERSE. See m_NativeFpsCb's header comment.
    FN::g_FpsCap60 = !m_NativeFpsCb->IsChecked();
}

void SettingsScreen::OnWideScreenToggle() {
    // Port specific: writes the PREF only -- never Layout::SetWideLayout()
    // (the ACTIVE value) -- so the layout does not change live; it takes
    // effect on the next boot (LoadSettings' SetWideLayout call). See
    // Layout.h and this checkbox's header comment.
    Layout::SetWideLayoutPref(m_WideScreenCb->IsChecked());
    UpdateCloseButtonLabel();
}

#if defined(FRUIT_PLATFORM_WII)
void SettingsScreen::OnLetterboxToggle() {
    // Port specific: applies LIVE -- unlike OnWideScreenToggle(), there is no
    // pref/active split (GameWii.cpp's renderFrame() reads Layout::
    // IsLetterbox() fresh every frame; no already-built screen positions
    // depend on it), so this writes straight through with no restart warning
    // and no UpdateCloseButtonLabel() call.
    Layout::SetLetterbox(m_LetterboxCb->IsChecked());
}
#endif

void SettingsScreen::OnSensChanged() {
    FN::g_MotionSpeedThreshold = SliderToThreshold(m_SensSlider->GetValue());
}

void SettingsScreen::OnLangChanged() {
    int idx = m_LangDrop->GetSelected();
    game_work.languageFlag = (uint8_t)idx;
    Localisation::Load(Game::GetInstance()->data_dir.c_str(), idx);
    UpdateCloseButtonLabel();
}

// Port specific: rebakes m_pCloseButton's label to LSTR_QUIT ("QUIT") once
// EITHER game_work.languageFlag has diverged from m_InitialLanguageFlag
// (mirrors the langChanged check in Toggle()'s close branch) OR
// Layout::WideLayoutRestartPending() is true (the widescreen pref has
// diverged from the active/boot value) -- both require an app restart to
// take effect, so closing the modal in either state quits instead of just
// dismissing. Else LSTR_DJ_BACK_BUTTON ("BACK"). BSButton has no SetText of
// its own -- m_pLabel is only baked into m_pLabelBox once, in
// BSButton::Init() -- so this reaches into m_pLabelBox directly. SetText()
// marks the box dirty and triggers RebuildMeshes() on the next Draw(); the
// gradient/stroke/size/bounds styling applied once in Init()
// (SetGradient/ReshapeBounds/SetStroke/SetFontSize/FitIntoVerticalBounds) are
// persistent box state, not tied to the text, so they don't need reapplying.
void SettingsScreen::UpdateCloseButtonLabel() {
    if (!m_pCloseButton || !m_pCloseButton->m_pLabelBox) return;
    bool langChanged = (game_work.languageFlag != m_InitialLanguageFlag);
    bool restartPending = langChanged || Layout::WideLayoutRestartPending();
    // Port specific: Settings is a port-only screen (no binary counterpart), so its
    // close-button label stays ENGLISH regardless of locale (user preference; also
    // sidesteps the localized-font path for these two words).
    m_pCloseButton->m_pLabelBox->SetText(restartPending ? "QUIT" : "BACK");
}

// Port specific: m_pCloseButton's click callback -- runs Toggle()'s close
// path directly (this instance IS s_pSettings while open, so Toggle() would
// take the same branch; called directly here to avoid re-deriving that from
// the button's own scope).
void SettingsScreen::CloseCallback() {
    SettingsScreen::Toggle();
}
