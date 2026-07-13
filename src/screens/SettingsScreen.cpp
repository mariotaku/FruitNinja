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
#include "Game.h"

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
// ---------------------------------------------------------------------------
static const char* const kLanguageNames[] = {
    "English (US)", "English (UK)", "Fran\303\247ais", "Espa\303\261ol", "Deutsch", "Italiano",
    "Nederlands", "Svenska", "Dansk", "Norsk", "Suomi", "\355\225\234\352\265\255\354\226\264",
    "\346\227\245\346\234\254\350\252\236", "\344\270\255\346\226\207", "\347\271\201\351\253\224\344\270\255\346\226\207", "Espa\303\261ol (LA)", "Polski",
    "Portugu\303\252s (PT)", "Portugu\303\252s (BR)", "\320\240\321\203\321\201\321\201\320\272\320\270\320\271", "Arabic", "Debug"
};
static const int kLanguageCount = 22;

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

// Port specific: left-column row Y positions use an EVEN vertical rhythm.
// The plate's usable content range is +-101 (kPlateHalfH(130) -
// kPlateDestBorderY(29), see kPlateHalfH note below). kRowGap is the
// top-of-plate / bottom-of-plate padding (above LANGUAGE, below FPS
// COUNTER) -- unchanged from the original single-uniform-gap derivation.
// MOTION MODE's row is the tallest allocation (label + 2-line description +
// SENSITIVITY, 4 lines total) laid out with uniform kRowLineGap spacing
// between each of its 4 lines; the other two rows (LANGUAGE, FPS COUNTER)
// are single-line.
//
// Divider clearance: each divider (kDividerHeight 15 tall, see DrawDivider)
// gets a DEDICATED 10-unit gap on each side, beyond the neighbouring label's
// baseline -- not shared with kRowGap/kRowLineGap. So the baseline-to-
// baseline gap spanning a divider is kDividerGap = 2*10 (pad) +
// kDividerHeight = 35; the divider sits centred in it, giving exactly 10
// units of clearance from each of its edges to the adjacent label baseline.
//
// Derivation (content half-height H=101, 3 internal kRowLineGap steps
// spanning Motion Mode's block, 2 dedicated kDividerGap steps):
//   kRowGap = (H*2 - 3*kRowLineGap - 2*kDividerGap) / 2
// so that: kRowGap (top pad) + kLangLabelY row + kDividerGap (Divider1 gap,
// padded) + [Motion block, 3*kRowLineGap tall] + kDividerGap (Divider2 gap,
// padded) + kFpsLabelY row + kRowGap (bottom pad) exactly fills [-H, H] at
// the label-baseline level (kRowGap grew from the old uniform-gap value to
// absorb the extra divider padding -- see MEASURE note in the .cpp's
// overflow analysis, content stays within the plate bound using widget
// extents too).
static const float kRowLineGap  = 24.0f;
static const float kDividerPad  = 10.0f;
static const float kDividerGap  = 2.0f * kDividerPad + 15.0f; // 15 = kDividerHeight (DrawDivider)
static const float kRowGap      = (101.0f * 2.0f - 3.0f * kRowLineGap - 2.0f * kDividerGap) / 2.0f;
static const float kLangLabelY  =   101.0f - kRowGap;
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
// kComboY re-centres the dropdown bar on the LANGUAGE label (the bar is
// taller than a text baseline, so a small downward nudge visually centres
// it under the label rather than sitting flush with the baseline). Kept
// small (-2, down from an earlier -8) so the bar's bottom edge
// (kComboY - kComboScaleY*0.5f) still clears Divider 1 -- see kDividerY1.
static const float kComboX      =   kRightEdge - kComboScaleX * 0.5f, kComboY      =   kLangLabelY - 2.0f;
static const uint8_t kComboVisibleRows = 6;
// Combo value/row text scale (Font::DrawString's scale param, font-native
// pixel size) -- fits the longest native name -- "PORTUGUES (BR)" -- without
// spilling into the caret.
static const float kComboTextScale = 18.0f;

// Divider 1 sits centred in kDividerGap below LANGUAGE and above MOTION
// MODE -- 10 units of dedicated clearance beyond each label's baseline on
// both sides (see kDividerGap derivation above), not the shared kRowGap.
static const float kDividerY1 = kLangLabelY - kDividerGap * 0.5f;

