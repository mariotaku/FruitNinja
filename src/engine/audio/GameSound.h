// Analysed: 2026-04-25T10:30
#ifndef MORTAR_GAME_SOUND_H
#define MORTAR_GAME_SOUND_H

#include "audio/MortarSound.h"
#include "audio/SoundManager.h"
#include "util/StringHash.h"
#include <cstdint>

namespace Mortar {

// Matches original GameSound (0x708 = 1800 bytes)
// Pool-based sound manager with 32 slots
class GameSound {
public:
    static const int MAX_SLOTS = 32;

    // SoundSlot (0x38 = 56 bytes) -- revised layout per docs/engine/sound.md
    struct SoundSlot {
        MortarSound* pSound;   // +0x00
        uint32_t nameHash;     // +0x04
        uint8_t  pad08[8];     // +0x08: padding (2 unknown fields)
        uint8_t  isFree;       // +0x10: 1=free, 0=in use
        uint8_t  field11;      // +0x11: set 0 at init
        uint8_t  field12;      // +0x12: 1 = paused-by-system (GameSound::Pause/Unpause)
        uint8_t  pad13;        // +0x13: alignment pad
        float    volume;       // +0x14: default 1.0
        float    pitch;        // +0x18
        // +0x1c: 28 bytes Delegate1<bool,MortarSound*> callback (stubbed as raw bytes)
        uint8_t  callback[28]; // +0x1c
    };
    // DIFFERS: original is 0x38 = 56 bytes on ARM32 (4-byte ptr). Port is larger on 64-bit host.
    // static_assert removed; layout is correct for ARM32 target (4-byte MortarSound*)

    float m_MasterVolume;           // +0x00: default 1.0
    int   m_field04;                // +0x04: interruption-deferred flag (GameSound::Update)
    SoundSlot m_Slots[MAX_SLOTS];   // +0x08

    GameSound();
    ~GameSound();

    // Matches FindFree (0x001290e8)
    int FindFree();

    // Matches SFXPlay (0x00129270)
    MortarSound* SFXPlay(const char* name, float vol = 1.0f, float pitch = 1.0f);

    // Matches IsPlaying (0x00129100)
    bool IsPlaying(uint32_t hash);
    bool IsPlaying(const char* name);

    // Matches IsValid (0x00129138)
    bool IsValid(MortarSound* sound, const char* name);

    // Matches Release (0x0012917c)
    void Release(MortarSound* sound, const char* name);

    // Matches KillAll (0x001291e0)
    void KillAll();

    // Matches GameSound::Pause (0x00129256)
    void Pause();

    // Matches GameSound::Unpause (0x00129218)
    void Unpause();

    // Matches GameSound::Update (0x00129380)
    void Update(float dt);

    // Matches DestroySoundInternals (0x00129170) -- static
    static void DestroySoundInternals(MortarSound* sound);
};

} // namespace Mortar

#endif
