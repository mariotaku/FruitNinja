#ifndef FN_SETTINGS_SCREEN_H
#define FN_SETTINGS_SCREEN_H

//
// SettingsScreen : HUDControl3d
//
// Port specific: in-game settings modal. NO binary counterpart -- v1.6.1 ships
// no settings/options screen (OptionsScreen's widget stack -- CheckBox /
// SliderControl / ComboBox / ListBox / VerticalScroller -- is dead code with
// no live call site; see each widget's header). This screen is a
// port-improvement that reuses those faithfully-ported-but-orphaned widgets to
// give the port an actual settings UI, wiring them to host-only globals
// (FN::g_MotionMode / FN::g_MotionSpeedThreshold / FN::g_ShowFps) and
// game_work.languageFlag. Not fidelity-constrained; no // ASM-verified markers
// apply to this class itself (only to the widgets it hosts).
//
// Usage: `new SettingsScreen(); Init(); game_work.mHud->AddControl(screen, false);`
// The screen owns its 4 widgets directly (they are NOT added to mHud) -- it
// drives their Update/Draw itself every frame from its own Update/Draw. Its
// ComboBox, however, DOES AddControl its spawned ListBox to mHud (that's
// ComboBox's own faithful behaviour, TOP_MOST-ish HUD_LAYER_POST_ACTOR so the
// dropdown draws over the modal panel) -- SettingsScreen does not manage that.
//
// Lifecycle: caller toggles visibility by adding/removing the screen from the
// HUD (see the ESC handler in src/GameSDL.cpp). Release() (called by the HUD
// removal sweep, and by the dtor) tears down the 4 owned widgets and the
// static injected placeholder textures.
//

#include "hud/HUDControl3d.h"
#include "math/Vec3.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include <string>
#include <vector>

class CheckBox;
class SliderControl;
class ComboBox;
namespace Mortar { class Font; }

class SettingsScreen : public HUDControl3d {
public:
    SettingsScreen();
    virtual ~SettingsScreen();

    void Init()    override;
    void Release() override;
    void Update(float dt) override;
    void Draw(float* hudScaleRaw) override;

    int GetType() override { return 1; }

private:
    ComboBox*      m_LangCombo;
    CheckBox*      m_MotionCb;
    SliderControl* m_SensSlider;
    CheckBox*      m_FpsCb;

    Mortar::SmartPtr<Mortar::Texture> m_Plate;      // dialog_box.tex
    Mortar::SmartPtr<Mortar::Texture> m_Backdrop;   // solid black, modal dim overlay (placeholder art)

    // TTF font for the language ComboBox (native language names need
    // CJK/Hangul/Cyrillic glyphs the bitmap font_fruit_ninja.fnt lacks).
    // fontstruetype/gangofchinese.ttf. Held here so it outlives the combo
    // (and the ListBox it hands the pointer to); released in Release().
    Mortar::SmartPtr<Mortar::Font> m_LangFont;

    // Kept-alive placeholder textures injected into the widgets' static slots
    // (see WidgetPlaceholderArt.h). Held here so they outlive every widget
    // that references them and are released in Release().
    Mortar::SmartPtr<Mortar::Texture> m_TexCheckboxOn;
    Mortar::SmartPtr<Mortar::Texture> m_TexCheckboxOff;
    Mortar::SmartPtr<Mortar::Texture> m_TexTrack;
    Mortar::SmartPtr<Mortar::Texture> m_TexThumb;
    Mortar::SmartPtr<Mortar::Texture> m_TexBar;
    Mortar::SmartPtr<Mortar::Texture> m_TexArrow;
    Mortar::SmartPtr<Mortar::Texture> m_TexRow;
    Mortar::SmartPtr<Mortar::Texture> m_TexScrTrack;
    Mortar::SmartPtr<Mortar::Texture> m_TexScrThumb;
    Mortar::SmartPtr<Mortar::Texture> m_TexScrArrow;

    std::vector<std::string> m_LangItems;
    int m_LangLast;   // last-polled ComboBox selection index

    void PollCombo();

public:
    // Delegate targets (Mortar::Delegate0<void> callees) -- see
    // CheckBox::SetOnToggleForTest / SliderControl::SetOnValueChangedForTest.
    void OnMotionToggle();
    void OnFpsToggle();
    void OnSensChanged();
};

#endif // FN_SETTINGS_SCREEN_H
