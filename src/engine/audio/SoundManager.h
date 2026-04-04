#ifndef MORTAR_SOUND_MANAGER_H
#define MORTAR_SOUND_MANAGER_H

#include "audio/MortarSound.h"
#include "core/Singleton.h"
#include <vector>
#include <map>
#include <string>
#include <cstdint>

namespace Mortar {

// Matches original SoundManager (40 bytes)
// Abstract base singleton for sound playback
class SoundManager : public Singleton<SoundManager> {
    friend class Singleton<SoundManager>;

public:
    // Volume/mute state (static globals in original)
    static float s_SFXVolume;
    static float s_MusicVolume;
    static bool s_SFXMuted;
    static bool s_MusicMuted;

    // Sound management
    virtual MortarSound* CreateNewSound();
    virtual void PreLoadSound(const char* name);
    virtual uint32_t SFXPlay(const char* name, MortarSound* sound = NULL);
    virtual void SFXStop(uint32_t handle);
    virtual void SFXPauseAll();
    virtual void SFXUnpauseAll();
    virtual void SFXSetVolume(uint32_t handle, float vol);

    // Music
    virtual void MusicPlay(const char* name);
    virtual void MusicStop();
    virtual void MusicPause();
    virtual void MusicResume();

    float GetMusicVolume() const { return s_MusicVolume; }
    void SetMusicVolume(float vol);
    void SetSFXVolume(float vol);

    virtual ~SoundManager();

protected:
    SoundManager();

    std::vector<MortarSound*> m_Sounds;
};

} // namespace Mortar

#endif