// Motion Mode row: TALLEST allocation -- label, then a clear gap, then a
// 2-line description, then SENSITIVITY, all spaced kRowLineGap apart (the
// same uniform line spacing used throughout this 4-line block).
static const float kMotionLabelY = kDividerY1 - kDividerGap * 0.5f;
// UiCheckbox: side 36; x back-solved so the box's right edge
// (pos.x + side*0.5f) == kRightEdge.
static const float kCheckboxSide = 36.0f;

// Motion Mode description -- smaller, dimmer TWO-line caption drawn under
// the MOTION MODE label (same kLabelX left margin). Manually split at the
// natural comma break ("Slow move aims, fast flick cuts" / "(pointer
// only)") rather than word-wrapped, since Font has no wrap-measurement path
// wired for this TTF path (see FindAdvanceOfNextWord TODO in Font.h). Only
// this row gets a sub-description; the other three rows stay single-line.
static const float kMotionDescY0    = kMotionLabelY - kRowLineGap;
static const float kMotionDescY1    = kMotionDescY0 - kRowLineGap;
static const float kMotionDescScale = 11.0f;
// Dimmer than SettingsTextColour()'s brown (0x6F,0x46,0x1E) -- same hue,
// lower alpha so it reads as secondary/caption text under the bold label.
static const Colour kMotionDescColour(0x6F, 0x46, 0x1E, 0xA0);

// kMotionCbY: vertically centred on the MOTION MODE label + 2-line
// description sub-block (kMotionLabelY..kMotionDescY1).
static const float kMotionCbX = kRightEdge - kCheckboxSide * 0.5f;
static const float kMotionCbY = (kMotionLabelY + kMotionDescY1) * 0.5f;

// SENSITIVITY is a TOP-LEVEL row (same kLabelX left margin, same
// kLabelScale as every other row), the 4th and last line of Motion Mode's
// block -- kRowLineGap below the description's 2nd line, same spacing as
// every other step in this block.
static const float kSensLabelY = kMotionDescY1 - kRowLineGap;
// UiSlider track -- wide, thin horizontal groove (NineSlice box.tex).
static const float kSensTrackW = 120.0f, kSensTrackH = 16.0f;
// UiSlider: x is centre of the track; back-solved so the track's right edge
// (pos.x + trackW*0.5f) == kRightEdge, same pattern every other
// right-column widget uses (checkbox, combo bar). Centred on the
// SENSITIVITY label alone.
static const float kSensX      =  kRightEdge - kSensTrackW * 0.5f, kSensY      = kSensLabelY;
static const int   kSensMin = 0, kSensMax = 100;

// Divider 2 sits centred in kDividerGap below SENSITIVITY and above FPS
// COUNTER (same dedicated 10-unit clearance as Divider 1).
static const float kDividerY2 = kSensLabelY - kDividerGap * 0.5f;

// FPS COUNTER: last top-level row, kDividerGap*0.5 below Divider 2 (same
// dedicated divider clearance as every other divider-adjacent row).
static const float kFpsLabelY = kDividerY2 - kDividerGap * 0.5f;
// UiCheckbox: same anchor as kMotionCbX above.
static const float kFpsCbX    =   kRightEdge - kCheckboxSide * 0.5f, kFpsCbY     = kFpsLabelY;

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

// Close button at the SCREEN's bottom-right corner (not the panel corner) --
// same on-screen position as PauseScreen's quit button (bomb at (215,-135),
// text offset (-29,3) pulling the label onto the bomb face), so the two read
// identically. Sits outside/below the plate, on the modal backdrop.
static const float kCloseBtnX = 215.0f;
static const float kCloseBtnY = -135.0f;
static const Vec3  kCloseTextOffset(-29.0f, 3.0f, 0.0f);

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

// Port specific: single open/close path, shared by the ESC key handler
// (src/GameSDL.cpp) and the MainScreen settings button (src/screens/
// MainScreen.cpp::SettingsCallback). No binary counterpart -- this screen
// has none.
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
        if (game_work.mHud) {
            // Port specific: capture input while the settings modal is open --
            // see HUD::SetInputModal (src/hud/HUD.h).
            game_work.mHud->SetInputModal(s_pSettings);
        }
    } else {
        if (game_work.mHud) {
            game_work.mHud->SetInputModal(NULL);
        }
        s_pSettings->SetPendingRemoval();
        s_pSettings = NULL;
    }
}

bool SettingsScreen::IsOpen() {
    return s_pSettings != NULL;
}

