// Analysed: 2026-04-25T10:30
#include "audio/GameSound.h"
#include <cstring>
#include <cstdio>

using namespace Mortar;

GameSound::GameSound()
    : m_MasterVolume(1.0f)
    , m_field04(0)
{
    SoundManager& mgr = SoundManager::GetInstance();
    for (int i = 0; i < MAX_SLOTS; i++) {
        m_Slots[i].pSound    = mgr.CreateNewSound();
        m_Slots[i].nameHash  = 0;
        memset(m_Slots[i].pad08, 0, sizeof(m_Slots[i].pad08));
        m_Slots[i].isFree    = 1;
        m_Slots[i].field11   = 0;
        m_Slots[i].field12   = 0;
        m_Slots[i].pad13     = 0;
        m_Slots[i].volume    = 1.0f;
        m_Slots[i].pitch     = 1.0f;
        memset(m_Slots[i].callback, 0, sizeof(m_Slots[i].callback));
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
// ASM-verified: 2026-04-29T00:00Z binary @ 0x00129270 (asm-inspector)
MortarSound* GameSound::SFXPlay(const char* name, float vol, float pitch) {
    int i = FindFree();
    if (i == -1) return nullptr;

    SoundManager& mgr = SoundManager::GetInstance();
    mgr.SFXPlay(name, m_Slots[i].pSound);

    m_Slots[i].isFree    = 0;
    m_Slots[i].nameHash  = StringHash(name);
    m_Slots[i].pitch     = pitch;
    m_Slots[i].volume    = vol;

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

// Matches 0x00129138
bool GameSound::IsValid(MortarSound* sound, const char* name) {
    uint32_t hash = StringHash(name);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!m_Slots[i].isFree && m_Slots[i].pSound == sound &&
            m_Slots[i].nameHash == hash) {
            return true;
        }
    }
    return false;
}

void GameSound::Release(MortarSound* sound, const char* name) {
    uint32_t hash = StringHash(name);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!m_Slots[i].isFree && m_Slots[i].pSound == sound &&
            m_Slots[i].nameHash == hash) {
            m_Slots[i].pSound->Stop(0.0f);
            m_Slots[i].isFree   = 1;
            m_Slots[i].nameHash = 0;
            return;
        }
    }
}

void GameSound::KillAll() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!m_Slots[i].isFree) {
            if (m_Slots[i].pSound) {
                m_Slots[i].pSound->Stop(0.0f);
            }
            m_Slots[i].isFree   = 1;
            m_Slots[i].nameHash = 0;
        }
    }
}

// Matches 0x00129256
void GameSound::Pause() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        SoundSlot* s = &m_Slots[i];
        if (s->isFree == 0 && s->pSound->IsPlaying()) {
            s->pSound->Pause();
            s->field12 = 1;   // +0x12: paused-by-system flag
        }
    }
}

// Matches 0x00129218
void GameSound::Unpause() {
    SoundManager& mgr = SoundManager::GetInstance();
    if (mgr.IsInterrupted()) {
        m_field04 = 1;  // defer until interruption clears
        return;
    }
    for (int i = 0; i < MAX_SLOTS; i++) {
        SoundSlot* s = &m_Slots[i];
        if (s->field12 != 0) {   // +0x12: was paused-by-system
            s->pSound->Resume();
            s->field12 = 0;
        }
    }
}

// Matches 0x00129380
void GameSound::Update(float /*dt*/) {
    // If paused-for-interruption flag (m_field04) set, check if interruption ended
    if (m_field04 != 0) {
        SoundManager& mgr = SoundManager::GetInstance();
        if (mgr.IsInterrupted()) return;  // still interrupted
        m_field04 = 0;
        Unpause();
    }

    for (int i = 0; i < MAX_SLOTS; i++) {
        SoundSlot* s = &m_Slots[i];
        if (s->isFree != 0) continue;

        if (!s->pSound->IsPlaying() && !s->pSound->IsPaused()) {
            // Sound finished naturally -- mark slot free
            // (binary fires a Delegate1 callback here; port stubs it as no-op)
            DestroySoundInternals(s->pSound);
            s->nameHash = 0;
            s->isFree   = 1;
            continue;
        }

        // Volume update: (1 - (1 - masterVol) * slotVol) * pitch
        if (s->volume > 0.0f) {
            s->pSound->SetVolume(
                (1.0f - (1.0f - m_MasterVolume) * s->volume) * s->pitch
            );
        }
    }
}

// Matches 0x00129170 -- static
void GameSound::DestroySoundInternals(MortarSound* sound) {
    sound->Destroy();
}
