//
// SettingsScreen -- Port specific in-game settings modal (see header note).
// Binds the dead-code CheckBox/SliderControl/ComboBox widget stack to
// host-only globals. No binary counterpart; not fidelity-constrained.
//
// Widget binding logic (delegate installs, combo polling, slider<->threshold
// mapping, language list) is lifted from tests/test_settings_interactive.cpp,
// the interactive dev harness that proved the same widgets against the same
// live globals.
//

#include "SettingsScreen.h"
#include "hud/CheckBox.h"
#include "hud/SliderControl.h"
#include "hud/ComboBox.h"
#include "hud/ListBox.h"
#include "hud/VerticalScroller.h"
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
static const float kLangLabelX  = -150.0f, kLangLabelY  =   85.0f;
// Port specific: kRightEdge is the shared right edge (x) every right-column
// control's visible art aligns to, so their right edges line up in a column
// instead of each widget's own centre-anchored x drifting independently.
// Chosen just inside the plate's content right bound (~+178, see kPlateHalfW
// note below) with a small margin. Per-widget x is then back-solved from
// (kRightEdge - <that widget's own centre-to-right-edge distance>), where the
// distance is read from each widget's real Draw()/ctor geometry (anchor is
// always pos == centre; see ComboBox/CheckBox/SliderControl below):
//   ComboBox:  bar half-width (kComboScaleX*0.5=60) + expand_arrow.tex width
//              (32, real asset -- see expand_arrow.tex note above) = 92 --
//              ComboBox::Draw's arrow quad sits fully to the RIGHT of the bar
//              (arrowCenter = pos.x + arrowW*0.5 + barW*0.5).
//   CheckBox:  hardcoded 128x64 quad (CheckBox::Draw), but checked.tex/
//              unchecked.tex centre their opaque art in the transparent
//              128x64 canvas -- visible square is texels x=[47,80] (34px),
//              so the ART's right edge is only 16 units right of pos.x
//              (texel 80 vs quad centre texel 64), not 64.
//   SliderControl: track (box.tex, stretched to kSensScale via
//              MakeScale -- see ctor) half-width 64; thumb (slider_will.tex,
//              real asset 32x32) protrudes slightly further when
//              m_CurrentValue==m_MaxValue -- thumb centre reaches
//              pos.x + trackW*0.5 - thumbPosW*0.5 (SliderControl::Draw's
//              thumbPos.x formula), thumbPosW=thumbW*74/128=18.5, so thumb
//              right edge = pos.x + 64 - 9.25 + 16 = pos.x + 70.75 (the
//              governing, slightly-wider-than-track extent).
static const float kRightEdge = 175.0f;

// ComboBox: bar height 38; box.tex (the binary's shared field/row/track
// texture -- see Init()'s LoadContent comment) is stretched to this bar
// size via ComboBox::Draw's MakeScale(m_DrawWidth, m_DrawHeight, 1), grown
// from the texture's native proportions so the bar's visual mass reads
// closer to the checkbox's ~34-unit-tall visible square (kMotionCbX/kFpsCbX
// note below) instead of looking thin next to it. x is centre of the bar
// (ComboBox::pos); back-solved
// so bar+arrow right edge == kRightEdge (see kRightEdge note: 92 -- unaffected
// by the height change, see below).
//
// ComboBox::Draw (src/hud/ComboBox.cpp @0x001687f4) scales the arrow quad to
// (arrowW * size.x, m_DrawHeight) where arrowW = s_expandArrow->GetWidth(),
// i.e. the arrow's WIDTH comes from the loaded expand_arrow.tex's own native
// pixel width (fixed, 32 for the real asset) -- it does NOT scale with
// m_DrawHeight/kComboScaleY (only the arrow's on-screen HEIGHT tracks the
// bar). So the kRightEdge note's 92 = barHalfWidth(60) + arrowWidth(32) holds
// unchanged after growing kComboScaleY; only the arrow's rendered height grows
// (cosmetic, no layout effect).
//
// kComboY nudged 85->82 (-3) so the taller bar's top (kComboY + kComboScaleY*
// 0.5 = 82+19 = 101) still lands exactly on the plate's content top bound
// (kPlateHalfH(130) - kPlateDestBorderY(29) = 101, see kPlateHalfH note below)
// instead of overflowing by 3 units at the old kComboY=85. Gap to the MOTION
// MODE checkbox's visible-art top (kMotionCbY(35) + ~17 half of its ~34-unit
// square = 52) is combo bottom(82-19=63) - 52 = 11 units, still clear.
static const float kComboX      =   kRightEdge - 92.0f, kComboY      =   82.0f;
static const float kComboScaleX =  120.0f, kComboScaleY =   38.0f;
static const uint8_t kComboVisibleRows = 6;
// Combo value font size (m_Width). 18 (was 16) to match the taller bar's
// visual mass; still fits the longest native name -- "PORTUGUES (BR)" --
// without spilling into the caret.
static const uint16_t kComboWidth      = 18;

