// test_settings_interactive.cpp -- INTERACTIVE dev harness for the ported
// settings widgets (CheckBox / SliderControl / ComboBox+ListBox+VerticalScroller).
//
// Unlike the screenshot render tests (test_settings_widgets_render /
// test_dropdown_render), this opens a REAL clickable window and drives the
// widgets from live mouse/touch input, each widget bound to an actual app
// parameter. Tick a checkbox / drag a slider / pick a language and watch the
// bound parameter change (per-change stdout log + an on-screen readout).
//
//   Row              Widget          Bound parameter
//   ---------------  --------------  -----------------------------------------
//   Language         ComboBox        game_work.languageFlag (+ Localisation::Load)
//   Sound            CheckBox        game_work.m_bSoundOn   (+ SoundManager::SyncMutes)
//   Music            CheckBox        game_work.m_bMusicOn   (+ SyncMutes)
//   Motion mode      CheckBox        FN::g_MotionMode
//   Flick            SliderControl   FN::g_MotionSpeedThreshold (0..30)
//   SFX vol          SliderControl   SoundManager::SetSFXVolume   (0..100 -> 0..1)
//   Mus vol          SliderControl   SoundManager::SetMusicVolume (0..100 -> 0..1)
//
// Usage:
//   test_settings_interactive              # interactive (default): window shown, ESC/close quits
//   test_settings_interactive --headless [--frames N]   # cheap CI compile/run smoke (default 30 frames)
//   test_settings_interactive --interactive             # force window (same as default)
//   Keyboard: ESC quits; M toggles Motion mode (safety net -- see input notes below).
//   Mouse wheel scrolls the row list; click-drag on empty list background also scrolls.
//
// -----------------------------------------------------------------------------
// LAYOUT (GOAL 1 -- balanced single-column settings list):
//
// A single column of rows, each: [label (left column)] [widget (right column)],
// consistent ROW HEIGHT + even vertical pitch. The LANGUAGE combo sits in a
// FIXED HEADER strip at the top; the six checkbox/slider rows live in a
// SCROLLABLE viewport below it, above the bottom readout strip.
//
// Widget visual weights are balanced so the checkbox box, slider track, and
// combo bar read as siblings (all ~30-44 px tall). See the size constants below.
//
// LAYOUT (GOAL 2 -- scrollable row list, port-improvement harness feature):
//   // DIFFERS: no native Mortar widget hosts an arbitrary scrolling list of
//   controls, so this scroll behaviour is a harness-level addition, not a port
//   of any binary function. Rows are laid out ONCE in content space; every frame
//   their pos.y is set to (contentBaseY - scrollOffset) so BOTH the draw and the
//   touch hit-test move together (widgets read their own pos). glScissor clips
//   the viewport so scrolled-out rows don't overdraw the header/readout.
//
// The LANGUAGE combo is intentionally kept in the fixed header (not scrolled).
// That sidesteps the dropdown-follow problem: the ListBox a ComboBox spawns is a
// SEPARATE HUD control created (at tap time) relative to the combo's then-current
// pos, and the harness does not own that pointer to shift it per frame. With the
// combo fixed, its dropdown always opens at the right place. See the dropdown
// caveat note at the bottom of this file.
//
// -----------------------------------------------------------------------------
// INPUT PUMP (the crux -- makes widgets respond to the pointer):
//
// The widgets hit-test Mortar::Touch::states1[] (via TouchInRegion / IsTouchDown)
// and read either game_work.m_FingerSpawnPos[slot] (CheckBox / SliderControl /
// ComboBox / VerticalScroller) or game_work.worldPos (ListBox row hover+commit --
// confirmed against the binary, not a port choice: GameTaskInput's
// PointerMoveCallback writes Game.worldPos.x/y on every real move event) for the
// touch position. Three facts drive the pump:
//   1. states1 is fed by the game's normal path: DrainSDLEvent() pushes each SDL
//      finger/mouse event into the Mortar::Touch ring; DispatchForSimTick() calls
//      Touch::Update(0) which drains the ring into states1. We reuse those exact
//      entry points via a local InputTranslatorSDL (no hand-rolled touch). On the
//      desktop mouse maps to a real Touch slot (0-7) -- the same path the live
//      shop/scroll widgets use -- so TouchInRegion sees it.
//   2. m_FingerSpawnPos is NEVER written with a live position anywhere in the port
//      (it is only z-aged in GameUpdate + read by these dead-code widgets; the
//      binary's InputSink writer is unported). So after DispatchForSimTick we copy
//      states1[slot].currX/currY into m_FingerSpawnPos[slot] ourselves --
//      SliderControl::UpdateTouchPosition / CheckBox capture then work.
//   3. game_work.worldPos is likewise never written on the SDL backend (no
//      InputTranslatorSDL write site despite the aspirational comment on
//      PointerMoveCallback). SyncWorldPos() mirrors the first active touch slot
//      into it each frame so ListBox's row hover/commit hit-test sees a live
//      position -- without this, tapping a dropdown ROW silently did nothing.
//
// SCROLL vs WIDGET input: on a fresh mouse press we decide whether the press
// landed on a widget's hit-rect. If it did NOT, subsequent vertical mouse motion
// while held is treated as a LIST DRAG (adjusts scrollOffset) and is NOT mirrored
// into m_FingerSpawnPos/worldPos (so it can't accidentally drive a widget). If it
// DID land on a widget, the normal input pump runs and the drag drives that
// widget. The mouse wheel always scrolls.
//
// Coordinate space (docs/engine/coordinate-system.md): draw-space and touch-space
// share the same axes -- X horizontal [-240,240], Y vertical [-160,160], +Y up --
// so a click lands where the widget is drawn (no conversion needed). The window is
// 960x640, i.e. exactly 2.0 window px per game unit; glScissor uses that factor.
//
// Motion-mode note: toggling Motion ON changes the raw-mouse routing inside
// DrainSDLEvent. In practice the synthesized-finger path still drives a Touch
// slot so the other widgets keep responding, but the keyboard 'M' toggle is
// provided as a safety net regardless.
// -----------------------------------------------------------------------------
//
// Manual dev harness: no CI assertions. Registered so --headless is a cheap
// compile/run check; the interactive run is launched by the orchestrator.
//
// C++03-clean host-only test TU (kept lambda/auto/range-for free; cross-build
// never sees tests).

