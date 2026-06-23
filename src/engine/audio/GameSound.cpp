// Analysed: 2026-05-04T08:00
#include "audio/GameSound.h"
#include "debug/Logger.h"
#include <cstring>
#include <cstdio>

using namespace Mortar;


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

GameSound::~GameSound() {
    KillAll();
    // sound objects are owned by SoundManager, not destroyed here
}

// Binary @ 0x001290e8
int GameSound::FindFree() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (m_Slots[i].isFree) return i;
    }
    return -1;
}

// ASM-verified: 2026-05-04T11:00 v1.6.1 binary @ 0x00129270 (asm-inspector)
// DIFFERS: binary @ 0x... calls SoundManager::SFXPlay(name, 0, NULL, 0x40, -1);
//          port simplifies to 2-arg form. Mirror of the marker in MortarSound.cpp::Play.
MortarSound* GameSound::SFXPlay(const char* name, float vol, float pitch,
                                 Mortar::Delegate1<bool, MortarSound*> finishCallback) {
    int i = FindFree();
    if (i == -1) {
        LOG_INFO("SFX", "SFXPlay('%s', vol=%.2f, pitch=%.2f) -- NO FREE SLOT",
                 name ? name : "(null)", vol, pitch);
        return NULL;
    }

    LOG_INFO("SFX", "SFXPlay('%s', vol=%.2f, pitch=%.2f, master=%.2f) slot=%d",
             name ? name : "(null)", vol, pitch, m_MasterVolume, i);

    SoundManager& mgr = SoundManager::GetInstance();
    mgr.SFXPlay(name, m_Slots[i].sound);

    m_Slots[i].isFree         = false;
    m_Slots[i].id             = StringHash(name);
    m_Slots[i].pitch          = pitch;
    m_Slots[i].volume         = vol;
    m_Slots[i].finishCallback = finishCallback;

    float finalVol = (1.0f - (1.0f - m_MasterVolume) * vol) * pitch;
    m_Slots[i].sound->SetVolume(finalVol);

    return m_Slots[i].sound;
}

// 3-arg overload -- no finish-callback.
MortarSound* GameSound::SFXPlay(const char* name, float vol, float pitch) {
    return SFXPlay(name, vol, pitch, Mortar::Delegate1<bool, MortarSound*>());
}

// Binary @ 0x00151aa8
bool GameSound::IsPlaying(int hash) {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!m_Slots[i].isFree && m_Slots[i].id == hash) {
            // Early exit: slot matched but sound is NULL
            if (m_Slots[i].sound == NULL) return false;
            if (m_Slots[i].sound->IsPlaying()) {
                return true;
            }
        }
    }
    return false;
}

bool GameSound::IsPlaying(const char* name) {
    return IsPlaying(StringHash(name));
}

// Binary @ 0x00129138
bool GameSound::IsValid(MortarSound* sound, const char* name) {
    uint32_t hash = StringHash(name);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!m_Slots[i].isFree && m_Slots[i].sound == sound &&
            m_Slots[i].id == hash) {
            return true;
        }
    }
    return false;
}

// Binary @ 0x0012917c
void GameSound::Release(MortarSound* sound, const char* name) {
    uint32_t hash = StringHash(name);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!m_Slots[i].isFree && m_Slots[i].sound == sound &&
            m_Slots[i].id == hash) {
            m_Slots[i].sound->Stop(0.0f);
            m_Slots[i].isFree = true;
            m_Slots[i].id     = 0;
            return;
        }
    }
}

// Binary @ 0x001291e0 -- unconditional clear of all slots.
void GameSound::KillAll() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (m_Slots[i].sound) {
            m_Slots[i].sound->Stop(0.0f);
        }
        m_Slots[i].isFree = true;
        m_Slots[i].id     = 0;
    }
}

// Binary @ 0x00129248
void GameSound::Pause() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot* s = &m_Slots[i];
        if (!s->isFree && s->sound->IsPlaying()) {
            s->sound->Pause();
            s->pausedBySystem = 1;
        }
    }
}

// Binary @ 0x00129218
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

// ASM-verified: 2026-05-04T11:00 v1.6.1 binary @ 0x0012930c (asm-inspector)
void GameSound::Update(float /*dt*/) {
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

// Binary @ 0x00129170 -- static
void GameSound::DestroySoundInternals(MortarSound* sound) {
    sound->Destroy();
}
