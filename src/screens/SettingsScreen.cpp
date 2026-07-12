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
// Real widget art (assets/ui-widgets/*.svg -> FruitNinjaBada/Data/textures/*.tex,
// rasterized at build time by tools/assets/svg-to-webp.mjs, see fn_asset_staging
// in CMakeLists.txt) is preferred when it loads; procedurally-drawn placeholder
// art (WidgetPlaceholderArt.h) is the fallback for hosts with no node/sharp
// available (LoadLocalisedTexture returns a null SmartPtr on missing file).
// Port specific: no binary counterpart, this screen has none.
// ---------------------------------------------------------------------------
static Mortar::SmartPtr<Mortar::Texture> LoadOrPlaceholder(
    const char* name, const Mortar::SmartPtr<Mortar::Texture>& placeholder)
{
    Mortar::SmartPtr<Mortar::Texture> tex = Mortar::TextureManager::LoadLocalisedTexture(name);
    if (tex.IsValid()) {
        return tex;
    }
    return placeholder;
}

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
static const float kComboX      =   55.0f, kComboY      =   85.0f;
// Bar 32 tall: ComboBox draws the caret cell (expand_arrow.tex) at
// (textureWidth x barHeight); a 32-tall bar renders the 32x32 caret 1:1 (no
// stretch) and matches the 32px checkbox/knob height. combo_bar.tex is 128x32
// so the value field is unstretched too.
static const float kComboScaleX =  120.0f, kComboScaleY =   32.0f;
static const uint8_t kComboVisibleRows = 6;
// Combo value font size (m_Width). 16 (not 20) so the longest native name --
// "PORTUGUES (BR)" -- fits the value cell without spilling into the caret.
static const uint16_t kComboWidth      = 16;

static const float kMotionLabelX = -150.0f, kMotionLabelY =   35.0f;
static const float kMotionCbX    =   95.0f, kMotionCbY    =   35.0f;

static const float kSensLabelX = -120.0f, kSensLabelY = -15.0f;
// Indented row: the slider sits in the right column (like the checkboxes at x=95)
// so its 120px track clears the long "SENSITIVITY" label to its left.
static const float kSensX      =  100.0f, kSensY      = -15.0f;
static const int   kSensMin = 0, kSensMax = 100;

static const float kFpsLabelX = -150.0f, kFpsLabelY = -65.0f;
static const float kFpsCbX    =   95.0f, kFpsCbY     = -65.0f;

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

    // ---- widget textures: real art (assets/ui-widgets/*.svg) preferred, ----
    // ---- procedural placeholder (WidgetPlaceholderArt.h) as fallback.    ----
    // Sizes mirror the balanced set in test_settings_interactive.cpp.
    m_TexCheckboxOn  = LoadOrPlaceholder("checked.tex",  MakeCheckboxTex(true,  128, 64, 22));
    m_TexCheckboxOff = LoadOrPlaceholder("unchecked.tex", MakeCheckboxTex(false, 128, 64, 22));
    m_TexTrack       = LoadOrPlaceholder("_dialog_box.tex", MakeSolidTex(120, 120, 120, 255, 120, 16));
    m_TexThumb       = LoadOrPlaceholder("slider_will.tex", MakeCircleTex(240, 140, 20, 30, 30));
    m_TexRow         = MakeSolidTex(255, 255, 255, 255, 8, 8);
    m_TexScrTrack    = LoadOrPlaceholder("vbar.tex",    MakeSolidTex(70, 70, 90, 255, 8, 8));
    m_TexScrThumb    = LoadOrPlaceholder("vslider.tex", MakeSolidTex(200, 200, 210, 255, 8, 8));
    m_TexScrArrow    = LoadOrPlaceholder("arrow.tex",   MakeArrowTex(180, 180, 200, 24, 24));
    // Wider arrow (ComboBox scales it to bar height ~55): a 40px-wide triangle
    // reads as a proper expander, not a thin spike.
    m_TexArrow       = LoadOrPlaceholder("expand_arrow.tex",
                                          MakeArrowTex(255, 210, 40, 40, 40, /*pointDown*/ true));
    // Port specific: modal dim backdrop -- solid black, alpha applied via vertex
    // tint (Colour(0,0,0,160) in Draw()), not baked into the texture.
    m_Backdrop       = MakeSolidTex(0, 0, 0, 255, 8, 8);

    // Combo bar: the designed recessed field (combo_bar.tex); fall back to the
    // shipped blank_dialog_box.tex if the SVG art wasn't generated on this host.
    m_TexBar = LoadOrPlaceholder("combo_bar.tex",
                                 Mortar::TextureManager::LoadLocalisedTexture("blank_dialog_box.tex"));

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
