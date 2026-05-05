// Analysed: 2026-04-25T10:30
#ifndef MORTAR_SOUND_H
#define MORTAR_SOUND_H

#include <cstdint>

namespace Mortar {

// Matches original MortarSound (0x10 = 16 bytes)
// +0x00 vtable, +0x04 m_Name, +0x08 m_Handle, +0x0c m_State
// ASM-verified: 2026-04-29T00:00Z binary @ 0x0018c6ac (asm-inspector)
class MortarSound {
public:
    char*    m_Name;    // +0x04: heap-allocated sound name; NULL when idle
    uint32_t m_Handle;  // +0x08: MAMAudioController sound ID; 0 = not active
    int      m_State;   // +0x0c: 0=idle, 1=paused, 2=playing

    MortarSound();
    virtual ~MortarSound();

    // Handle-validity guard (binary pattern: if m_Handle==0 -> m_State=0)
    // Returns m_State==2 (0x0018c780)
    bool IsPlaying();
    // Returns m_State==1 (0x0018c794)
    bool IsPaused();
    bool IsIdle() { return m_State == 0; }

    // Maps 0-1 float to 0-255 byte, calls backend SetSoundVolume (0x0018c7b4)
    // DAT_0018c7ec = 255.0f
    void SetVolume(float vol);

    // If m_State==0: calls SoundManager::SFXPlay, m_State=2 (0x0018c850)
    void Play();

    // If m_State==2: calls backend PauseSound, m_State=1 (0x0018c830)
    void Pause();

    // If m_State==1: calls backend ResumeSound, m_State=2 (0x0018c810)
    void Resume();

    // If m_Handle!=0: calls backend StopSound, m_State=0, m_Handle=0 (0x0018c7f0)
    // fadeTime is always 0.0f in all observed calls (DAT_0018c8cc=0.0f)
    void Stop(float fadeTime = 0.0f);

    // Non-virtual wrapper -- calls InternalDestroy (0x0018c6fc)
    void Destroy();

    // Free m_Name, Stop, RemoveListener (0x0018c8a4)
    void InternalDestroy();

    // Copy name into m_Name (heap). Called from SFXPlayInternal.
    // No binary symbol -- inferred from call context (see docs/engine/sound.md)
    void Load(const char* name);

public:

public:

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: MortarSound::InternalLoad -- auto stub from binary missing-symbol set
    void InternalLoad(char const*);
    // STUB: MortarSound::IsReady -- auto stub from binary missing-symbol set
    void IsReady();
    // STUB: MortarSound::SetPitch -- auto stub from binary missing-symbol set
    void SetPitch(unsigned int);
    // ---- end AUTO-STUB MERGE ----
};

} // namespace Mortar

#endif
