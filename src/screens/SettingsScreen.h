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
// false); s->Init();` -- AddControl the SCREEN ITSELF before calling Init():
// Init() AddControl's the 5 owned widgets (4 settings controls + the
// m_pCloseButton BSButton), and since HUD::Draw has no per-control sort key
// (only control-list order), the screen must already be in the list first so
// its own Draw() (backdrop + plate + labels) paints behind every widget.
// SettingsScreen::Toggle() follows this exact order; a caller bypassing
// Toggle() (e.g. a render test) must too.
//
// The screen does NOT drive the widgets' Update/Draw directly -- each is
// AddControl'd to game_work.mHud with m_LayerFlags = HUD_LAYER_TOP_MOST
// (matching the modal control itself) so HUD::Update's modal input-capture
// gate (see HUD.h SetInputModal) still delivers input to them while the
// settings modal owns SetInputModal. Draw order (the plate behind the
// widgets, the open dropdown panel above its siblings) is controlled purely
// by HUD control-list order: screen first, then checkboxes/slider/close
// button, then the dropdown last (see Init()).
//
// Lifecycle: call the static SettingsScreen::Toggle() to open/close the modal
// -- it owns the single file-static instance pointer, the AddControl/Init on
// open, and the SetInputModal(NULL)+SetPendingRemoval on close. Both the ESC
// key (src/GameSDL.cpp), the MainScreen settings button (src/screens/
// MainScreen.cpp), and m_pCloseButton's tap (CloseCallback) call Toggle() so
// there is exactly one open/close path.
// Release() (called by the HUD removal sweep, and by the dtor) tears down the
// 5 owned widgets (SetPendingRemoval + null -- HUD's own Update sweep deletes
// each, matching GameModeScreen::RemoveButtons' pattern for AddControl'd
// buttons) and the shared injected textures/font.
//

#include "hud/HUDControl3d.h"
#include "math/Vec3.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
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

    int GetType() override { return 1; }

    // Port specific: single open/close path for the modal, shared by the ESC
    // key (src/GameSDL.cpp) and the MainScreen settings button
    // (src/screens/MainScreen.cpp). Owns the file-static instance pointer;
    // Toggle() opens (new + Init + AddControl + SetInputModal(this)) when
    // closed, or closes (SetInputModal(NULL) + SetPendingRemoval) when open.
    static void Toggle();
    static bool IsOpen();

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
    Mortar::SmartPtr<Mortar::Texture> m_TexFade;   // list_fade.tex -- UiDropdown open-list edge fade

    std::vector<std::string> m_LangItems;

public:
    // Delegate targets (Mortar::Delegate0<void> callees), installed on each
    // widget's SetOnChange in Init().
    void OnMotionToggle();
    void OnFpsToggle();
    void OnSensChanged();
    void OnLangChanged();

    // Port specific: m_pCloseButton's click callback. Runs the same
    // open/close path as Toggle() (SetInputModal(NULL) + SetPendingRemoval)
    // so tapping Close behaves exactly like the ESC key / settings gear.
    void CloseCallback();

    // Test-only: expose the dropdown so render tests can force it open
    // (UiDropdown::SetOpenForTest) without a synthetic touch sequence.
    UiDropdown* GetLangDropForTest() const { return m_LangDrop; }
};

#endif // FN_SETTINGS_SCREEN_H
