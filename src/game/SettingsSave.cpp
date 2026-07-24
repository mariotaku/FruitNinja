// SettingsSave -- port-specific settings persistence (no binary counterpart).
// See header for contract. Mirrors FruitSaveData.cpp's GetSavePath /
// IDBFS-flush / tinyxml2-wrapper patterns for consistency with the game's
// other save file.

#include "SettingsSave.h"
#include "GameWork.h"
#include "debug/DebugFlags.h"
#include "engine/xml/TiXml.h"
#include "Game.h"
#include "debug/Logger.h"
#include "render/Layout.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <string>
#include <cstring>

namespace {

// Resolve the on-disk settings-save path: <save_dir>/SettingsSave.xml on
// every platform -- save_dir is resolved per-platform in exactly one place
// per backend (Mortar_ResolveSaveDir for host/webOS/Emscripten --
// src/platform/SaveDirSDL.h; FN_SAVE_DIR on Wii -- GameWii.cpp), so this
// function carries no platform branches. Mirrors FruitSaveData.cpp's
// GetSavePath() (not shared -- that helper is anonymous-namespace/local
// to that translation unit too).
std::string GetSettingsSavePath() {
    return Game::GetInstance()->save_dir + "/" + "SettingsSave.xml";
}

} // namespace

void SaveSettings() {
    TiXmlDocument doc;
    TiXmlElement root = doc.NewElement("settings");

    root.SetAttribute("languageFlag", (int)game_work.languageFlag);
    root.SetAttribute("motionMode", FN::g_MotionMode ? "true" : "false");
    root.SetAttribute("showFps", FN::g_ShowFps ? "true" : "false");
    root.SetAttribute("fpsCap60", FN::g_FpsCap60 ? "true" : "false");
    // Saves the PREF (user's saved choice), not the live active value -- a
    // widescreen toggle needs an app restart to apply (see Layout.h); the
    // active value only re-syncs to the pref at the next LoadSettings/boot.
    root.SetAttribute("widescreen", Layout::IsWideLayoutPref() ? "true" : "false");
    // Port specific: Wii-only in the UI (see SettingsScreen.h m_LetterboxCb),
    // but saved/loaded unconditionally like every other attribute here --
    // applies LIVE (no restart, unlike widescreen), so this saves the ACTUAL
    // current value, not a separate pref. Harmless on host/web: there is no
    // UI path to change it there, so it always round-trips the default true.
    root.SetAttribute("letterbox", Layout::IsLetterbox() ? "true" : "false");
    root.SetAttribute("motionSpeedThreshold", FN::g_MotionSpeedThreshold);

    doc.InsertEndChild(root);
    if (!doc.SaveFile(GetSettingsSavePath().c_str())) {
        LOG_ERROR("SettingsSave", "failed to save '%s'", GetSettingsSavePath().c_str());
    }
#if defined(__EMSCRIPTEN__)
    // Port specific: flush the IDBFS /save mount to IndexedDB after each
    // write so data survives page reload/close.
    EM_ASM({ FS.syncfs(false, function(err) {}); });
#endif
}

void LoadSettings() {
    TiXmlDocument doc;
    if (!doc.LoadFile(GetSettingsSavePath().c_str())) {
        return;  // expected on first run: no-op, leave globals untouched
    }

    TiXmlElement root = doc.FirstChildElement("settings");
    if (!root) return;

    int languageFlag = 0;
    if (root.QueryIntAttribute("languageFlag", &languageFlag) == TIXML_SUCCESS) {
        // Clamp: kLanguageNames (SettingsScreen.cpp) has 21 entries (0..20).
        // A save file written before the langId-21 "Debug" entry was removed
        // from the picker (or any other garbage value) must not index OOB.
        if (languageFlag < 0) languageFlag = 0;
        if (languageFlag > 20) languageFlag = 20;
        game_work.languageFlag = (uint8_t)languageFlag;
    }

    const char* motionMode = root.Attribute("motionMode");
    if (motionMode) {
        FN::g_MotionMode = (strcmp(motionMode, "true") == 0);
    }

    const char* showFps = root.Attribute("showFps");
    if (showFps) {
        FN::g_ShowFps = (strcmp(showFps, "true") == 0);
    }

    const char* fpsCap60 = root.Attribute("fpsCap60");
    if (fpsCap60) {
        FN::g_FpsCap60 = (strcmp(fpsCap60, "true") == 0);
    }
#if defined(FRUIT_PLATFORM_WII)
    // Port specific: Wii has no capped-fps concept -- native/display refresh
    // is always on, regardless of a stale/imported SettingsSave.xml carrying
    // fpsCap60="true" from another platform. The SettingsScreen "NATIVE FRAME
    // RATE" checkbox is hidden on this platform (see SettingsScreen.h
    // m_NativeFpsCb), so there is no UI path back to true either -- this just
    // guards against a foreign save file. The XML attribute/global itself
    // stay intact (SaveSettings() still writes "false" here on Wii) so the
    // persistence shape matches every other platform.
    FN::g_FpsCap60 = false;
#endif

    const char* ws = root.Attribute("widescreen");
    if (ws) {
        Layout::SetWideLayout(strcmp(ws, "true") == 0);
    }

    const char* letterbox = root.Attribute("letterbox");
    if (letterbox) {
        Layout::SetLetterbox(strcmp(letterbox, "true") == 0);
    } else {
#if defined(FRUIT_PLATFORM_WII)
        // Port specific: no saved preference (first run, or a save file
        // predating this attribute) -- Wii defaults to OFF (stretch-to-fill,
        // today's look) so a user who never opens Settings sees no bars.
        // A previously-saved letterbox="true" is handled by the branch
        // above and is never reached here, so it's never clobbered.
        Layout::SetLetterbox(false);
#else
        // Host/web: no saved preference -- keep Layout::g_Letterbox's
        // compile-time default (true) so behaviour is unchanged from
        // before this platform default existed.
        Layout::SetLetterbox(true);
#endif
    }

    float motionSpeedThreshold = 0.0f;
    if (root.QueryFloatAttribute("motionSpeedThreshold", &motionSpeedThreshold) == TIXML_SUCCESS) {
        FN::g_MotionSpeedThreshold = motionSpeedThreshold;
    }
}
