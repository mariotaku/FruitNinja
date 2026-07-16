#ifndef FN_GAME_SETTINGSSAVE_H
#define FN_GAME_SETTINGSSAVE_H

// Port specific: settings persistence. Binary (v1.6.1) never saved settings
// to disk -- this whole module has no binary counterpart.
//
// Persists 9 port-side globals to a small flat XML file:
//   game_work.languageFlag, FN::g_MotionMode, FN::g_ShowFps,
//   FN::g_FpsCap60, FN::g_MotionSpeedThreshold, Layout::IsWideLayout()/
//   Layout::SetWideLayout() (opt-in widescreen layout, see
//   src/engine/render/Layout.h), and the web audio-consent choice
//   (FN::g_AudioChoiceMade / g_SavedSoundOn / g_SavedMusicOn, see
//   src/debug/DebugFlags.h) -- set once by the consent-overlay tap
//   (mainEmscripten.cpp) so a returning web visit skips the overlay.
//
// Call LoadSettings() once at startup, before any CLI/URL override write to
// those globals (so an explicit --lang / ?lang= etc. override still wins by
// running after and overwriting again) and before GameInitialise's
// Localisation::Load step (see mainSDL.cpp / mainEmscripten.cpp).
//
// Call SaveSettings() whenever the settings UI closes, so changes are
// durable across relaunch (needed because a language change requires a
// full quit/relaunch to take effect -- see SettingsScreen::Toggle()).
//
// Both are safe no-ops when the file is missing or partially unparsable:
// LoadSettings leaves any untouched global at its current value (never
// zeroes/resets), and SaveSettings always writes a fresh well-formed file.
//
// Save path: <data_dir>/SettingsSave.xml on desktop, /save/SettingsSave.xml
// (IDBFS-backed) on the web build -- mirrors FruitSaveData's GetSavePath().
void SaveSettings();
void LoadSettings();

#endif
