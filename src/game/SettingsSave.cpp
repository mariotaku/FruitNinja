// SettingsSave -- port-specific settings persistence (no binary counterpart).
// See header for contract. Mirrors FruitSaveData.cpp's GetSavePath /
// IDBFS-flush / tinyxml2-wrapper patterns for consistency with the game's
// other save file.

#include "SettingsSave.h"
#include "GameWork.h"
#include "debug/DebugFlags.h"
#include "engine/xml/TiXml.h"
#include "Game.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include <string>
#include <cstring>

namespace {

// Resolve the on-disk settings-save path. Mirrors FruitSaveData.cpp's
// GetSavePath() (not shared -- that helper is anonymous-namespace/local
// to that translation unit too).
std::string GetSettingsSavePath() {
#if defined(__EMSCRIPTEN__)
    // Port specific: on the web build, saves go to the IDBFS-backed /save
    // mount rather than the read-only MEMFS asset bundle.
    return std::string("/save/SettingsSave.xml");
#else
    Game* g = Game::GetInstance();
    if (!g) return std::string("SettingsSave.xml");
    return g->data_dir + "/SettingsSave.xml";
#endif
}

} // namespace

void SaveSettings() {
    TiXmlDocument doc;
    TiXmlElement root = doc.NewElement("settings");

    root.SetAttribute("languageFlag", (int)game_work.languageFlag);
    root.SetAttribute("motionMode", FN::g_MotionMode ? "true" : "false");
    root.SetAttribute("showFps", FN::g_ShowFps ? "true" : "false");
    root.SetAttribute("motionSpeedThreshold", FN::g_MotionSpeedThreshold);

    doc.InsertEndChild(root);
    doc.SaveFile(GetSettingsSavePath().c_str());
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

    float motionSpeedThreshold = 0.0f;
    if (root.QueryFloatAttribute("motionSpeedThreshold", &motionSpeedThreshold) == TIXML_SUCCESS) {
        FN::g_MotionSpeedThreshold = motionSpeedThreshold;
    }
}
