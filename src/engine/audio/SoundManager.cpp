#include "audio/SoundManager.h"

namespace Mortar {

// Static globals
float SoundManager::s_SFXVolume = 1.0f;
float SoundManager::s_MusicVolume = 0.45f;
bool SoundManager::s_SFXMuted = false;
bool SoundManager::s_MusicMuted = false;

SoundManager::SoundManager() {
}

SoundManager::~SoundManager() {
    for (size_t i = 0; i < m_Sounds.size(); i++) {
        delete m_Sounds[i];
    }
    m_Sounds.clear();
}

MortarSound* SoundManager::CreateNewSound() {
    MortarSound* s = new MortarSound();
    m_Sounds.push_back(s);
    return s;
}

void SoundManager::PreLoadSound(const char* name) {
    (void)name;
    // Stub — implemented by SDL backend
}

uint32_t SoundManager::SFXPlay(const char* name, MortarSound* sound) {
    (void)name; (void)sound;
    return 0; // Stub — implemented by SDL backend
}

void SoundManager::SFXStop(uint32_t handle) { (void)handle; }
void SoundManager::SFXPauseAll() {}
void SoundManager::SFXUnpauseAll() {}
void SoundManager::SFXSetVolume(uint32_t handle, float vol) { (void)handle; (void)vol; }
void SoundManager::MusicPlay(const char* name) { (void)name; }
void SoundManager::MusicStop() {}
void SoundManager::MusicPause() {}
void SoundManager::MusicResume() {}

void SoundManager::SetMusicVolume(float vol) {
    s_MusicVolume = vol;
    s_MusicMuted = (vol <= 0.0f);
}

void SoundManager::SetSFXVolume(float vol) {
    s_SFXVolume = vol;
    s_SFXMuted = (vol <= 0.0f);
}

} // namespace Mortar