#include "test_harness.h"
#include "hud/CheckBox.h"
#include "hud/SliderControl.h"
#include "hud/ComboBox.h"
#include "hud/ListBox.h"
#include "hud/VerticalScroller.h"
#include "hud/HUD.h"
#include "game/GameWork.h"
#include "engine/audio/SoundManager.h"
#include "engine/util/Localisation.h"
#include "engine/util/Delegate.h"
#include "input/Touch.h"
#include "platform/InputTranslatorSDL.h"
#include "debug/DebugFlags.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "render/gl_funcs.h"
#include "render/Font.h"
#include "render/Utf8StringIterator.h"
#include "asset/Texture.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include "widget_placeholder_art.h"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <SDL.h>

using namespace fn_widget_art;

// Language display list in languageFlag order (mirrors StringTable's file-static
// kLanguageSuffix[]; index == flag). Only 0..13 have shipped .str data; higher
// flags fall back to english_us inside Localisation::Load -- fine for a harness.
//
// UPPERCASE: font_fruit_ninja.fnt (game_work.pFontMain) ships only 92 glyphs --
// space, punctuation, digits, uppercase A-Z, underscore, and accented uppercase
// Latin-1/OE/Euro (see .fnt `chars count=92`; ids run 32..95 then jump straight
// to 96/161.. -- no lowercase a-z (97-122) at all). Any lowercase letter makes
// Font::GetCharTemplate return null, so Font::DrawString emits zero quads for
// it -- a fully-lowercase string like "english_us" renders as nothing, not
// small/wrong-coloured text. This is a port-improvement test harness (no
// fidelity constraint on display casing), so displaying uppercase is the
// correct, non-band-aid fix -- not a colour/size/matrix workaround.
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

