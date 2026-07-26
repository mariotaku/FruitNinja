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
// The sfx/music on/off preference is NOT part of this file -- it is
// genuinely persisted separately, via FruitSaveData's "soundOff"/"musicOff"
// totals (GameInitialise.cpp derives game_work.m_bSoundOn/m_bMusicOn from
// them at boot, matching v1.6.1 InitialiseData @0x0011c3f0). The web
// audio-consent overlay only gates the browser's separate AudioContext
// UNLOCK gesture on top of that persisted preference -- see
// mainEmscripten.cpp's g_gameInited comment block for the show/skip/
// unlock-only/first-run decision. Nothing about THAT unlock decision is
// persisted (it is re-derived every load from the AudioContext's born state
// plus the already-loaded preference).
//
// Call LoadSettings() once at startup, before any CLI/URL override write to
// those globals (so an explicit --lang / ?lang= etc. override still wins by
// running after and overwriting again) and before GameInitialise's
// Localisation::Load step (see mainSDL.cpp / mainEmscripten.cpp).
//
// Call SaveSettings() whenever the settings UI closes, so changes are
// durable across relaunch (needed because a language change requires a
// full quit/relaunch to take effect -- see SettingsScreen::Toggle()).
// It is ALSO called from GameTaskSaveOnExit (GameTaskState.cpp) so
// live-applied settings survive quit/backgrounding while the popup is
// still open or animating closed -- that hook covers desktop window
// close, focus-loss/minimize, and web tab-hide. Idempotent and cheap
// (always rewrites the whole file), so double-saving is harmless. Do
// NOT call it from per-frame paths (e.g. a slider drag handler).
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
