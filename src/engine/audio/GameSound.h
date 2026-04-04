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

    // SoundSlot (0x38 = 56 bytes)
    struct SoundSlot {
        MortarSound* pSound;   // +0x00
        uint32_t nameHash;     // +0x04
        uint8_t isFree;        // +0x10: 1=free, 0=in use
        float volume;          // +0x14: default 1.0
        float pitch;           // +0x18
    };

    float m_MasterVolume;           // +0x00: default 1.0
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

    // Matches Release (0x0012917c)
    void Release(MortarSound* sound, const char* name);

    // Matches KillAll (0x001291e0)
    void KillAll();
};

} // namespace Mortar

#endif