static float ClampF(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ===========================================================================
// BALANCED WIDGET SIZES (GOAL 1). Chosen so the checkbox box, slider track, and
// combo bar read at comparable visual weight (~30-44 px tall) and align in two
// consistent columns. See per-widget report in the task summary.
//
//   Checkbox box  : 44 px (placeholder halfBox 22; inside the +/-36 x / +/-28.5 y
//                   binary hit-rect -- the 128x64 draw quad and hit-rect are
//                   binary-faithful and unchanged).
//   Slider track  : 190 x 16 px (thin bar); thumb 30 x 30 circle.
//   Combo bar     : 190 x 32 px (matches slider track width; bar height a touch
//                   taller so the two-line combo text fits).
// ===========================================================================
static const int   kCbBoxHalf   = 22;    // checkbox box half-extent in placeholder px (box = 44)
static const int   kTrackW      = 190;   // slider track width  (px == units, size=1)
static const int   kTrackH      = 16;    // slider track height
static const int   kThumbD      = 30;    // slider thumb circle diameter
static const int   kBarW        = 190;   // combo bar width  (ctor textScaleX)
static const int   kBarH        = 32;    // combo bar height (ctor textScaleY)
static const int   kArrowD      = 18;    // combo expand-arrow size

// Two consistent columns (game-space X). Left = label anchor column, right =
// widget column. The checkbox draws centered on its pos with its label to the
// RIGHT (pos.x + 64); the slider/combo draw centered with their label to the
// right of the track. To keep the two widget families visually aligned we place
// their pos at a common right-column X and let each widget's own label offset
// handle the text. Labels are drawn by the harness in the LEFT column so every
// row reads "LABEL .......... [widget]".
static const float kLabelX      = -220.0f;  // left column: row label left edge
static const float kWidgetX     =  -30.0f;  // right column: widget pos.x (bar/track/box centred here)

// ===========================================================================
// SCROLL VIEWPORT (GOAL 2). Fixed header holds the LANGUAGE combo; the readout
// strip sits at the very bottom; the scrollable rows live between.
// Game-space Y is [-160, 160], +Y up.
// ===========================================================================
// Viewport spans below the combo header down to just above the readout strip.
// glScissor is derived from the actual drawable size at draw time (HiDPI-safe);
// the nominal window is 960x640 == 2.0 window px per game unit.
static const float kViewTop     =   95.0f;   // top of scroll viewport (below combo header)
static const float kViewBottom  = -128.0f;   // bottom of scroll viewport (above readout)

static const float kRowPitch    =   58.0f;   // even vertical pitch (clears checkbox hit-rect +/-28.5 with margin)
// Content-space Y of row 0's centre. At scrollOffset==0 row 0 sits just below the
// viewport top; pos.y = contentY + scrollOffset, so a POSITIVE scrollOffset shifts
// the content UP (revealing later, lower-in-list rows).
static const float kRowFirst    =   66.0f;

// ---------------------------------------------------------------------------
// A scrollable row: a widget plus its fixed content-space Y (its natural, un-
// scrolled centre). Each frame we set widget->pos.y = contentY + scrollOffset
// so the draw and hit-test both move. Kept POD/struct (no C++11) for the C++03
// test TU.
// ---------------------------------------------------------------------------
struct ScrollRow {
    HUDControl* widget;    // the control whose pos we shift
    const char* label;     // harness-drawn left-column label
    float       contentY;  // natural content-space centre Y
};

// ---------------------------------------------------------------------------
// Binds each widget's live state to its app parameter. Checkbox/slider methods
// are installed as the widgets' real m_OnToggle / m_OnValueChanged delegates and
// fire from inside HUD::Update; the combo has no per-select delegate on the
// collapsed bar, so its selection is polled once per frame (PollCombo).
// ---------------------------------------------------------------------------
struct SettingsHarness {
    CheckBox*      soundCb;
    CheckBox*      musicCb;
    CheckBox*      motionCb;
    SliderControl* flickSl;
    SliderControl* sfxSl;
    SliderControl* musSl;
    ComboBox*      langCombo;
    std::vector<std::string>* langItems;
    int            lastLangFlag;
    std::string    dataDir;

    void OnSoundToggle() {
        game_work.m_bSoundOn = soundCb->IsChecked();
        Mortar::SoundManager::GetInstance().SyncMutes();
        std::printf("[settings_interactive] Sound -> %s\n",
                    game_work.m_bSoundOn ? "ON" : "OFF");
    }
    void OnMusicToggle() {
        game_work.m_bMusicOn = musicCb->IsChecked();
        Mortar::SoundManager::GetInstance().SyncMutes();
        std::printf("[settings_interactive] Music -> %s\n",
                    game_work.m_bMusicOn ? "ON" : "OFF");
    }
    void OnMotionToggle() {
        FN::g_MotionMode = motionCb->IsChecked();
        std::printf("[settings_interactive] Motion mode -> %s\n",
                    FN::g_MotionMode ? "ON" : "OFF");
    }
    void OnFlickChanged() {
        FN::g_MotionSpeedThreshold = (float)flickSl->GetValue();
        std::printf("[settings_interactive] Flick threshold -> %d\n", flickSl->GetValue());
    }
    void OnSfxChanged() {
        Mortar::SoundManager::GetInstance().SetSFXVolume((float)sfxSl->GetValue() / 100.0f);
        std::printf("[settings_interactive] SFX volume -> %d%%\n", sfxSl->GetValue());
    }
    void OnMusicVolChanged() {
        Mortar::SoundManager::GetInstance().SetMusicVolume((float)musSl->GetValue() / 100.0f);
        std::printf("[settings_interactive] Music volume -> %d%%\n", musSl->GetValue());
    }
    void PollCombo() {
        std::string* sel = langCombo->SelectedIter();
        if (!sel || langItems->empty()) return;
        int idx = (int)(sel - &(*langItems)[0]);
        if (idx < 0 || idx >= (int)langItems->size()) return;
        if (idx == lastLangFlag) return;
        lastLangFlag = idx;
        game_work.languageFlag = (uint8_t)idx;
        Localisation::Load(dataDir.c_str(), idx);
        std::printf("[settings_interactive] Language -> %d (%s)\n",
                    idx, (*langItems)[idx].c_str());
    }
};

// The port never writes m_FingerSpawnPos with a live position (see header note).
// Mirror the drained Touch state into it so the widgets read a valid press pos.
static void SyncFingerSpawnPos() {
    Mortar::Touch& t = Mortar::Touch::GetInstance();
    for (int s = 0; s < Mortar::Touch::MAX_SLOTS; ++s) {
        const Mortar::TouchState& st = t.states1[s];
        if (st.phase < 1) {  // active (just-pressed or held)
            game_work.m_FingerSpawnPos[s].x = st.currX;
            game_work.m_FingerSpawnPos[s].y = st.currY;
            game_work.m_FingerSpawnPos[s].z = 0.0f;
        }
    }
}

// ListBox::Update (hover + row-commit hit-test) reads game_work.worldPos, not
// m_FingerSpawnPos -- confirmed against the binary (GameTaskInput's
// PointerMoveCallback writes Game.worldPos.x/y on every move event; see the
// v1.6.1 spec comment on PointerMoveCallback in src/game/GameTaskInput.cpp).
// InputTranslatorSDL does NOT write worldPos on this port (that comment is
// aspirational), so the harness mirrors the live pointer position here every
// frame -- same source as SyncFingerSpawnPos, just the single global world-pos
// slot the dropdown widgets read instead of the per-finger capture array.
static void SyncWorldPos() {
    Mortar::Touch& t = Mortar::Touch::GetInstance();
    for (int s = 0; s < Mortar::Touch::MAX_SLOTS; ++s) {
        const Mortar::TouchState& st = t.states1[s];
        if (st.phase < 1) {  // active (just-pressed or held)
            game_work.worldPos.x = st.currX;
            game_work.worldPos.y = st.currY;
            game_work.worldPos.z = 0.0f;
            break;  // binary tracks a single world touch pos, not per-slot
        }
    }
}

static void DrawReadoutLine(const char* s, float x, float y, float size) {
    if (!game_work.pFontMain.IsValid()) return;
    Mortar::Utf8StringIterator it(s);
    game_work.pFontMain->DrawString(it, x, y, 0.0f,
                                    Colour(255, 255, 255, 255), size,
                                    0.0f, 0.0f, 1, NULL, 0.0f);
}

// Does a game-space point (mouse mapped to touch coords) land inside a widget's
// clickable hit-rect at its CURRENT (scrolled) pos? Mirrors each widget's own
// Update() hit-region so a press-on-empty-background can be told apart from a
// press-on-widget. Combo bar hit-rect matches ComboBox::Update.
// Is the combo's dropdown currently open? The ListBox a ComboBox spawns is a
// separate HUD control; scan the HUD's control list for one. When a dropdown is
// open we must NOT drag-scroll (a tap in the viewport may be a dropdown-row
// select, which the normal input pump has to see).
static bool DropdownOpen() {
    if (!game_work.mHud) return false;
    std::list<HUDControl*>::iterator it = game_work.mHud->controls.begin();
    for (; it != game_work.mHud->controls.end(); ++it) {
        if (dynamic_cast<ListBox*>(*it)) return true;
    }
    return false;
}

static bool PointInAnyWidget(const std::vector<ScrollRow>& rows, float px, float py) {
    for (size_t i = 0; i < rows.size(); ++i) {
        HUDControl* w = rows[i].widget;
        // CheckBox: pos.x +/- 36, pos.y +/- 28.5 (hardcoded).
        CheckBox* cb = dynamic_cast<CheckBox*>(w);
        if (cb) {
            if (px >= cb->pos.x - 36.0f && px <= cb->pos.x + 36.0f &&
                py >= cb->pos.y - 28.5f && py <= cb->pos.y + 28.5f) return true;
            continue;
        }
        SliderControl* sl = dynamic_cast<SliderControl*>(w);
        if (sl) {
            float halfX = sl->TrackWidth() * 0.5f;
            float halfY = (sl->size.y * 60.0f) * 0.5f;
            if (px >= sl->pos.x - halfX && px <= sl->pos.x + halfX &&
                py >= sl->pos.y - halfY && py <= sl->pos.y + halfY) return true;
            continue;
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
    fn::TestHarness h(argc, argv, "settings_interactive");
    h.SetInteractiveDefault(true);   // default: show the window (manual harness)
    h.SetInitFrames(90);             // burn-in so GameInitialise loads pFontMain
    if (!h.ParseFlags()) return 1;
    if (!h.InitComponent()) return 1;

    if (!game_work.mHud) {
        std::fprintf(stderr, "FAIL: game_work.mHud null after boot\n");
        return 1;
    }

    // Local translator -- reuse the game's exact DrainSDLEvent/DispatchForSimTick path.
    InputTranslatorSDL translator;
    translator.Init();

    // -----------------------------------------------------------------------
    // Placeholder textures (real widget art is not shipped -- see the shared
    // header). Inject BEFORE constructing widgets (the SliderControl ctor reads
    // the track/thumb dims; the others read on Draw).
    //
    // Sizes are the BALANCED set (GOAL 1) -- see the size-constant block above.
    // SliderControl's ctor sizes m_TrackWidth/Height directly from these texture
    // pixel dims * size (size == 1 for every slider below), so texture px ==
    // on-screen px here.
    // -----------------------------------------------------------------------
    Mortar::SmartPtr<Mortar::Texture> texCheckboxOn  = MakeCheckboxTex(true,  128, 64, kCbBoxHalf);
    Mortar::SmartPtr<Mortar::Texture> texCheckboxOff = MakeCheckboxTex(false, 128, 64, kCbBoxHalf);
    Mortar::SmartPtr<Mortar::Texture> texTrack     = MakeSolidTex(120, 120, 120, 255, kTrackW, kTrackH);
    Mortar::SmartPtr<Mortar::Texture> texThumb     = MakeCircleTex(240, 140, 20, kThumbD, kThumbD);
    Mortar::SmartPtr<Mortar::Texture> texBar       = MakeSolidTex(40, 40, 60, 255, 8, 8);
    // Expand-arrow quad width = arrowTex.GetWidth() * combo size.x (ComboBox::Draw);
    // size.x == 1 for langCombo below, so texture px == on-screen px. Small glyph
    // next to the bar.
    Mortar::SmartPtr<Mortar::Texture> texArrow     = MakeArrowTex(255, 210, 40, kArrowD, kArrowD);
    Mortar::SmartPtr<Mortar::Texture> texRow       = MakeSolidTex(255, 255, 255, 255, 8, 8);
    Mortar::SmartPtr<Mortar::Texture> texScrTrack  = MakeSolidTex(70, 70, 90, 255, 8, 8);
    Mortar::SmartPtr<Mortar::Texture> texScrThumb  = MakeSolidTex(200, 200, 210, 255, 8, 8);
    Mortar::SmartPtr<Mortar::Texture> texScrArrow  = MakeArrowTex(180, 180, 200, 24, 24);

    CheckBox::SetTexturesForTest(texCheckboxOn, texCheckboxOff);
    SliderControl::SetTexturesForTest(texTrack, texThumb);
    ComboBox::SetTexturesForTest(texBar, texArrow);
    ListBox::SetTexturesForTest(texRow);
    VerticalScroller::SetTexturesForTest(texScrTrack, texScrThumb, texScrArrow);

    // -----------------------------------------------------------------------
    // Language model (must outlive the ComboBox -- stored by pointer).
    // -----------------------------------------------------------------------
    std::vector<std::string> langItems;
    for (int i = 0; i < kLanguageCount; ++i) langItems.push_back(std::string(kLanguageNames[i]));

    // -----------------------------------------------------------------------
    // FIXED HEADER combo (LANGUAGE) -- not scrolled; sits in the header strip.
    // Balanced bar: kBarW x kBarH (ctor textScaleX/textScaleY). Its dropdown
    // opens DOWNWARD from just below the bar; since the combo never scrolls the
    // dropdown always lands correctly. Positioned in the right widget column so
    // it aligns with the scrollable rows below.
    // -----------------------------------------------------------------------
    uint16_t langDefault = (uint16_t)(game_work.languageFlag < kLanguageCount ? game_work.languageFlag : 0);

    // Labels are UPPERCASE -- font_fruit_ninja.fnt has no lowercase glyphs.
    ComboBox* langCombo = new ComboBox(Vec3(kWidgetX, 128.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                                       langItems, langDefault, "LANGUAGE",
                                       /*textFlag/visibleRows*/ 4, /*width (label font size)*/ 20,
                                       /*textScaleX (bar width)*/ (uint16_t)kBarW,
                                       /*textScaleY (bar height)*/ (uint16_t)kBarH);
    langCombo->SetTextColour(Colour(255, 255, 255, 255));

    // -----------------------------------------------------------------------
    // SCROLLABLE rows (checkboxes + sliders), single column, even pitch. Their
    // content-space Y descends by kRowPitch from kRowFirst. Six rows at pitch 58
    // span ~290 units of content, so with a ~223-unit viewport the list scrolls.
    // -----------------------------------------------------------------------
    // Empty widget labels -- the harness draws each row's label in the fixed LEFT
    // column (so every row reads "LABEL ..... [widget]"). CheckBox/SliderControl
    // still DrawString their own m_Label, but an empty string emits zero glyphs,
    // so passing "" suppresses the widget's built-in (right-side) label without
    // touching the faithful Draw path.
    CheckBox* soundCb  = new CheckBox(Vec3(kWidgetX, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "");
    CheckBox* musicCb  = new CheckBox(Vec3(kWidgetX, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "");
    CheckBox* motionCb = new CheckBox(Vec3(kWidgetX, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "");
    soundCb->SetCheckedForTest(game_work.m_bSoundOn);
    musicCb->SetCheckedForTest(game_work.m_bMusicOn);
    motionCb->SetCheckedForTest(FN::g_MotionMode);

    int flick0 = ClampInt((int)FN::g_MotionSpeedThreshold, 0, 30);
    int sfx0   = ClampInt((int)(Mortar::SoundManager::s_SFXVolume   * 100.0f + 0.5f), 0, 100);
    int mus0   = ClampInt((int)(Mortar::SoundManager::s_MusicVolume * 100.0f + 0.5f), 0, 100);

    SliderControl* flickSl = new SliderControl(Vec3(kWidgetX, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                                               "", 0, 30, 24, flick0);
    SliderControl* sfxSl   = new SliderControl(Vec3(kWidgetX, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                                               "", 0, 100, 24, sfx0);
    SliderControl* musSl   = new SliderControl(Vec3(kWidgetX, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                                               "", 0, 100, 24, mus0);

    // -----------------------------------------------------------------------
    // Bindings -- install the real widget callbacks and prime the combo poll.
    // -----------------------------------------------------------------------
    SettingsHarness bind;
    bind.soundCb   = soundCb;
    bind.musicCb   = musicCb;
    bind.motionCb  = motionCb;
    bind.flickSl   = flickSl;
    bind.sfxSl     = sfxSl;
    bind.musSl     = musSl;
    bind.langCombo = langCombo;
    bind.langItems = &langItems;
    bind.lastLangFlag = (int)langDefault;
    bind.dataDir   = h.game.data_dir;

    soundCb->SetOnToggleForTest (Mortar::Delegate0<void>::Make(&bind, &SettingsHarness::OnSoundToggle));
    musicCb->SetOnToggleForTest (Mortar::Delegate0<void>::Make(&bind, &SettingsHarness::OnMusicToggle));
    motionCb->SetOnToggleForTest(Mortar::Delegate0<void>::Make(&bind, &SettingsHarness::OnMotionToggle));
    flickSl->SetOnValueChangedForTest(Mortar::Delegate0<void>::Make(&bind, &SettingsHarness::OnFlickChanged));
    sfxSl->SetOnValueChangedForTest  (Mortar::Delegate0<void>::Make(&bind, &SettingsHarness::OnSfxChanged));
    musSl->SetOnValueChangedForTest  (Mortar::Delegate0<void>::Make(&bind, &SettingsHarness::OnMusicVolChanged));

    // Add to the HUD so HUD::Update drives each widget's Update() (hit-testing)
    // and HUD::Draw renders them. The ComboBox creates its ListBox (and the
    // ListBox its VerticalScroller) on tap and AddControl's them here too.
    // Ownership stays with the HUD; h.Shutdown() releases them.
    game_work.mHud->AddControl(langCombo, false);
    game_work.mHud->AddControl(soundCb,   false);
    game_work.mHud->AddControl(musicCb,   false);
    game_work.mHud->AddControl(motionCb,  false);
    game_work.mHud->AddControl(flickSl,   false);
    game_work.mHud->AddControl(sfxSl,     false);
    game_work.mHud->AddControl(musSl,     false);

    // -----------------------------------------------------------------------
    // Scrollable-row table: content-space Y in row order (top to bottom).
    // -----------------------------------------------------------------------
    std::vector<ScrollRow> rows;
    {
        const char* labels[6] = { "SOUND", "MUSIC", "MOTION MODE", "FLICK", "SFX VOL", "MUS VOL" };
        HUDControl* widgets[6] = { soundCb, musicCb, motionCb, flickSl, sfxSl, musSl };
        for (int i = 0; i < 6; ++i) {
            ScrollRow r;
            r.widget   = widgets[i];
            r.label    = labels[i];
            r.contentY = kRowFirst - (float)i * kRowPitch;
            rows.push_back(r);
        }
    }

    // Content extent = distance from the first row centre to the last row centre
    // plus a half-pitch of padding at each end. Scroll range clamps to
    // [0, contentHeight - viewportHeight].
    const float viewportHeight = kViewTop - kViewBottom;
    const float contentHeight  = (float)rows.size() * kRowPitch;
    float scrollMax = contentHeight - viewportHeight;
    if (scrollMax < 0.0f) scrollMax = 0.0f;

    float scrollOffset = 0.0f;

    // Drag-scroll state: -1 = not dragging; else the touch slot we're scrolling with.
    bool  dragScrolling  = false;
    float dragLastY      = 0.0f;   // last touch Y (game space) while drag-scrolling
    bool  pressDecided   = false;  // have we classified the current press yet?

    std::printf("[settings_interactive] ready -- %s. Toggle/drag widgets; wheel/drag scrolls; ESC quits, M=motion.\n",
                h.IsInteractive() ? "INTERACTIVE (window shown)" : "headless smoke");

    // -----------------------------------------------------------------------
    // Frame loop.
    // -----------------------------------------------------------------------
    const float kDt  = 1.0f / 60.0f;
    const int   mask = 0x7FFFFFFF;
    const int maxFrames = h.IsInteractive() ? -1 : (h.HasFramesOverride() ? h.frames : 30);

    bool running = true;
    int  frame   = 0;
    while (running) {
        if (maxFrames >= 0 && frame >= maxFrames) break;

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { running = false; break; }
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) { running = false; break; }
                if (ev.key.keysym.sym == SDLK_m) {
                    // Safety net: toggle motion mode from the keyboard so the
                    // pointer can never get "stuck" in a routing that locks out
                    // the other widgets.
                    FN::g_MotionMode = !FN::g_MotionMode;
                    motionCb->SetCheckedForTest(FN::g_MotionMode);
                    std::printf("[settings_interactive] Motion mode -> %s (keyboard)\n",
                                FN::g_MotionMode ? "ON" : "OFF");
                }
            }
            if (ev.type == SDL_MOUSEWHEEL) {
                // Wheel scrolls the list. SDL wheel y > 0 = scroll up (content
                // moves down -> reveal earlier rows -> decrease scrollOffset).
                float dy = (float)ev.wheel.y;
                if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
                scrollOffset = ClampF(scrollOffset - dy * kRowPitch * 0.5f, 0.0f, scrollMax);
            }
            translator.DrainSDLEvent(ev, h.window);
        }
        if (!running) break;

        // Reuse the game's per-tick ring drain (Touch::Update(0) -> states1).
        translator.DispatchForSimTick();

        // ---- classify the press: widget interaction vs list drag-scroll ----
        // Find the first active touch slot + its live position.
        Mortar::Touch& touch = Mortar::Touch::GetInstance();
        int   activeSlot = -1;
        float touchX = 0.0f, touchY = 0.0f;
        for (int s = 0; s < Mortar::Touch::MAX_SLOTS; ++s) {
            const Mortar::TouchState& st = touch.states1[s];
            if (st.phase < 1) {
                activeSlot = s;
                touchX = st.currX;
                touchY = st.currY;
                break;
            }
        }

        if (activeSlot == -1) {
            // Nothing held -- reset the per-press classification.
            pressDecided  = false;
            dragScrolling = false;
        } else {
            bool inViewport = (touchY <= kViewTop && touchY >= kViewBottom);
            if (!pressDecided) {
                pressDecided = true;
                // A press that lands inside the viewport but NOT on a widget
                // hit-rect starts a drag-scroll. A press on a widget (or outside
                // the viewport, e.g. on the combo header) drives that control.
                if (inViewport && !DropdownOpen() && !PointInAnyWidget(rows, touchX, touchY)) {
                    dragScrolling = true;
                    dragLastY     = touchY;
                } else {
                    dragScrolling = false;
                }
            } else if (dragScrolling) {
                // Continue the drag: moving the finger UP (touchY decreasing)
                // scrolls the content up -> reveal later rows -> increase offset.
                float delta = dragLastY - touchY;   // +Y up: finger-up => delta>0
                scrollOffset = ClampF(scrollOffset + delta, 0.0f, scrollMax);
                dragLastY = touchY;
            }
        }

        // While drag-scrolling we must NOT feed the pointer into the widget input
        // path, or the drag would also drive whatever widget it passes over.
        if (!dragScrolling) {
            SyncFingerSpawnPos();
            SyncWorldPos();
        }

        // ---- apply the scroll offset to every scrollable row's pos ----
        // pos.y = contentY + scrollOffset: a positive offset shifts content UP
        // (reveals later rows). Both the draw and the hit-test read pos, so they
        // move together.
        for (size_t i = 0; i < rows.size(); ++i) {
            rows[i].widget->pos.y = rows[i].contentY + scrollOffset;
        }

        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(h.window, &ww, &wh);
        glViewport(0, 0, ww, wh);

        Mortar::DisplayManager::GetInstance().BeginFrame();
        MatrixManager::GetInstance().SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

        // Widget Update() hit-tests + fires the checkbox/slider delegates. Runs
        // with the freshly-scrolled pos so both draw and hit-test agree.
        game_work.mHud->Update(kDt);

        // ------------------------------------------------------------------
        // Draw the SCROLLABLE rows inside a scissor-clipped viewport so rows
        // scrolled outside it don't overdraw the header/readout.
        //
        // glScissor is in window pixels, origin bottom-left. Convert the
        // game-space viewport rect: game Y=+160 is window top (y_px = wh),
        // game Y=-160 is window bottom (y_px = 0). So:
        //   y_px(gameY) = (gameY + 160) * (wh / 320)   [wh = drawable height]
        // We use the actual drawable size for robustness on HiDPI.
        // ------------------------------------------------------------------
        float sx = (float)ww / 480.0f;   // window px per game unit, X
        float sy = (float)wh / 320.0f;   // window px per game unit, Y
        int scX = 0;
        int scY = (int)((kViewBottom + 160.0f) * sy + 0.5f);
        int scW = ww;
        int scH = (int)((kViewTop - kViewBottom) * sy + 0.5f);
        (void)sx;

        // HUD::Draw is a single monolithic pass over every control, so we cannot
        // easily draw only the scrollable rows scissored. Two-pass strategy:
        //   Pass 1: scissor to the viewport band, draw the WHOLE HUD. The combo
        //           BAR lives in the header ABOVE the band (y=128 > kViewTop=95),
        //           so it is clipped out here; the scrollable rows draw clipped to
        //           the band; the open dropdown (which opens downward from ~95
        //           into the band) draws inside the band.
        //   Pass 2: with scissor OFF, re-draw just the combo bar so the fixed
        //           header widget is always fully visible/unclipped.
        // The combo is drawn visibly in exactly one pass (pass 1 clips it, pass 2
        // shows it), so there is no double-draw tint artefact on the bar.
        glEnable(GL_SCISSOR_TEST);
        glScissor(scX, scY, scW, scH);

        game_work.mHud->BeginDraw(kDt);
        game_work.mHud->Draw(mask);

        glDisable(GL_SCISSOR_TEST);

        // Pass 2: re-draw the combo (header) unscissored so it is never clipped by
        // the row viewport.
        {
            float hudScale[3] = { 1.0f, 1.0f, 1.0f };
            langCombo->PreDraw(hudScale);
            langCombo->Draw(hudScale);
        }

        // Combo has no per-select delegate on the collapsed bar -- poll it.
        bind.PollCombo();

        // Harness-drawn LEFT-column row labels, each at its row's current
        // (scrolled) Y, clipped to the viewport band so they scroll with the row.
        glEnable(GL_SCISSOR_TEST);
        glScissor(scX, scY, scW, scH);
        for (size_t i = 0; i < rows.size(); ++i) {
            float ry = rows[i].contentY + scrollOffset;
            if (ry > kViewTop + kRowPitch || ry < kViewBottom - kRowPitch) continue;
            DrawReadoutLine(rows[i].label, kLabelX, ry - 8.0f, 18.0f);
        }
        glDisable(GL_SCISSOR_TEST);

        // On-screen readout of the live parameter values, in the clear bottom
        // strip below the viewport (y < kViewBottom). Nothing else draws there.
        char l1[128];
        char l2[192];
        std::snprintf(l1, sizeof(l1), "SOUND:%s  MUSIC:%s  MOTION:%s",
                      game_work.m_bSoundOn ? "ON" : "OFF",
                      game_work.m_bMusicOn ? "ON" : "OFF",
                      FN::g_MotionMode ? "ON" : "OFF");
        std::snprintf(l2, sizeof(l2), "FLICK:%d  SFX:%d%%  MUS:%d%%  LANG:%s",
                      (int)FN::g_MotionSpeedThreshold,
                      ClampInt((int)(Mortar::SoundManager::s_SFXVolume   * 100.0f + 0.5f), 0, 100),
                      ClampInt((int)(Mortar::SoundManager::s_MusicVolume * 100.0f + 0.5f), 0, 100),
                      kLanguageNames[bind.lastLangFlag < kLanguageCount ? bind.lastLangFlag : 0]);
        DrawReadoutLine(l1, -235.0f, -142.0f, 16.0f);
        DrawReadoutLine(l2, -235.0f, -157.0f, 16.0f);

        SDL_GL_SwapWindow(h.window);
        if (h.IsInteractive()) SDL_Delay(16);
        ++frame;
    }

    std::printf("[settings_interactive] exit after %d frames\n", frame);

    // Release the placeholder textures (static slots + local refs) while the GL
    // context is still alive -- glDeleteTextures runs in the Texture2D_Bada dtor.
    // The widgets themselves are owned by game_work.mHud and freed by h.Shutdown();
    // their dtors touch no GL.
    CheckBox::UnloadContent();
    SliderControl::UnloadContent();
    ComboBox::UnloadContent();
    ListBox::UnloadContent();
    VerticalScroller::UnloadContent();
    texCheckboxOn.SetNull();
    texCheckboxOff.SetNull();
    texTrack.SetNull();
    texThumb.SetNull();
    texBar.SetNull();
    texArrow.SetNull();
    texRow.SetNull();
    texScrTrack.SetNull();
    texScrThumb.SetNull();
    texScrArrow.SetNull();

    return h.Shutdown();
}