static const float kMotionLabelX = -150.0f, kMotionLabelY =   35.0f;
// CheckBox: quad is 128x64 (CheckBox::Draw's MakeScale(128,64,1), 1:1
// texel:unit), but checked.tex/unchecked.tex pack their opaque art centred
// in a transparent 128x64 canvas -- visible square spans texels x=[47,80]
// (34px wide), NOT the full quad width. Back-solved so the ART's right edge
// (pos.x + (80-64)) == kRightEdge, not the padded quad's right edge
// (pos.x+64) -- aligning to the quad edge left a ~48-unit gap of transparent
// padding, which is why the checkbox looked mis-aligned vs the ComboBox/
// SliderControl despite matching kRightEdge on paper (see kRightEdge note).
// -16 nudged to -21: the -16 solve still overshot kRightEdge slightly in
// practice, so back off an extra 5 units for a visual right-edge match with
// the ComboBox/SliderControl column above/below it.
static const float kMotionCbX    =   kRightEdge - 21.0f, kMotionCbY    =   35.0f;

static const float kSensLabelX = -120.0f, kSensLabelY = -15.0f;
// SliderControl: x is centre of the track; back-solved so the thumb's
// max-value right edge (pos.x+70.75) == kRightEdge (see kRightEdge note).
static const float kSensX      =  kRightEdge - 70.75f, kSensY      = -15.0f;
static const int   kSensMin = 0, kSensMax = 100;

