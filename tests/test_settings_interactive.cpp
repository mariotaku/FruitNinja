// test_settings_interactive.cpp -- INTERACTIVE dev harness for the ported
// settings widgets (CheckBox / SliderControl / ComboBox+ListBox+VerticalScroller).
//
// Unlike the screenshot render tests (test_settings_widgets_render /
// test_dropdown_render), this opens a REAL clickable window and drives the
// widgets from live mouse/touch input, each widget bound to an actual app
// parameter. Toggle a switch / drag a slider / pick a language and watch the
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
// Coordinate space (docs/engine/coordinate-system.md): draw-space and touch-space
// share the same axes -- X horizontal [-240,240], Y vertical [-160,160], +Y up --
// so a click lands where the widget is drawn (no conversion needed).
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
    // -----------------------------------------------------------------------
    // Track/thumb sized so the slider reads at comparable visual weight to the
    // CheckBox's HARDCODED 128x64 switch quad (SliderControl::Draw, can't change --
    // see CheckBox::Draw). SliderControl's ctor sizes m_TrackWidth/Height directly
    // from these texture pixel dims * size (size.x/y == 1 for every slider below),
    // so texture px == on-screen px here.
    Mortar::SmartPtr<Mortar::Texture> texSwitchOn  = MakeSwitchTex(true,  128, 64);
    Mortar::SmartPtr<Mortar::Texture> texSwitchOff = MakeSwitchTex(false, 128, 64);
    Mortar::SmartPtr<Mortar::Texture> texTrack     = MakeSolidTex(120, 120, 120, 255, 180, 40);
    Mortar::SmartPtr<Mortar::Texture> texThumb     = MakeCircleTex(240, 140, 20, 46, 46);
    Mortar::SmartPtr<Mortar::Texture> texBar       = MakeSolidTex(40, 40, 60, 255, 8, 8);
    // Expand-arrow quad width = arrowTex.GetWidth() * combo size.x (ComboBox::Draw);
    // size.x == 1 for langCombo below, so texture px == on-screen px. 16px reads as
    // a small glyph next to the 140-wide bar (was 32px -- a screen-filling triangle).
    Mortar::SmartPtr<Mortar::Texture> texArrow     = MakeArrowTex(255, 210, 40, 16, 16);
    Mortar::SmartPtr<Mortar::Texture> texRow       = MakeSolidTex(255, 255, 255, 255, 8, 8);
    Mortar::SmartPtr<Mortar::Texture> texScrTrack  = MakeSolidTex(70, 70, 90, 255, 8, 8);
    Mortar::SmartPtr<Mortar::Texture> texScrThumb  = MakeSolidTex(200, 200, 210, 255, 8, 8);
    Mortar::SmartPtr<Mortar::Texture> texScrArrow  = MakeArrowTex(180, 180, 200, 24, 24);

    CheckBox::SetTexturesForTest(texSwitchOn, texSwitchOff);
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
    // Widgets, two columns, top to bottom (screen Y in [-160,160], +Y up):
    //
    //   Combo (LANGUAGE)               centered, y=140  (near the top edge --
    //                                   its dropdown opens DOWNWARD from
    //                                   y=~111 for 4*16=64 units, clearing
    //                                   y=~47, well above row 1 below)
    //   Row 1  SOUND (cb)  / FLICK (sl)     y=25
    //   Row 2  MUSIC (cb)  / SFX VOL (sl)   y=-40
    //   Row 3  MOTION (cb) / MUS VOL (sl)   y=-105
    //
    // Row pitch 65 clears CheckBox's hardcoded 64-tall quad (hit-rect +/-28.5)
    // and the slider's Y hit half-extent (size.y*60*0.5 = 30) with margin.
    // Row 3's lowest edge (checkbox -133.5, slider -135) leaves a clear
    // y=[-160,-135] strip at the bottom for the readout text (see below).
    // Left column x=-100 (checkbox hit-rect pos.x+/-36 -> [-136,-64], label to
    // pos.x+64=-36); right column x=55 (track 180 wide -> label at
    // pos.x+15+trackW*0.5=145, comfortably inside the +240 edge). See
    // coordinate note in header.
    // -----------------------------------------------------------------------
    uint16_t langDefault = (uint16_t)(game_work.languageFlag < kLanguageCount ? game_work.languageFlag : 0);

    // Labels are UPPERCASE -- font_fruit_ninja.fnt has no lowercase glyphs
    // (see kLanguageNames comment above); lowercase letters silently draw
    // nothing.
    ComboBox* langCombo = new ComboBox(Vec3(0.0f, 140.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                                       langItems, langDefault, "LANGUAGE",
                                       /*textFlag/visibleRows*/ 4, /*width (label font size)*/ 20,
                                       /*textScaleX (bar width)*/ 140, /*textScaleY (bar height)*/ 28);
    langCombo->SetTextColour(Colour(255, 255, 255, 255));

    const float kColL = -100.0f;  // checkbox column
    const float kColR =   55.0f;  // slider column
    const float kRow1 =   25.0f;
    const float kRow2 =  -40.0f;
    const float kRow3 = -105.0f;

    CheckBox* soundCb  = new CheckBox(Vec3(kColL, kRow1, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "SOUND");
    CheckBox* musicCb  = new CheckBox(Vec3(kColL, kRow2, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "MUSIC");
    CheckBox* motionCb = new CheckBox(Vec3(kColL, kRow3, 0.0f), Vec3(1.0f, 1.0f, 1.0f), "MOTION MODE");
    soundCb->SetCheckedForTest(game_work.m_bSoundOn);
    musicCb->SetCheckedForTest(game_work.m_bMusicOn);
    motionCb->SetCheckedForTest(FN::g_MotionMode);

    int flick0 = ClampInt((int)FN::g_MotionSpeedThreshold, 0, 30);
    int sfx0   = ClampInt((int)(Mortar::SoundManager::s_SFXVolume   * 100.0f + 0.5f), 0, 100);
    int mus0   = ClampInt((int)(Mortar::SoundManager::s_MusicVolume * 100.0f + 0.5f), 0, 100);

    SliderControl* flickSl = new SliderControl(Vec3(kColR, kRow1, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                                               "FLICK", 0, 30, 24, flick0);
    SliderControl* sfxSl   = new SliderControl(Vec3(kColR, kRow2, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                                               "SFX VOL", 0, 100, 24, sfx0);
    SliderControl* musSl   = new SliderControl(Vec3(kColR, kRow3, 0.0f), Vec3(1.0f, 1.0f, 1.0f),
                                               "MUS VOL", 0, 100, 24, mus0);

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

    std::printf("[settings_interactive] ready -- %s. Toggle/drag widgets; ESC quits, M=motion.\n",
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
            translator.DrainSDLEvent(ev, h.window);
        }
        if (!running) break;

        // Reuse the game's per-tick ring drain (Touch::Update(0) -> states1),
        // then mirror the drained pointer position into m_FingerSpawnPos.
        translator.DispatchForSimTick();
        SyncFingerSpawnPos();
        SyncWorldPos();

        int ww = 0, wh = 0;
        SDL_GL_GetDrawableSize(h.window, &ww, &wh);
        glViewport(0, 0, ww, wh);

        Mortar::DisplayManager::GetInstance().BeginFrame();
        MatrixManager::GetInstance().SetupOrtho(160.0f, -160.0f, -240.0f, 240.0f, 2000.0f, -6000.0f);

        // Widget Update() hit-tests + fires the checkbox/slider delegates.
        game_work.mHud->Update(kDt);
        game_work.mHud->BeginDraw(kDt);
        game_work.mHud->Draw(mask);

        // Combo has no per-select delegate on the collapsed bar -- poll it.
        bind.PollCombo();

        // On-screen readout of the live parameter values, in the clear bottom
        // strip below row 3 (y in [-160,-135] -- see column-layout comment
        // above; nothing else draws there, so it never sits on top of a widget).
        char l1[128];
        char l2[192];
        // UPPERCASE labels -- see kLanguageNames comment (font_fruit_ninja.fnt
        // has no lowercase glyphs; lowercase letters silently drew nothing).
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
    texSwitchOn.SetNull();
    texSwitchOff.SetNull();
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
