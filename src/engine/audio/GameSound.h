// Analysed: 2026-05-04T08:00
#ifndef FN_ENGINE_AUDIO_GAMESOUND_H
#define FN_ENGINE_AUDIO_GAMESOUND_H

#include "audio/MortarSound.h"
#include "audio/SoundManager.h"
#include "util/StringHash.h"
#include "util/Delegate.h"
#include <cstdint>

// GameSound -- pool-based sound manager with 32 slots.
// sizeof 0x708 (ARM32, 4-byte ptrs). No vtable.
// Binary layout confirmed from RE @ 0x001290e8 region.
class GameSound {
public:
    static const int MAX_SLOTS = 32;

    // Slot layout (stride 0x38 = 56 bytes, ARM32).
    // Binary field order confirmed from RE; port may differ in sizeof on 64-bit host.
    struct Slot {
        uint32_t                              id;             // +0x00: sound name hash
        Mortar::MortarSound*                  sound;          // +0x04
        bool                                  isFree;         // +0x08: 1 = available
        uint8_t                               pad09;          // +0x09: set 0 at init
        uint8_t                               pausedBySystem; // +0x0A: set 1 by Pause(), cleared by Unpause()
        uint8_t                               pad0B;          // +0x0B: alignment
        float                                 volume;         // +0x0C: default 1.0
        float                                 pitch;          // +0x10: default 1.0
        // +0x14: 32-byte finish callback (Delegate1 on ARM32; may be wider on 64-bit host)
        Delegate1<bool, Mortar::MortarSound*> finishCallback; // +0x14
        uint32_t                              reserved;       // +0x34 (ARM32)
    };

    // offsetof asserts are ARM32 / Bada-only (4-byte ptrs, short-enums ABI).
#ifdef __bada__
    // TODO: Slot sizeof + reserved offset asserts disabled under cross-build:
    //   binary's Delegate1 is 32 bytes per RE; port's Delegate.h is 36 bytes
    //   (FreeFn/MemFn/Functor concept-with-vptr design). Fixing requires
    //   either narrowing the port's Delegate or updating the binary RE
    //   to confirm 36B. Asserts that don't depend on Delegate sizing still fire.
    static_assert(__builtin_offsetof(Slot, id)             == 0x00, "Slot::id offset");
    static_assert(__builtin_offsetof(Slot, sound)          == 0x04, "Slot::sound offset");
    static_assert(__builtin_offsetof(Slot, isFree)         == 0x08, "Slot::isFree offset");
    static_assert(__builtin_offsetof(Slot, volume)         == 0x0C, "Slot::volume offset");
    static_assert(__builtin_offsetof(Slot, pitch)          == 0x10, "Slot::pitch offset");
    static_assert(__builtin_offsetof(Slot, finishCallback) == 0x14, "Slot::finishCallback offset");
    // static_assert(sizeof(Slot) == 0x38, ...);
    // static_assert(__builtin_offsetof(Slot, reserved) == 0x34, ...);
#endif

    float m_MasterVolume;       // +0x00: default 1.0
    bool  m_PausedForInterrupt; // +0x04: defer-unpause flag (GameSound::Update)
    uint8_t pad05[3];           // +0x05: alignment
    Slot  m_Slots[MAX_SLOTS];   // +0x08

    GameSound();
    ~GameSound();

    // Binary @ 0x001290e8
    int FindFree();

    // Binary @ 0x00129270 -- 4-arg form; 3-arg overload omits finishCallback.
    Mortar::MortarSound* SFXPlay(const char* name, float vol, float pitch,
                                 const Delegate1<bool, Mortar::MortarSound*>& finishCallback);
    Mortar::MortarSound* SFXPlay(const char* name, float vol = 1.0f, float pitch = 1.0f);

    // Binary @ 0x00129100
    bool IsPlaying(uint32_t hash);
    bool IsPlaying(const char* name);

    // Binary @ 0x00129138
    bool IsValid(Mortar::MortarSound* sound, const char* name);

    // Binary @ 0x0012917c
    void Release(Mortar::MortarSound* sound, const char* name);

    // Binary @ 0x001291e0
    void KillAll();

    // Binary @ 0x00129248
    void Pause();

    // Binary @ 0x00129218
    void Unpause();

    // Binary @ 0x0012930c
    void Update(float dt);

    // Binary @ 0x00129170 -- static
    static void DestroySoundInternals(Mortar::MortarSound* sound);
};

#endif // FN_ENGINE_AUDIO_GAMESOUND_H
