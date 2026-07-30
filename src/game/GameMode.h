#ifndef FN_GAME_MODE_H
#define FN_GAME_MODE_H

#include <cstdint>
#include "game/GameWork.h"

//
// Fruit Ninja game-mode enum.
//
// Binary's enum is GLOBAL `::GAME_MODE` (per the mangled symbols
// `_Z11GetModeName9GAME_MODE`, `_Z14GetModeBitMask9GAME_MODE`, and
// FruitSaveData::PlayedModeToday @0x00152fd8 taking `GAME_MODE` by value) --
// mangling encodes the canonical (non-typedef) type, so the enum must live
// at global scope, not inside `namespace Mortar`, for those symbols to pair
// with the binary. `Mortar::GameMode` is kept as a typedef alias so the
// ~50 existing `Mortar::GameMode` / `Mortar::GAME_MODE_*` call sites across
// the codebase keep compiling unchanged.
//
// Mode-to-WaveList mapping resolved from binary rodata @ 0x001ba9a0..:
//   0 -> "xml/originalWaveList.xml"   (Classic)
//   1 -> "xml/comboWaveList.xml"      (Combo / "Casino" in callback names)
//   2 -> "xml/arcadeWaveList.xml"     (Arcade)
//   3 -> "xml/zenWaveList.xml"        (Zen)
//
// Use these named constants instead of raw 0/1/2/3 literals when
// comparing `game_work.gameMode`. Keep the storage type uint8_t so the
// Game struct layout (and asm-verify cross-build symbol shape) is
// unchanged.
//
enum GAME_MODE {
    GAME_MODE_CLASSIC = 0,   // originalWaveList.xml -- Classic / "Original"
    GAME_MODE_COMBO   = 1,   // comboWaveList.xml -- internal callback uses "Casino"
    GAME_MODE_ARCADE  = 2,   // arcadeWaveList.xml
    GAME_MODE_ZEN     = 3,   // zenWaveList.xml
    // Higher indices (Versus / Multiplayer / Tutorial) exist but are
    // out of scope for the SP-port; add as needed.
};

namespace Mortar {

// Back-compat alias -- binary's enum is global `::GAME_MODE` (see file header);
// this typedef lets existing `Mortar::GameMode` call sites keep compiling
// without a 26-file rename sweep.
typedef ::GAME_MODE GameMode;

// A typedef alone does NOT inject the enum's enumerators into this namespace,
// so re-export them here so `Mortar::GAME_MODE_*` call sites resolve. The enum
// itself stays global `::GAME_MODE` for binary-mangling fidelity (see header).
using ::GAME_MODE_CLASSIC;
using ::GAME_MODE_COMBO;
using ::GAME_MODE_ARCADE;
using ::GAME_MODE_ZEN;

// v1.6.1 FailureEnabled @0x00119ff8 -- ((uint8_t)(gameMode - 2u) > 1u) means modes 0/1
// (Classic, Combo) return true; 2/3 (Arcade, Zen) return false. Gates the
// 3-strike miss-penalty / MissControl spawn path.
inline bool FailureEnabled(uint8_t gameMode) {
    return ((uint8_t)(gameMode - 2u)) > 1u;
}

// v1.6.1 IsTimedGame @0x0011a060 -- ((uint8_t)(gameMode - 2u) < 2u) means modes 2/3
// (Arcade, Zen) return true; 0/1 (Classic, Combo) return false. Gates the
// TimeControl countdown HUD.
inline bool IsTimedGame(uint8_t gameMode) {
    return ((uint8_t)(gameMode - 2u)) < 2u;
}

// ASM-spec v1.6.1 IsDeviceUpsideDown @0x0011a154: reads the float at Game+0x1b0
// (vldr.32 s15,[r3,#0x1b0]) and returns x > 0. That accelerometer field is never
// written in the shipped binary (dead initialisation path), so it always returns 0.
// Gates the ShopListItem locked-state-1 red prompt.
// Defunct: accelerometer DeviceUpsideDown -- no-op stub; v1.6.1 IsDeviceUpsideDown @ 0x0011a154
inline bool IsDeviceUpsideDown() {
    return false;
}

} // namespace Mortar

// v1.6.1 GetModeName @0x0011bac0 (_Z11GetModeName9GAME_MODE)
// Returns the ASCII mode-name string for the given game mode.
const char* GetModeName(GAME_MODE gameMode);

// ASM-spec v1.6.1 GetModeBitMask @0x0011bae0 (_Z14GetModeBitMask9GAME_MODE) -- whole body:
//   0011bae0: cmp    r0,#0x4
//   0011bae4: movne  r3,#0x1
//   0011bae8: mvneq  r0,#0x0
//   0011baec: movne  r0,r3, lsl r0
//   0011baf0: bx     lr
// Mode 4 is NOT bit 4. It is the "unknown / every mode" sentinel that ParseGameMode
// @0x0011bf6c returns for an unrecognised mode name, and it maps to the WILDCARD
// 0xFFFFFFFF. Every other value shifts bit 0 left by the mode index -- a range
// dispatch, never a `& 3` fold, so mode 5..31 gets its own high bit rather than
// aliasing onto 0..3.
// Callers always pass a zero-extended byte (ldrb of game_work+0x4 / ParseGameMode's
// 0..4 result). An ARM register-specified LSL by 32..255 produces 0, while C's <<
// is undefined there -- hence the explicit early return for >= 32.
// Used by ParseModeMask @0x0014f320, GlobalProbabilityOveride::CheckForOverride, and
// the eight mode-gated AchievementManager::Unlock*Achievement paths.
inline uint32_t GetModeBitMask(GAME_MODE gameMode) {
    if ((unsigned)gameMode == 4u)  return 0xFFFFFFFFu;
    if ((unsigned)gameMode >= 32u) return 0u;
    return (uint32_t)(1u << (unsigned)gameMode);
}

// ParseModeMask -- v1.6.1 @0x0014f320.
// Parses a comma-separated mode-name string ("CLASSIC", "ARCADE,CLASSIC", etc.)
// into a bitmask of GAME_MODE bits (bit N = mode N).
// Unknown tokens are ignored. Empty/null -> 0xFFFFFFFF (all modes).
uint32_t ParseModeMask(const char* modeStr);

// ParseGameMode -- v1.6.1 @0x0011bf6c (_Z13ParseGameModem).
// Maps a pre-computed StringHash of a mode name to its index: CLASSIC=0, CASINO=1,
// ARCADE=2, ZEN=3. Returns 4 if the hash does not match any known mode.
unsigned int ParseGameMode(unsigned long nameHash);

#endif // FN_GAME_MODE_H
