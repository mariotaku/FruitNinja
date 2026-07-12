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
#include "hud/WidgetPlaceholderArt.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "game/GameWork.h"
#include "engine/util/Localisation.h"
#include "engine/util/Delegate.h"
#include "render/MatrixManager.h"
#include "render/NineSlice.h"
#include "render/Font.h"
#include "render/Utf8StringIterator.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "debug/DebugFlags.h"
#include "Game.h"

using namespace fn_widget_art;

// ---------------------------------------------------------------------------
// Real widget art (assets/ui-widgets/*.svg -> FruitNinjaBada/Data/textures/*.tex,
// see tools/assets/svg_to_tex.py) is preferred when it loads; procedurally-drawn
// placeholder art (WidgetPlaceholderArt.h) is the fallback for hosts with no SVG
// rasterizer installed (LoadLocalisedTexture returns a null SmartPtr on missing
// file). Port specific: no binary counterpart, this screen has none.
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
// UPPERCASE: font_fruit_ninja.fnt (game_work.pFontMain) ships only 92 glyphs
// (space/punctuation/digits/uppercase A-Z/underscore/accented uppercase
// Latin-1) -- no lowercase a-z. Lowercase text silently renders as nothing.
// Port-improvement screen, no fidelity constraint on display casing.
// ---------------------------------------------------------------------------
static const char* const kLanguageNames[] = {
    "ENGLISH_US", "ENGLISH_UK", "FRENCH", "SPANISH", "GERMAN", "ITALIAN",
    "DUTCH", "SWEDISH", "DANISH", "NORWEGIAN", "FINNISH", "KOREAN",
    "JAPANESE", "CHINESE", "TRADITIONAL CHINESE", "LATIN SPANISH", "POLISH",
    "PORTUGUESE (PT)", "PORTUGUESE (BR)", "RUSSIAN", "ARABIC", "FAKE DEBUG LANGUAGE"
};
static const int kLanguageCount = 22;

static int ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
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
static const uint16_t kComboWidth      = 20;

static const float kMotionLabelX = -150.0f, kMotionLabelY =   35.0f;
static const float kMotionCbX    =   95.0f, kMotionCbY    =   35.0f;

static const float kSensLabelX = -120.0f, kSensLabelY = -15.0f;
// Indented row: the slider sits in the right column (like the checkboxes at x=95)
// so its 120px track clears the long "SENSITIVITY" label to its left.
static const float kSensX      =  100.0f, kSensY      = -15.0f;
static const int   kSensMin = 0, kSensMax = 100;

static const float kFpsLabelX = -150.0f, kFpsLabelY = -65.0f;
static const float kFpsCbX    =   95.0f, kFpsCbY     = -65.0f;

// Plate quad -- built from dialog_box.tex scaled to this footprint (see Draw()).
// dialog_box.tex's wooden frame is inset from the quad edge, so the plate is sized
// generously (280 tall, fits the 320 viewport) to keep all four rows -- including
// the top combo + its bar-height expand arrow -- inside the inner frame.
static const float kPlateHalfW = 220.0f;
static const float kPlateHalfH = 140.0f;
// 9-slice borders for the plate (dialog_box.tex is 256x128 with a wooden frame):
// srcBorder = corner inset in TEXELS, destBorder = corner size in WORLD units.
static const float kPlateBorderSrc = 45.0f;
static const float kPlateBorderDst = 52.0f;

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

SettingsScreen::SettingsScreen()
    : m_LangCombo(0)
    , m_MotionCb(0)
    , m_SensSlider(0)
    , m_FpsCb(0)
{
    m_LangLast = -1;
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;
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

    m_Plate = Mortar::TextureManager::LoadLocalisedTexture("dialog_box.tex");

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
    m_LangCombo->SetTextColour(Colour(255, 255, 255, 255));
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
}

void SettingsScreen::Release() {
    delete m_LangCombo;  m_LangCombo  = 0;
    delete m_MotionCb;   m_MotionCb   = 0;
    delete m_SensSlider; m_SensSlider = 0;
    delete m_FpsCb;      m_FpsCb      = 0;

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
    if (m_LangCombo)  m_LangCombo->Update(dt);
    if (m_MotionCb)   m_MotionCb->Update(dt);
    if (m_SensSlider) m_SensSlider->Update(dt);
    if (m_FpsCb)      m_FpsCb->Update(dt);

    PollCombo();
}

static void DrawSettingsLabel(const char* s, float x, float y) {
    if (!game_work.pFontMain.IsValid()) return;
    Mortar::Utf8StringIterator it(s);
    game_work.pFontMain->DrawString(it, x, y, 0.0f,
                                    Colour(255, 255, 255, 255), 18.0f,
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

    // ---- plate panel (9-slice so the wooden frame/corners stay crisp) ----
    if (m_Plate.IsValid()) {
        Mortar::NineSlice::Draw(m_Plate.Get(), 0.0f, 0.0f,
                                kPlateHalfW * 2.0f, kPlateHalfH * 2.0f,
                                kPlateBorderSrc, kPlateBorderDst, Colour::White);
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
