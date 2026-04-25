// Analysed: 2026-04-25T10:30
#include "audio/MortarSound.h"
#include "audio/SoundManager.h"
#include <cstring>
#include <cstdlib>

namespace Mortar {

// 0x0018c6ac
MortarSound::MortarSound()
    : m_Name(nullptr)
    , m_Handle(0)
    , m_State(0)
{
}

// 0x0018c704 -- calls InternalDestroy
MortarSound::~MortarSound() {
    InternalDestroy();
}

// Handle-validity guard used by every method that reads m_Handle.
// Binary pattern: if (m_Handle == 0) m_State = 0;
inline void MortarSound_HandleGuard(MortarSound* s) {
    if (s->m_Handle == 0) s->m_State = 0;
}

// 0x0018c780
// Guard: if m_Handle==0 -> m_State=0 (voice finished or never started).
// Port addition: if m_Handle!=0 but backend says voice no longer active,
// zero the handle so future guard checks detect completion correctly.
// This replaces the MAMAudioController ListenPair completion callback mechanism.
bool MortarSound::IsPlaying() {
    MortarSound_HandleGuard(this);
    if (m_Handle != 0 && m_State == 2) {
        // Verify backend still has this voice active
        SoundManager& mgr = SoundManager::GetInstance();
        if (!mgr.SFXIsActive(m_Handle)) {
            // Voice finished naturally -- zero handle, state becomes idle
            m_Handle = 0;
            m_State  = 0;
        }
    }
    return m_State == 2;
}

// 0x0018c794
bool MortarSound::IsPaused() {
    MortarSound_HandleGuard(this);
    return m_State == 1;
}

// 0x0018c850
// If m_State==0: calls SoundManager::SFXPlay(m_Name, this), if m_Handle!=0: m_State=2
void MortarSound::Play() {
    MortarSound_HandleGuard(this);
    if (m_State == 0) {
        SoundManager& mgr = SoundManager::GetInstance();
        // Passes 'this' so backend stores the new handle into m_Handle
        mgr.SFXPlay(m_Name, this);
        if (m_Handle != 0) {
            m_State = 2;
        }
    }
}

// 0x0018c830
void MortarSound::Pause() {
    MortarSound_HandleGuard(this);
    if (m_State == 2) {
        SoundManager& mgr = SoundManager::GetInstance();
        mgr.SFXPause(m_Handle);
        m_State = 1;
    }
}

// 0x0018c810
void MortarSound::Resume() {
    MortarSound_HandleGuard(this);
    if (m_State == 1) {
        SoundManager& mgr = SoundManager::GetInstance();
        mgr.SFXResume(m_Handle);
        m_State = 2;
    }
}

// 0x0018c7f0
// fadeTime is always 0.0f in all observed calls (DAT_0018c8cc = 0.0f).
// Fade not implemented -- stop is immediate.
void MortarSound::Stop(float /*fadeTime*/) {
    MortarSound_HandleGuard(this);
    if (m_Handle != 0) {
        SoundManager& mgr = SoundManager::GetInstance();
        mgr.SFXStop(m_Handle);
        m_State  = 0;
        m_Handle = 0;
    }
}

// 0x0018c7b4
// SetVolume clamp: (0.0 < vol*255.0f) * (uint8)(int)(vol*255.0f)
// Produces 0 for negative inputs; byte truncation clamps at 255.
// Constant DAT_0018c7ec = 255.0f
void MortarSound::SetVolume(float vol) {
    MortarSound_HandleGuard(this);
    if (m_Handle != 0) {
        float scaled = vol * 255.0f;
        uint8_t byte_vol = static_cast<uint8_t>(
            static_cast<int>((0.0f < scaled) * scaled)
        );
        SoundManager& mgr = SoundManager::GetInstance();
        mgr.SFXSetVolume(m_Handle, byte_vol);
    }
}

// 0x0018c6fc -- non-virtual wrapper
void MortarSound::Destroy() {
    InternalDestroy();
}

// 0x0018c8a4
// Free m_Name, Stop(0), RemoveListener (listener table not maintained in port)
void MortarSound::InternalDestroy() {
    if (m_Name != nullptr) {
        delete[] m_Name;
        m_Name = nullptr;
    }
    Stop(0.0f);
    // MAMAudioController::RemoveListener(m_Handle) -- handle already 0 after Stop,
    // listener cleanup is implicit in port (no listener list maintained)
}

// No binary symbol -- inferred from SFXPlayInternal call context.
// Heap-copies name string into m_Name.
void MortarSound::Load(const char* name) {
    if (m_Name) { delete[] m_Name; m_Name = nullptr; }
    if (name && *name) {
        size_t len = strlen(name) + 1;
        m_Name = new char[len];
        memcpy(m_Name, name, len);
    }
}

} // namespace Mortar
