// Analysed: 2026-04-25T10:30
#ifndef MORTAR_SOUND_H
#define MORTAR_SOUND_H

#include <cstdint>

namespace Mortar {

// Matches original MortarSound (0x10 = 16 bytes)
// +0x00 vtable, +0x04 m_Name, +0x08 m_Handle, +0x0c m_State
// vtable @0x002cfcc0. The ctor body confirms the layout above.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 Mortar::MortarSound::MortarSound @ 0x00230030 (asm-inspector)
class MortarSound {
public:
    char*    m_Name;    // +0x04: heap-allocated sound name; NULL when idle
    uint32_t m_Handle;  // +0x08: MAMAudioController sound ID; 0 = not active
    int      m_State;   // +0x0c: 0=idle, 1=paused, 2=playing

    MortarSound();
    virtual ~MortarSound();

    // Handle-validity guard (binary pattern: if m_Handle==0 -> m_State=0)
    // Returns m_State==2 (v1.6.1 Mortar::MortarSound::IsPlaying @0x0023027c)
    bool IsPlaying();
    // Returns m_State==1 (v1.6.1 Mortar::MortarSound::IsPaused @0x0023029c)
    bool IsPaused();
    bool IsIdle() { return m_State == 0; }

    // Maps float to a 0-255 byte (vol*255, TRUNCATED -- wraps above 1.0, see
    // MortarSound.cpp) and calls backend SFXSetVolume. Every port backend
    // applies that byte as a LINEAR GAIN (byte/255), so SetVolume(0.0f) is
    // silent, 1.0f is full, and everything between fades smoothly.
    //
    // DIFFERS: original = mute gate, byte > 5 plays at FULL amplitude with
    // samples mixed raw (v1.6.1 MAMAudioThread::FillBuffer @0x0022f7f0); port
    // scales by the byte instead because reproducing the gate turns every
    // in-game fade into an abrupt on/off and forces sounds the game intends at
    // 1-7% to full volume -- a limitation of the 2010 mixer rather than a
    // design choice.
    //
    // A silenced sound keeps playing regardless (cursor advances, loops wrap,
    // completion fires) -- that part IS faithful.
    void SetVolume(float vol);

    // If m_State==0: calls SoundManager::SFXPlay, m_State=2
    // (v1.6.1 Mortar::MortarSound::Play @0x0023006c)
    void Play();

    // If m_State==2: calls backend PauseSound, m_State=1
    // (v1.6.1 Mortar::MortarSound::Pause @0x002300f0)
    void Pause();

    // If m_State==1: calls backend ResumeSound, m_State=2
    // (v1.6.1 Mortar::MortarSound::Resume @0x00230128)
    void Resume();

    // If m_Handle!=0: calls backend StopSound, m_State=0, m_Handle=0
    // (v1.6.1 Mortar::MortarSound::Stop @0x00230160)
    // fadeTime is always 0.0f in all observed calls (DAT_0018c8cc=0.0f)
    void Stop(float fadeTime = 0.0f);

    // Non-virtual wrapper -- calls InternalDestroy
    // (v1.6.1 Mortar::MortarSound::Destroy @0x00230064)
    void Destroy();

    // Free m_Name, Stop, RemoveListener
    // (v1.6.1 Mortar::MortarSound::InternalDestroy @0x00230190)
    void InternalDestroy();

    // v1.6.1 Mortar::MortarSound::Load @0x00230068 -- one-line tail-call wrapper
    // (4 bytes: a single b.w to InternalLoad), calls InternalLoad
    void Load(const char* name);

    // Heap-copies name into m_Name; calls InternalDestroy first if m_Name != null
    // (v1.6.1 Mortar::MortarSound::InternalLoad @0x002301d0)
    void InternalLoad(const char* name);

    // Handle-validity guard + always returns true
    // (v1.6.1 Mortar::MortarSound::IsReady @0x002302bc)
    // Note: binary is a no-op stub; loads complete synchronously.
    bool IsReady();

    // ASM-spec v1.6.1 MortarSound::SetPitch @0x00230218: (float) — pitch arg discarded in binary body.
    void SetPitch(float pitch);
};

} // namespace Mortar

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(Mortar::MortarSound) == 0x10, "Mortar::MortarSound size mismatch"); // v1.6.1 SoundManager::CreateNewSound @0x002304a4 -- operator new(0x10) sizes MortarSound
#endif

#endif