SettingsScreen::SettingsScreen()
    : m_LangDrop(0)
    , m_MotionCb(0)
    , m_SensSlider(0)
    , m_FpsCb(0)
    , m_pCloseButton(0)
{
    // TOP_MOST (0x800): the modal must draw over ALL main-screen HUD. Every
    // AddControl'd widget below is ALSO set to TOP_MOST so HUD::Update's
    // modal input-capture gate (see HUD.h SetInputModal) still delivers
    // input to them while this screen holds the modal (see HUD.cpp
    // partOfModal check: ctrl == m_pInputModal || ctrl->m_LayerFlags ==
    // HUD_LAYER_TOP_MOST). Draw order among same-layer TOP_MOST controls is
    // pure HUD control-list (AddControl) order -- see Init() for the
    // plate-then-widgets-then-dropdown ordering.
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

    int langDefault = (int)(game_work.languageFlag < kLanguageCount ? game_work.languageFlag : 0);

    // Native language names need CJK/Hangul/Cyrillic glyphs the bitmap
    // font_fruit_ninja.fnt doesn't ship; switch the dropdown to the TTF font.
    m_LangFont = Mortar::Font::Create("fontstruetype/gangofchinese.ttf");

    m_LangDrop = new UiDropdown(Vec3(kComboX, kComboY, 0.0f), m_LangItems, langDefault,
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

    // ---- checkboxes / slider, seeded from live globals ----
    m_MotionCb = new UiCheckbox(Vec3(kMotionCbX, kMotionCbY, 0.0f), kCheckboxSide, FN::g_MotionMode);
    m_MotionCb->SetBoxTexture(m_TexBox);
    m_MotionCb->SetCheckGlyph(m_TexCheck);
    m_MotionCb->SetOnChange(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnMotionToggle));
    m_MotionCb->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;

    m_FpsCb = new UiCheckbox(Vec3(kFpsCbX, kFpsCbY, 0.0f), kCheckboxSide, FN::g_ShowFps);
    m_FpsCb->SetBoxTexture(m_TexBox);
    m_FpsCb->SetCheckGlyph(m_TexCheck);
    m_FpsCb->SetOnChange(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnFpsToggle));
    m_FpsCb->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;

    int sens0 = ThresholdToSlider(FN::g_MotionSpeedThreshold);
    m_SensSlider = new UiSlider(Vec3(kSensX, kSensY, 0.0f), kSensMin, kSensMax, sens0);
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
        Vec3(kCloseBtnX, kCloseBtnY, 0.0f),
        // Port specific: no dedicated "CLOSE" string table entry exists (this
        // screen has no binary counterpart). LSTR_DJ_BACK_BUTTON ("BACK") is
        // reused instead of LSTR_QUIT ("QUIT") -- other screens (AboutScreen,
        // GameModeScreen, DojoScreen) already reuse this same id for their
        // back/exit buttons, and "BACK" reads correctly for closing a modal.
        GETSTRING(LSTR_DJ_BACK_BUTTON, 0),
        Vec3(1.0f, 1.0f, 1.0f)
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

    // ---- AddControl every widget to the HUD, TOP_MOST, in draw order ----
    // HUD::Draw has no per-control sort key -- controls sharing a layer mask
    // draw in HUD control-list (AddControl) order. This screen itself was
    // already AddControl'd by Toggle() BEFORE Init() runs (see Toggle()),
    // so its own Draw() (backdrop + plate + labels) is already first in the
    // list; add the widgets after it, dropdown last, so the open dropdown
    // panel overlays every other widget.
    if (game_work.mHud) {
        game_work.mHud->AddControl(m_MotionCb, false);
        game_work.mHud->AddControl(m_FpsCb, false);
        game_work.mHud->AddControl(m_SensSlider, false);
        game_work.mHud->AddControl(m_pCloseButton, false);
        game_work.mHud->AddControl(m_LangDrop, false);
    }
}

