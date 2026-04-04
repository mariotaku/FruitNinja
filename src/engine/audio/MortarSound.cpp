#include "audio/MortarSound.h"
#include "audio/SoundManager.h"

namespace Mortar {

MortarSound::MortarSound()
    : m_Name(NULL)
    , m_Handle(0)
    , m_State(0)
{
}

MortarSound::~MortarSound() {
    Stop();
}

void MortarSound::SetVolume(float vol) {
    // Maps 0.0-1.0 to 0-255 (DAT_0018c7ec = 255.0f)
    (void)vol;
    // Backend-specific — handled by SoundManager
}

void MortarSound::Stop() {
    m_State = 0;
    m_Handle = 0;
}

void MortarSound::Pause() {
    if (m_State == 2) {
        m_State = 1;
    }
}

void MortarSound::Play() {
    if (m_State == 0) {
        m_State = 2;
    }
}

} // namespace Mortar
