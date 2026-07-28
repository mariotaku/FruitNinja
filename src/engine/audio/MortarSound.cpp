// Analysed: 2026-04-25T10:30
#include "audio/MortarSound.h"
#include "audio/SoundManager.h"
#include <cstring>
#include <cstdlib>

namespace Mortar {

// v1.6.1 Mortar::MortarSound::MortarSound @0x00230030 (vtable @0x002cfcc0)
MortarSound::MortarSound()
    : m_Name(nullptr)
    , m_Handle(0)
    , m_State(0)
{
}

// v1.6.1 Mortar::MortarSound::~MortarSound @0x0022ffe0 -- calls InternalDestroy
// (deleting dtor @0x00230014)
MortarSound::~MortarSound() {
    InternalDestroy();
}

// Handle-validity guard used by every method that reads m_Handle.
// Binary pattern: if (m_Handle == 0) m_State = 0;
inline void MortarSound_HandleGuard(MortarSound* s) {
    if (s->m_Handle == 0) s->m_State = 0;
}

// v1.6.1 Mortar::MortarSound::IsPlaying @0x0023027c
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

// v1.6.1 Mortar::MortarSound::IsPaused @0x0023029c
bool MortarSound::IsPaused() {
    MortarSound_HandleGuard(this);
    return m_State == 1;
}

// v1.6.1 Mortar::MortarSound::Play @0x0023006c
// Binary body: MAMAudioController::RemoveListener(&m_Handle); the global
// AudioHandleHandle is set to &m_Handle; then SFXPlay(m_Name, 0, 0, '@', -1)
// — volume='@' = 0x40 = 64, flags=-1. m_State = 2 only if the handle is nonzero.
// Port specific: passes 'this' as listener instead of the global-handle write.
// DIFFERS: original = SFXPlay(m_Name, 0, 0, 0x40, -1) (v1.6.1 Mortar::MortarSound::Play
//   @0x0023006c), using the 2-arg port form because the port's SoundManager owns
//   volume/flags defaults; loses default volume 64 and listener-table semantics.
void MortarSound::Play() {
    MortarSound_HandleGuard(this);
    if (m_State == 0) {
        SoundManager& mgr = SoundManager::GetInstance();
        mgr.SFXPlay(m_Name, this);
        if (m_Handle != 0) {
            m_State = 2;
        }
    }
}

// v1.6.1 Mortar::MortarSound::Pause @0x002300f0
void MortarSound::Pause() {
    MortarSound_HandleGuard(this);
    if (m_State == 2) {
        SoundManager& mgr = SoundManager::GetInstance();
        mgr.SFXPause(m_Handle);
        m_State = 1;
    }
}

// v1.6.1 Mortar::MortarSound::Resume @0x00230128
void MortarSound::Resume() {
    MortarSound_HandleGuard(this);
    if (m_State == 1) {
        SoundManager& mgr = SoundManager::GetInstance();
        mgr.SFXResume(m_Handle);
        m_State = 2;
    }
}

// v1.6.1 Mortar::MortarSound::Stop @0x00230160
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

// ASM-verified: 2026-07-26T04:30Z v1.6.1 MortarSound::SetVolume @ 0x00230228 (asm-inspector)
// vol is a GAIN, not an attenuation: `s15 = vol * 255.0f` (pool @0x00230278 = 255.0f),
// `vcvt.u32.f32`, `uxtb`, tail-call MAMAudioController::SetSoundVolume(handle, byte).
// 0.0 -> byte 0 -> silent; 1.0 -> 255 -> full.
//
// The byte conversion TRUNCATES, it does not clamp -- `vcvt.u32.f32` saturates only at
// 32 bits, so vol > 1.0 survives as a value above 255 and `uxtb` keeps its low byte.
// e.g. BonusScreen's drum-roll ramp tops out at vol = 1.166 -> 297 -> byte 41, and
// vol = 1.004 -> 256 -> byte 0. That wrap-around is genuine v1.6.1 behaviour and the
// port reproduces it deliberately -- do NOT "fix" it into a clamp.
// The `(0.0f < scaled) *` term reproduces vcvt.u32's saturation of negatives to 0.
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

// v1.6.1 Mortar::MortarSound::Destroy @0x00230064 -- non-virtual wrapper
void MortarSound::Destroy() {
    InternalDestroy();
}

// v1.6.1 Mortar::MortarSound::InternalDestroy @0x00230190
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

// v1.6.1 Mortar::MortarSound::Load @0x00230068 (single b.w tail-call to InternalLoad)
void MortarSound::Load(const char* name) {
    InternalLoad(name);
}

// ASM-verified: 2026-05-18 v1.6.1 Mortar::MortarSound::InternalLoad @ 0x002301d0 (re-analyst)
// Calling InternalLoad on an actively-playing sound silently stops it
// because InternalDestroy calls Stop(0) and zeros m_Handle before m_Name is replaced.
void MortarSound::InternalLoad(const char* name) {
    if (m_Name != nullptr) InternalDestroy();
    size_t n = strlen(name) + 1;
    m_Name = new char[n];
    memcpy(m_Name, name, n);
}

// ASM-verified: 2026-05-18 v1.6.1 Mortar::MortarSound::IsReady @ 0x002302bc (re-analyst)
// Note: binary is a no-op stub; loads complete synchronously.
bool MortarSound::IsReady() {
    if (m_Handle == 0) m_State = 0;
    return true;
}

// ASM-spec v1.6.1 MortarSound::SetPitch @0x00230218: (float) — body discards the pitch arg.
void MortarSound::SetPitch(float /*pitch*/) {
    if (m_Handle == 0) m_State = 0;
}

} // namespace Mortar
