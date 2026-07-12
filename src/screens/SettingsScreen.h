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
// The screen owns its 5 widgets directly (4 settings controls + the
// m_pCloseButton BSButton; none are added to mHud) -- it drives their
// Update/Draw itself every frame from its own Update/Draw. Its ComboBox,
// however, DOES AddControl its spawned ListBox to mHud (that's ComboBox's own
// faithful behaviour, TOP_MOST-ish HUD_LAYER_POST_ACTOR so the dropdown draws
// over the modal panel) -- SettingsScreen does not manage that.
//
// Lifecycle: call the static SettingsScreen::Toggle() to open/close the modal
// -- it owns the single file-static instance pointer, the AddControl/Init on
// open, and the SetInputModal(NULL)+SetPendingRemoval on close. Both the ESC
// key (src/GameSDL.cpp), the MainScreen settings button (src/screens/
// MainScreen.cpp), and m_pCloseButton's tap (CloseCallback) call Toggle() so
// there is exactly one open/close path.
// Release() (called by the HUD removal sweep, and by the dtor) tears down the
// 5 owned widgets and the static injected placeholder textures.
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

    int GetType() override { return 1; }

    // Port specific: single open/close path for the modal, shared by the ESC
    // key (src/GameSDL.cpp) and the MainScreen settings button
    // (src/screens/MainScreen.cpp). Owns the file-static instance pointer;
    // Toggle() opens (new + Init + AddControl + SetInputModal(this)) when
    // closed, or closes (SetInputModal(NULL) + SetPendingRemoval) when open.
    static void Toggle();
    static bool IsOpen();

private:
    ComboBox*      m_LangCombo;
    CheckBox*      m_MotionCb;
    SliderControl* m_SensSlider;
    CheckBox*      m_FpsCb;

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

    // TTF font for the language ComboBox (native language names need
    // CJK/Hangul/Cyrillic glyphs the bitmap font_fruit_ninja.fnt lacks).
    // fontstruetype/gangofchinese.ttf. Held here so it outlives the combo
    // (and the ListBox it hands the pointer to); released in Release().
    Mortar::SmartPtr<Mortar::Font> m_LangFont;

    // Kept-alive widget textures injected into the widgets' static slots -- real
    // staged art (assets/ui-widgets/*.svg via fn_asset_staging) for most slots,
    // with a couple of procedural-only fills (WidgetPlaceholderArt.h) for
    // elements with no real .tex counterpart (m_TexRow, m_Backdrop). Held here
    // so they outlive every widget that references them and are released in
    // Release().
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

    // Port specific: m_pCloseButton's click callback. Runs the same
    // open/close path as Toggle() (SetInputModal(NULL) + SetPendingRemoval)
    // so tapping Close behaves exactly like the ESC key / settings gear.
    void CloseCallback();
};

#endif // FN_SETTINGS_SCREEN_H