static const float kFpsLabelX = -150.0f, kFpsLabelY = -65.0f;
// CheckBox: same anchor/visible-art offset as kMotionCbX above (-21, see note).
static const float kFpsCbX    =   kRightEdge - 21.0f, kFpsCbY     = -65.0f;

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
// combo, half-height 16 at y=85) reaches y=101, which needs a content
// half-height of >=101+28=129 to clear the 28-unit padding -- wider than the
// old 110, so the panel is grown to 130 (widget rows unchanged; nothing
// currently overflows the width padding, x=150 vs a 178 content half-width).
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
static const float kSensThresholdMax = 30.0f;

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
        s_pSettings->Init();
        if (game_work.mHud) {
            game_work.mHud->AddControl(s_pSettings, false);
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
    : m_LangCombo(0)
    , m_MotionCb(0)
    , m_SensSlider(0)
    , m_FpsCb(0)
    , m_pCloseButton(0)
{
    m_LangLast = -1;
    // TOP_MOST (0x800): the modal must draw over ALL main-screen HUD. POST_ACTOR
    // (0x80) draws early -- default/buttons/modal/slider/top-most all paint over
    // it (see HUDLayer.h GameDraw order). 0x800 is the last, unconditional pass.
    // The combo's spawned ListBox is also 0x800 and AddControl'd later, so the
    // open dropdown still layers above this panel.
    m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;
}

SettingsScreen::~SettingsScreen() {
    Release();
}

void SettingsScreen::Init() {
    m_Active = 1;

    // ---- widget textures: real art, staged at build time from ----
    // ---- assets/ui-widgets/*.svg by fn_asset_staging (mandatory --  ----
    // ---- the build fails if generation fails, see svg-to-webp.mjs). ----
    m_TexCheckboxOn  = Mortar::TextureManager::LoadLocalisedTexture("checked.tex");
    m_TexCheckboxOff = Mortar::TextureManager::LoadLocalisedTexture("unchecked.tex");
    // box.tex is the binary's single shared field/row/track texture -- ComboBox
    // (@0x00168b3c), ListBox (@0x00194fdc), and SliderControl (@0x001b7bc0) all
    // LoadLocalisedTexture the SAME "box.tex" (Ghidra-confirmed). One load here,
    // reused for both the combo bar and the slider track injections below.
    m_TexTrack       = Mortar::TextureManager::LoadLocalisedTexture("box.tex");
    m_TexThumb       = Mortar::TextureManager::LoadLocalisedTexture("slider_will.tex");
    // No dedicated ListBox-row art beyond box.tex (this screen's dropdown never
    // opens -- kComboVisibleRows keeps it collapsed) -- solid white tint canvas.
    m_TexRow         = MakeSolidTex(255, 255, 255, 255, 8, 8);
    m_TexScrTrack    = Mortar::TextureManager::LoadLocalisedTexture("vbar.tex");
    m_TexScrThumb    = Mortar::TextureManager::LoadLocalisedTexture("vslider.tex");
    m_TexScrArrow    = Mortar::TextureManager::LoadLocalisedTexture("arrow.tex");
    m_TexArrow       = Mortar::TextureManager::LoadLocalisedTexture("expand_arrow.tex");
    // Port specific: modal dim backdrop -- solid black, alpha applied via vertex
    // tint (Colour(0,0,0,160) in Draw()), not baked into the texture. No real
    // widget counterpart.
    m_Backdrop       = MakeSolidTex(0, 0, 0, 255, 8, 8);

    // Combo bar: box.tex, same shared field texture as the slider track above.
    m_TexBar = m_TexTrack;

    CheckBox::SetTexturesForTest(m_TexCheckboxOn, m_TexCheckboxOff);
    SliderControl::SetTexturesForTest(m_TexTrack, m_TexThumb);
    ComboBox::SetTexturesForTest(m_TexBar, m_TexArrow);
    ListBox::SetTexturesForTest(m_TexRow);
    VerticalScroller::SetTexturesForTest(m_TexScrTrack, m_TexScrThumb, m_TexScrArrow);

    m_Plate = Mortar::TextureManager::LoadLocalisedTexture("medbacking.tex");

    // ---- language model ----
    m_LangItems.clear();
    for (int i = 0; i < kLanguageCount; ++i) {
        m_LangItems.push_back(std::string(kLanguageNames[i]));
    }

    uint16_t langDefault = (uint16_t)(game_work.languageFlag < kLanguageCount ? game_work.languageFlag : 0);
    m_LangLast = (int)langDefault;

    // NULL combo header label: the screen draws its own left-column "LANGUAGE"
    // label, so suppress the ComboBox's built-in (yellow, right-of-bar) header
    // to avoid a duplicate.
    m_LangCombo = new ComboBox(Vec3(kComboX, kComboY, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                               m_LangItems, langDefault, NULL,
                               kComboVisibleRows, kComboWidth,
                               (uint16_t)kComboScaleX, (uint16_t)kComboScaleY);
    m_LangCombo->SetTextColour(SettingsTextColour());

    // Native language names need CJK/Hangul/Cyrillic glyphs the bitmap
    // font_fruit_ninja.fnt doesn't ship; switch the combo (and its dropdown
    // ListBox, via ComboBox::Update's font propagation) to the TTF font.
    m_LangFont = Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (m_LangFont.IsValid()) {
        m_LangCombo->SetFont(m_LangFont.Get());
    }

    m_LangCombo->Init();

    // ---- checkboxes / slider, seeded from live globals ----
    m_MotionCb = new CheckBox(Vec3(kMotionCbX, kMotionCbY, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "");
    m_MotionCb->SetCheckedForTest(FN::g_MotionMode);
    m_MotionCb->Init();

    m_FpsCb = new CheckBox(Vec3(kFpsCbX, kFpsCbY, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "");
    m_FpsCb->SetCheckedForTest(FN::g_ShowFps);
    m_FpsCb->Init();

    int sens0 = ThresholdToSlider(FN::g_MotionSpeedThreshold);
    m_SensSlider = new SliderControl(Vec3(kSensX, kSensY, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                                     "", kSensMin, kSensMax, 24, sens0);
    m_SensSlider->Init();

    m_MotionCb->SetOnToggleForTest(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnMotionToggle));
    m_FpsCb->SetOnToggleForTest(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnFpsToggle));
    m_SensSlider->SetOnValueChangedForTest(Mortar::Delegate0<void>::Make(this, &SettingsScreen::OnSensChanged));

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
    m_pCloseButton->m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;
}

void SettingsScreen::Release() {
    delete m_LangCombo;   m_LangCombo    = 0;
    delete m_MotionCb;    m_MotionCb     = 0;
    delete m_SensSlider;  m_SensSlider   = 0;
    delete m_FpsCb;       m_FpsCb        = 0;
    delete m_pCloseButton; m_pCloseButton = 0;

    CheckBox::UnloadContent();
    SliderControl::UnloadContent();
    ComboBox::UnloadContent();
    ListBox::UnloadContent();
    VerticalScroller::UnloadContent();

    m_TexCheckboxOn.SetNull();
    m_TexCheckboxOff.SetNull();
    m_TexTrack.SetNull();
    m_TexThumb.SetNull();
    m_TexBar.SetNull();
    m_TexArrow.SetNull();
    m_TexRow.SetNull();
    m_TexScrTrack.SetNull();
    m_TexScrThumb.SetNull();
    m_TexScrArrow.SetNull();
    m_Plate.SetNull();
    m_Backdrop.SetNull();
    m_LangFont.SetNull();
    m_CloseTex.SetNull();

    HUDControl3d::Release();
}

void SettingsScreen::PollCombo() {
    if (!m_LangCombo) return;
    std::string* sel = m_LangCombo->SelectedIter();
    if (!sel || m_LangItems.empty()) return;
    int idx = (int)(sel - &m_LangItems[0]);
    if (idx < 0 || idx >= (int)m_LangItems.size()) return;
    if (idx == m_LangLast) return;
    m_LangLast = idx;
    game_work.languageFlag = (uint8_t)idx;
    Localisation::Load(Game::GetInstance()->data_dir.c_str(), idx);
}

void SettingsScreen::Update(float dt) {
    if (m_LangCombo)    m_LangCombo->Update(dt);
    if (m_MotionCb)     m_MotionCb->Update(dt);
    if (m_SensSlider)   m_SensSlider->Update(dt);
    if (m_FpsCb)        m_FpsCb->Update(dt);
    if (m_pCloseButton) m_pCloseButton->Update(dt);

    PollCombo();
}

static void DrawSettingsLabel(const char* s, float x, float y) {
    if (!game_work.pFontMain.IsValid()) return;
    Mortar::Utf8StringIterator it(s);
    game_work.pFontMain->DrawString(it, x, y, 0.0f,
                                    SettingsTextColour(), 18.0f,
                                    0.0f, 0.0f, 1, NULL, 0.0f);
}

void SettingsScreen::Draw(float* hudScale) {
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

    // ---- left-column labels ----
    // +7 vertical: pFontMain->DrawString positions text below the given y, so a
    // label centred on a row uses rowY + 7 (mirrors ComboBox::Draw's own text at
    // pos.y + size.y*7). Keeps each label vertically centred on its widget.
    DrawSettingsLabel("LANGUAGE",     kLangLabelX,   kLangLabelY   + 7.0f);
    DrawSettingsLabel("MOTION MODE",  kMotionLabelX, kMotionLabelY + 7.0f);
    DrawSettingsLabel("SENSITIVITY",  kSensLabelX,   kSensLabelY   + 7.0f);
    DrawSettingsLabel("FPS COUNTER",  kFpsLabelX,    kFpsLabelY    + 7.0f);

    // ---- widgets ----
    if (m_LangCombo)  { m_LangCombo->PreDraw(hudScale);  m_LangCombo->Draw(hudScale); }
    if (m_MotionCb)   { m_MotionCb->PreDraw(hudScale);   m_MotionCb->Draw(hudScale); }
    if (m_SensSlider) { m_SensSlider->PreDraw(hudScale); m_SensSlider->Draw(hudScale); }
    if (m_FpsCb)      { m_FpsCb->PreDraw(hudScale);      m_FpsCb->Draw(hudScale); }

    // ---- close button: bomb icon + "QUIT"-style text label, own Draw() ----
    // ---- draws both the textured quad and m_pLabelBox (BSButton::Draw). ----
    if (m_pCloseButton) { m_pCloseButton->PreDraw(hudScale); m_pCloseButton->Draw(hudScale); }
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

// Port specific: m_pCloseButton's click callback -- runs Toggle()'s close
// path directly (this instance IS s_pSettings while open, so Toggle() would
// take the same branch; called directly here to avoid re-deriving that from
// the button's own scope).
void SettingsScreen::CloseCallback() {
    SettingsScreen::Toggle();
}
