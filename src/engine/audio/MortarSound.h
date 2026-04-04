#ifndef MORTAR_SOUND_H
#define MORTAR_SOUND_H

#include <cstdint>

namespace Mortar {

// Matches original MortarSound (16 bytes)
class MortarSound {
public:
    const char* m_Name;  // +0x04: sound name
    uint32_t m_Handle;   // +0x08: backend handle (0 = not loaded)
    int m_State;         // +0x0C: 0=idle, 1=paused, 2=playing

    MortarSound();
    virtual ~MortarSound();

    bool IsPlaying() const { return m_State == 2; }
    bool IsPaused() const { return m_State == 1; }
    bool IsIdle() const { return m_State == 0; }

    void SetVolume(float vol);
    void Stop();
    void Pause();
    void Play();
};

} // namespace Mortar

#endif
