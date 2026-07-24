#ifndef FN_GAME_SETTINGSSAVE_H
#define FN_GAME_SETTINGSSAVE_H

// Port specific: settings persistence. Binary (v1.6.1) never saved settings
// to disk -- this whole module has no binary counterpart.
//
// Persists 6 port-side globals to a small flat XML file:
//   game_work.languageFlag, FN::g_MotionMode, FN::g_ShowFps,
//   FN::g_FpsCap60, FN::g_MotionSpeedThreshold, Layout::IsWideLayout()/
//   Layout::SetWideLayout() (opt-in widescreen layout, see
//   src/engine/render/Layout.h).
//
// The web audio-consent overlay's sound/muted choice is intentionally NOT
// persisted here (or anywhere) -- see mainEmscripten.cpp's g_gameInited
// comment block: whether the overlay shows is decided fresh every load from
// the AudioContext's born state, and the tapped choice only ever applies to
// game_work for that session.
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
// Save path: <save_dir>/SettingsSave.xml on every platform (per-platform
// save_dir -- see src/platform/SaveDirSDL.h) -- mirrors FruitSaveData's
// GetSavePath().
void SaveSettings();
void LoadSettings();

#endif
