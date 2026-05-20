#ifndef FN_GAME_MODE_H
#define FN_GAME_MODE_H

#include <cstdint>

//
// Mortar / Fruit Ninja game-mode enum.
//
// Mirrors the binary's `GAME_MODE` (per `_Z11GetModeName9GAME_MODE`,
// `_Z13ParseGameModem`, and the `gameModeWaveListXMLs` lookup table).
// Storage on `Game::gameMode` stays `uint8_t` to preserve the binary
// struct layout at +0x04 -- compare via cast or via `mode == (uint8_t)X`.
//
// Mode-to-WaveList mapping resolved from binary rodata @ 0x001ba9a0..:
//   0 -> "xml/originalWaveList.xml"   (Classic)
//   1 -> "xml/comboWaveList.xml"      (Combo / "Casino" in callback names)
//   2 -> "xml/arcadeWaveList.xml"     (Arcade)
//   3 -> "xml/zenWaveList.xml"        (Zen)
//
// Use these named constants instead of raw 0/1/2/3 literals when
// comparing `game->gameMode`. Keep the storage type uint8_t so the
// Game struct layout (and asm-verify cross-build symbol shape) is
// unchanged.
//
namespace Mortar {

enum GameMode {
    GAME_MODE_CLASSIC = 0,   // originalWaveList.xml -- Classic / "Original"
    GAME_MODE_COMBO   = 1,   // comboWaveList.xml -- internal callback uses "Casino"
    GAME_MODE_ARCADE  = 2,   // arcadeWaveList.xml
    GAME_MODE_ZEN     = 3,   // zenWaveList.xml
    // Higher indices (Versus / Multiplayer / Tutorial) exist but are
    // out of scope for the SP-port; add as needed.
};

// Binary @ 0x0010a404 -- ((uint8_t)(gameMode - 2u) > 1u) means modes 0/1
// (Classic, Combo) return true; 2/3 (Arcade, Zen) return false. Gates the
// 3-strike miss-penalty / MissControl spawn path.
inline bool FailureEnabled(uint8_t gameMode) {
    return ((uint8_t)(gameMode - 2u)) > 1u;
}

// Binary @ 0x0010a44c -- ((uint8_t)(gameMode - 2u) < 2u) means modes 2/3
// (Arcade, Zen) return true; 0/1 (Classic, Combo) return false. Gates the
// TimeControl countdown HUD.
inline bool IsTimedGame(uint8_t gameMode) {
    return ((uint8_t)(gameMode - 2u)) < 2u;
}

// Binary @ 0x0010b15c -- GetModeName(GAME_MODE). Returns the ASCII mode-name
// string used to construct per-mode stat keys ("combo_CLASSIC", "combo_ARCADE", etc.).
// Names taken from rodata / FruitSaveData key evidence; see also FruitSaveData.cpp:k_ModeNames.
inline const char* GetModeName(uint8_t gameMode) {
    static const char* s_Names[] = { "CLASSIC", "CASINO", "ARCADE", "ZEN" };
    if (gameMode < 4) return s_Names[gameMode];
    return "UNKNOWN";
}

} // namespace Mortar

#endif // FN_GAME_MODE_H
