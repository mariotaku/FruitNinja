// Analysed: 2026-05-04T08:00
#include "audio/GameSound.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdio>

using namespace Mortar;


// ASM-spec v1.6.1 GameSound::GameSound @0x00151ff4 (C1) / @0x001520a0 (C2, identical):
// per slot writes sound = SoundManager::CreateNewSound(), isFree = 1, pad09 = 0,
// pausedBySystem = 0, id = 0, volume = 1.0f -- then m_MasterVolume = 1.0f and
// m_PausedForInterrupt = false. It never writes pitch (+0x10).
// DIFFERS: original leaves Slot::pitch indeterminate at construction; the port seeds
// it to 1.0f so a slot read before SFXPlay writes it can't hit an indeterminate float.
GameSound::GameSound()
    : m_MasterVolume(1.0f)
    , m_PausedForInterrupt(false)
{
    memset(pad05, 0, sizeof(pad05));
    SoundManager& mgr = SoundManager::GetInstance();
    for (int i = 0; i < MAX_SLOTS; i++) {
        m_Slots[i].id             = 0;
        m_Slots[i].sound          = mgr.CreateNewSound();
        m_Slots[i].isFree         = true;
        m_Slots[i].pad09          = 0;
        m_Slots[i].pausedBySystem = 0;
        m_Slots[i].pad0B          = 0;
        m_Slots[i].volume         = 1.0f;
        m_Slots[i].pitch          = 1.0f;
        // finishCallback default-constructs to empty
    }
}

// ASM-spec v1.6.1 GameSound::~GameSound @0x00151ebc (D1) / @0x00151f58 (D2, identical)
GameSound::~GameSound() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (m_Slots[i].sound) {
            delete m_Slots[i].sound;   // virtual dtor -> MortarSound::~MortarSound() -> InternalDestroy()
            m_Slots[i].sound = nullptr;
        }
    }
}

// ASM-verified: 2026-07-31T00:00Z v1.6.1 GameSound::FindFree @ 0x00151a7c (asm-inspector)
// First-fit, and ONLY first-fit: scan slots 0..31 (stride 0x3c, isFree at slot+0x08 =
// this+0x10), return the first index whose isFree is non-zero, else -1. There is no
// second predicate -- the binary never inspects id, sound, pausedBySystem or volume,
// and it never steals a busy slot. That last part is load-bearing (see fd9b9d1e): the
// bomb-fuse block holds a raw MortarSound* for the whole session with no validity
// guard, which is only safe because a live voice can never be recycled under it.
//
// asm-verify reports this as DIVERGE "44.4% LCS (18p vs 11b)". It is COSMETIC. GCC
// 4.4.1's loop-header copy (-ftree-ch, on at -O2) rotates the loop and duplicates the
// isFree test, so the port emits a check of slot+0x10 AND slot+0x4c with the cursor
// advancing 0x78 per turn. That second `ldrb`/`cmp`/`b` is the NEXT slot in the
// rotated loop, not an extra condition on the same slot. Confirmed by compiling three
// different source spellings (this for-loop, a pointer do-while, and a while + early
// return) with the Bada toolchain at -O2: the for-loop and the while both emit the
// identical 18 instructions, the do-while emits 23. No spelling reproduces the
// binary's 11, so the divergence is not addressable from the source side.
int GameSound::FindFree() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (m_Slots[i].isFree) return i;
    }
    return -1;
}

// ASM-verified: 2026-07-24T00:00Z v1.6.1 GameSound::SFXPlay @0x00151d04 (asm-inspector)
// s0 is an ATTENUATION term, not raw volume: finalVol = (1 - (1-master)*s0) * s1. So s0=0.0 => full volume.
// DIFFERS: binary v1.6.1 GameSound::SFXPlay @0x00151d04 calls SoundManager::SFXPlay(name, 0, NULL, 0x40, -1);
//          port simplifies to 2-arg form. Mirror of the marker in MortarSound.cpp::Play.
MortarSound* GameSound::SFXPlay(const char* name, float vol, float gain,
                                 Mortar::Delegate1<bool, MortarSound*> finishCallback,
                                 float pitch) {
    int i = FindFree();
    if (i == -1) {
        LOG_INFO("SFX", "SFXPlay('%s', vol=%.2f, gain=%.2f) -- NO FREE SLOT",
                 name ? name : "(null)", vol, gain);
        return NULL;
    }

    LOG_INFO("SFX", "SFXPlay('%s', vol=%.2f, gain=%.2f, master=%.2f) slot=%d",
             name ? name : "(null)", vol, gain, m_MasterVolume, i);

    SoundManager& mgr = SoundManager::GetInstance();
    mgr.SFXPlay(name, m_Slots[i].sound);

    m_Slots[i].isFree         = false;
    m_Slots[i].id             = StringHash(name);
    m_Slots[i].pitch          = gain;
    m_Slots[i].volume         = vol;
    m_Slots[i].finishCallback = finishCallback;

    float finalVol = (1.0f - (1.0f - m_MasterVolume) * vol) * gain;
    m_Slots[i].sound->SetVolume(finalVol);
    m_Slots[i].sound->SetPitch(pitch);

    return m_Slots[i].sound;
}

