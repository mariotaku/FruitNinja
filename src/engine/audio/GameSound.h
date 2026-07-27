// Analysed: 2026-05-04T08:00
#ifndef FN_ENGINE_AUDIO_GAMESOUND_H
#define FN_ENGINE_AUDIO_GAMESOUND_H

#include "audio/MortarSound.h"
#include "audio/SoundManager.h"
#include "util/StringHash.h"
#include "util/Delegate.h"
#include <cstdint>

// GameSound -- pool-based sound manager with 32 slots.
// sizeof 0x788 (ARM32, 4-byte ptrs): 8 header + 32 * 0x3c = 0x788. No vtable.
// v1.6.1 GameSound ctor @0x001290e8.
class GameSound {
public:
    static const int MAX_SLOTS = 32;

    // Slot layout (stride 0x3c = 60 bytes, ARM32).
    // Field order verified from SFXPlay @ 0x00151d04:
    //   str r0,[r4,#0x8] -> sound at +0x00; str r3,[r4,#0xc] -> id at +0x04.
    // ARM ABI Tag_ABI_align_needed:8 forces Delegate1 (8-byte aligned) to +0x18;
    // a 4-byte pad sits at +0x14 between pitch and finishCallback.
    // Mortar::Delegate1 is 36 bytes; fills +0x18..+0x3b.
    struct Slot {
        Mortar::MortarSound*                  sound;          // +0x00
        uint32_t                              id;             // +0x04: sound name hash
        bool                                  isFree;         // +0x08: 1 = available
        uint8_t                               pad09;          // +0x09: set 0 at init
        uint8_t                               pausedBySystem; // +0x0A: set 1 by Pause(), cleared by Unpause()
        uint8_t                               pad0B;          // +0x0B: alignment
        float                                 volume;         // +0x0C: default 1.0
        float                                 pitch;          // +0x10: default 1.0
        uint8_t                               _pad14[4];      // +0x14: alignment pad (8-byte align for Delegate1)
        Mortar::Delegate1<bool, Mortar::MortarSound*> finishCallback; // +0x18 (36 bytes; fills +0x18..+0x3b)
    };

    // offsetof asserts are ARM32 / Bada-only (4-byte ptrs, short-enums ABI).
#ifdef __bada__
    static_assert(sizeof(Slot) == 0x3c, "GameSound::Slot must be 0x3c bytes on ARM32");
    static_assert(__builtin_offsetof(Slot, sound)          == 0x00, "Slot::sound offset");
    static_assert(__builtin_offsetof(Slot, id)             == 0x04, "Slot::id offset");
    static_assert(__builtin_offsetof(Slot, isFree)         == 0x08, "Slot::isFree offset");
    static_assert(__builtin_offsetof(Slot, volume)         == 0x0C, "Slot::volume offset");
    static_assert(__builtin_offsetof(Slot, pitch)          == 0x10, "Slot::pitch offset");
    static_assert(__builtin_offsetof(Slot, finishCallback) == 0x18, "Slot::finishCallback offset");
#endif

    float m_MasterVolume;       // +0x00: default 1.0
    bool  m_PausedForInterrupt; // +0x04: defer-unpause flag (GameSound::Update)
    uint8_t pad05[3];           // +0x05: alignment
    Slot  m_Slots[MAX_SLOTS];   // +0x08

    GameSound();
    ~GameSound();

    // Binary @ 0x001290e8
    int FindFree();

    // v1.6.1 GameSound::SFXPlay @0x00151d04 -- single 5-param symbol. Trailing float = pitch;
    // fed once to MortarSound::SetPitch (@0x00230218 no-op stub on Bada), never stored.
    Mortar::MortarSound* SFXPlay(const char* name, float vol = 1.0f, float gain = 1.0f,
        Mortar::Delegate1<bool, Mortar::MortarSound*> finishCallback = Mortar::Delegate1<bool, Mortar::MortarSound*>(),
        float pitch = 0.0f);

    // v1.6.1 GameSound::IsPlaying(int) @0x00151aa8 -- first-match short-circuit:
    // returns MortarSound::IsPlaying() for the FIRST id-matching slot verbatim,
    // does not keep scanning later slots even if that result is false.
    bool IsPlaying(int hash);
    // v1.6.1 GameSound::IsPlaying(char const*) @0x00151ae4 -- hashes name, delegates to IsPlaying(int).
    bool IsPlaying(const char* name);

    // Binary @ 0x00129138
    bool IsValid(Mortar::MortarSound* sound, const char* name);

    // Binary @ 0x0012917c
    void Release(Mortar::MortarSound* sound, const char* name);

    // Binary @ 0x00151c00
    void KillAll();

    // Binary @ 0x00129248
    void Pause();

    // Binary @ 0x00129218
    void Unpause();

    // v1.6.1 GameSound::Update @ 0x00151e60
    void Update();

    // Binary @ 0x00151b60 -- static
    static void DestroySoundInternals(Mortar::MortarSound* sound);
};

#ifdef __bada__
// sizeof needs the complete type -- assert after the class definition.
static_assert(sizeof(GameSound) == 0x788, "GameSound size mismatch (v1.6.1 @0x001290e8)");
#endif

#endif // FN_ENGINE_AUDIO_GAMESOUND_H
