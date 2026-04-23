#include "audio/GameSound.h"

namespace Mortar {

GameSound::GameSound()
    : m_MasterVolume(1.0f)
{
    SoundManager& mgr = SoundManager::GetInstance();
    for (int i = 0; i < MAX_SLOTS; i++) {
        m_Slots[i].pSound = mgr.CreateNewSound();
        m_Slots[i].nameHash = 0;
        m_Slots[i].isFree = 1;
        m_Slots[i].volume = 1.0f;
        m_Slots[i].pitch = 1.0f;
    }
}

GameSound::~GameSound() {
    KillAll();
    // pSound objects are owned by SoundManager, not destroyed here
}

int GameSound::FindFree() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (m_Slots[i].isFree) return i;
    }
    return -1;
}

// Matches 0x00129270
MortarSound* GameSound::SFXPlay(const char* name, float vol, float pitch) {
    int i = FindFree();
    if (i == -1) return nullptr;

    SoundManager& mgr = SoundManager::GetInstance();
    mgr.SFXPlay(name, m_Slots[i].pSound);

    m_Slots[i].isFree = 0;
    m_Slots[i].nameHash = StringHash(name);
    m_Slots[i].pitch = pitch;
    m_Slots[i].volume = vol;

    // Volume formula: (1 - (1 - masterVol) * vol) * pitch
    float finalVol = (1.0f - (1.0f - m_MasterVolume) * vol) * pitch;
    m_Slots[i].pSound->SetVolume(finalVol);

    return m_Slots[i].pSound;
}

bool GameSound::IsPlaying(uint32_t hash) {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!m_Slots[i].isFree && m_Slots[i].nameHash == hash) {
            if (m_Slots[i].pSound && m_Slots[i].pSound->IsPlaying()) {
                return true;
            }
        }
    }
    return false;
}

bool GameSound::IsPlaying(const char* name) {
    return IsPlaying(StringHash(name));
}

void GameSound::Release(MortarSound* sound, const char* name) {
    uint32_t hash = StringHash(name);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!m_Slots[i].isFree && m_Slots[i].pSound == sound &&
            m_Slots[i].nameHash == hash) {
            m_Slots[i].pSound->Stop();
            m_Slots[i].isFree = 1;
            m_Slots[i].nameHash = 0;
            return;
        }
    }
}

void GameSound::KillAll() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!m_Slots[i].isFree) {
            if (m_Slots[i].pSound) {
                m_Slots[i].pSound->Stop();
            }
            m_Slots[i].isFree = 1;
            m_Slots[i].nameHash = 0;
        }
    }
}

} // namespace Mortar