// ASM-spec v1.6.1 GameSound::IsPlaying(int) @0x00151aa8: breaks the slot scan at the
// FIRST id-matching slot and returns MortarSound::IsPlaying() for that slot verbatim
// (no further scanning of later slots, even on false).
bool GameSound::IsPlaying(int hash) {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!m_Slots[i].isFree && m_Slots[i].id == hash) {
            if (m_Slots[i].sound == NULL) return false;
            return m_Slots[i].sound->IsPlaying();
        }
    }
    return false;
}

bool GameSound::IsPlaying(const char* name) {
    return IsPlaying(StringHash(name));
}

// ASM-spec v1.6.1 GameSound::IsValid @0x00151b04: breaks the scan at the FIRST slot
// whose sound pointer matches -- no isFree test, and the id is NOT part of the loop
// condition -- then returns that slot's id == hash. A later slot holding the same
// pointer is never reached.
bool GameSound::IsValid(MortarSound* sound, const char* name) {
    uint32_t hash = StringHash(name);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (m_Slots[i].sound == sound) {
            return m_Slots[i].id == hash;
        }
    }
    return false;
}

// ASM-spec v1.6.1 GameSound::Release @0x00151b68: matches on (sound && id) with no
// isFree test, stops the voice only while it is still playing, then destroys the
// sound internals and frees the slot (id=0, isFree=1, pad09=0).
void GameSound::Release(MortarSound* sound, const char* name) {
    uint32_t hash = StringHash(name);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (m_Slots[i].sound == sound && m_Slots[i].id == hash) {
            if (sound->IsPlaying()) {
                sound->Stop(0.0f);
            }
            DestroySoundInternals(sound);
            m_Slots[i].id     = 0;
            m_Slots[i].isFree = true;
            m_Slots[i].pad09  = 0;
            return;
        }
    }
}

// ASM-spec v1.6.1 GameSound::KillAll @0x00151c00 -- per-slot Stop+DestroySoundInternals gated on !isFree;
// isFree/pausedBySystem/id reset unconditionally for every slot.
void GameSound::KillAll() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot* s = &m_Slots[i];
        if (!s->isFree) {
            if (s->sound) {
                s->sound->Stop(0.0f);
                DestroySoundInternals(s->sound);
            }
        }
        s->isFree         = true;
        s->pausedBySystem = 0;
        s->id             = 0;
    }
}

// ASM-spec v1.6.1 GameSound::Pause @0x00151cb8
void GameSound::Pause() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot* s = &m_Slots[i];
        if (!s->isFree && s->sound->IsPlaying()) {
            s->sound->Pause();
            s->pausedBySystem = 1;
        }
    }
}

// ASM-spec v1.6.1 GameSound::Unpause @0x00151c60
void GameSound::Unpause() {
    SoundManager& mgr = SoundManager::GetInstance();
    if (mgr.IsInterrupted()) {
        m_PausedForInterrupt = true;
        return;
    }
    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot* s = &m_Slots[i];
        if (s->pausedBySystem != 0) {
            s->sound->Resume();
            s->pausedBySystem = 0;
        }
    }
}

// ASM-verified: 2026-05-04T11:00 v1.6.1 GameSound::Update @ 0x00151dd0 (asm-inspector)
// NOTE the per-slot re-apply below: (1 - (1-master)*vol) * PITCH runs every
// frame for every live slot with vol > 0 -- for a slot played with gain 0
// (e.g. SpeedControl's first Combo-Blitz-Backing SFXPlay) this writes volume
// byte 0 each frame, which is what silences that stream the moment
// SpeedControl::Update stops re-raising it (pause/menu: bM_Mode gate).
void GameSound::Update() {
    if (m_PausedForInterrupt) {
        SoundManager& mgr = SoundManager::GetInstance();
        if (mgr.IsInterrupted()) return;
        m_PausedForInterrupt = false;
        Unpause();
    }

    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot* s = &m_Slots[i];
        if (s->isFree || s->sound == NULL) continue;

        if (!s->sound->IsPlaying() && !s->sound->IsPaused()) {
            if (static_cast<bool>(s->finishCallback)) {
                bool restartedLoop = s->finishCallback(s->sound);
                if (restartedLoop) return;
            }
            DestroySoundInternals(s->sound);
            s->id     = 0;
            s->isFree = true;
            continue;
        }

        if (s->volume > 0.0f) {
            s->sound->SetVolume(
                (1.0f - (1.0f - m_MasterVolume) * s->volume) * s->pitch
            );
        }
    }
}

// ASM-spec v1.6.1 GameSound::DestroySoundInternals @0x00151b60 -- one-line body.
// The binary passes `this` (it is a non-static member); the port declares it static
// since the body never touches the pool. Itanium mangling is identical either way.
void GameSound::DestroySoundInternals(MortarSound* sound) {
    sound->Destroy();
}