void SettingsScreen::Release() {
    // AddControl'd widgets are torn down via SetPendingRemoval (HUD's own
    // Update sweep deletes each next tick, matching GameModeScreen::
    // RemoveButtons' pattern) rather than a direct delete here -- deleting
    // synchronously while the widget is still linked into game_work.mHud's
    // control list would leave a dangling pointer in that list.
    if (m_LangDrop)    { m_LangDrop->SetPendingRemoval();    m_LangDrop    = 0; }
    if (m_MotionCb)    { m_MotionCb->SetPendingRemoval();    m_MotionCb    = 0; }
    if (m_SensSlider)  { m_SensSlider->SetPendingRemoval();  m_SensSlider  = 0; }
    if (m_FpsCb)       { m_FpsCb->SetPendingRemoval();       m_FpsCb       = 0; }
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
    (void)dt;

    // Port specific: while the dropdown panel is open, gate out the other
    // AddControl'd widgets (checkboxes/slider/close) so they neither receive
    // input nor draw over the panel -- HUD::Update/Draw both skip inactive
    // controls (m_Active gate). The dropdown itself stays always-active.
    // Restored to active the frame the panel closes.
    uint8_t othersActive = m_LangDrop && m_LangDrop->IsOpen() ? 0 : 1;
    if (m_MotionCb)    m_MotionCb->m_Active    = othersActive;
    if (m_SensSlider)  m_SensSlider->m_Active  = othersActive;
    if (m_FpsCb)       m_FpsCb->m_Active       = othersActive;
    if (m_pCloseButton) m_pCloseButton->m_Active = othersActive;
}

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

void SettingsScreen::Draw(float* hudScale) {
    (void)hudScale;
    MatrixManager& mm = MatrixManager::GetInstance();

    // ---- modal dark backdrop, full-screen ----
    if (m_Backdrop.IsValid()) {
        mm.GetWorldStack().Reset();
        m_Backdrop->Set();
        Matrix44 bgMat = Matrix44::MakeScale(480.0f, 320.0f, 1.0f);
        bgMat.GlobalTranslate44(Vec3(0.0f, 0.0f, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(bgMat);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 160), NULL);
        m_Backdrop->UnSet();
    }

    // ---- plate panel: medbacking.tex, drawn as a 9-slice (full texture, no UV
    // cropping -- see kPlateSrcBorder*/kPlateDestBorder* above) ----
    if (m_Plate.IsValid()) {
        Mortar::NineSlice::Draw(m_Plate.Get(), 0.0f, 0.0f,
                                kPlateHalfW * 2.0f, kPlateHalfH * 2.0f,
                                kPlateSrcBorderXPx, kPlateSrcBorderYPx,
                                kPlateDestBorderX, kPlateDestBorderY,
                                Colour::White);
    }

    // ---- row-group dividers: between Language/Motion Mode, and between
    // ---- Sensitivity/FPS Counter (see kDividerY1/kDividerY2) ----
    DrawDivider(kDividerY1);
    DrawDivider(kDividerY2);

    // ---- left-column labels ----
    // +7 vertical: DrawString positions text below the given y, so a label
    // centred on a row uses rowY + 7. Keeps each label vertically centred on
    // its widget (widgets draw separately, via the HUD). All four labels
    // share kLabelX (left-aligned) so their left edges line up, and share
    // the TTF font (m_LangFont) for mixed-case rendering.
    Mortar::Font* labelFont = m_LangFont.Get();
    DrawSettingsLabel(labelFont, "LANGUAGE",     kLabelX, kLangLabelY   + 7.0f);
    DrawSettingsLabel(labelFont, "MOTION MODE",  kLabelX, kMotionLabelY + 7.0f);
    DrawSettingsDesc(labelFont, "Slow move aims, fast flick cuts",
                      kLabelX, kMotionDescY0 + 7.0f);
    DrawSettingsDesc(labelFont, "(pointer only)",
                      kLabelX, kMotionDescY1 + 7.0f);
    DrawSettingsLabel(labelFont, "SENSITIVITY", kLabelX, kSensLabelY + 7.0f);
    DrawSettingsLabel(labelFont, "FPS COUNTER", kLabelX, kFpsLabelY    + 7.0f);
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

    static const float kDividerHeight = 15.0f;
    static const float kDividerRightX = kRightEdge + kCheckboxSide * 0.5f;
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

void SettingsScreen::OnSensChanged() {
    FN::g_MotionSpeedThreshold = SliderToThreshold(m_SensSlider->GetValue());
}

void SettingsScreen::OnLangChanged() {
    int idx = m_LangDrop->GetSelected();
    game_work.languageFlag = (uint8_t)idx;
    Localisation::Load(Game::GetInstance()->data_dir.c_str(), idx);
}

// Port specific: m_pCloseButton's click callback -- runs Toggle()'s close
// path directly (this instance IS s_pSettings while open, so Toggle() would
// take the same branch; called directly here to avoid re-deriving that from
// the button's own scope).
void SettingsScreen::CloseCallback() {
    SettingsScreen::Toggle();
}
