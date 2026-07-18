// Port specific: Wii ASND/AESND audio backend -- SCAFFOLDING ONLY.
//
// Mirrors SoundManagerSDL.cpp / SoundManagerWebAudio.cpp: implements the
// full Mortar::SoundManager class body (the class itself has no per-backend
// subclass -- SDL/WebAudio/Wii each provide their own complete definition of
// every method declared in src/engine/audio/SoundManager.h, selected at
// compile time by which backend .cpp is added to the source list -- see
// src/engine/CMakeLists.txt's SDL-vs-WebAudio branch and
// src/platform/wii/CMakeLists.txt for the Wii branch).
//
// libogc2 provides two audio APIs: the lower-level ASND (simple, fixed
// voice count, closer to what MAMAudioThread's 16-voice model already
// assumes) and AESND (more flexible, used by newer homebrew). ASND is the
// natural first target since SoundManager's voice-table shape
// (VOICE_COUNT=16, src/engine/audio/SoundManager.h) already matches
// MAMAudioThread's original voice limit that ASND-style fixed-voice mixers
// mirror closely.
//
// PCM format plan: same as SoundManagerSDL.cpp -- S16LE, 16kHz mono
// .wav.pcm assets, >>4 sample shift after load (MAMAudioController::LoadSound
// semantics) -- ASND consumes S16LE natively so no format conversion beyond
// what the existing loader already does should be needed, pending real RE
// verification once this backend is implemented for real.
//
// Every method below is a no-op stub returning a safe default -- see
// CLAUDE.md "Defunct features -- stub, never skip" for why the shape (every
// method present, correct signature) is preserved even though nothing here
// is defunct (audio on Wii is very much a live target for a future pass;
// this file just isn't that pass).
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "audio/SoundManager.h"
#include "audio/MortarSound.h"   // complete type for CreateNewSound's `new MortarSound()`
#include "debug/Logger.h"
#include <cstring>

// TODO(wii): #include <asndlib.h> once libogc2 is available.

namespace Mortar {

float SoundManager::s_SFXVolume   = 0.4f;
float SoundManager::s_MusicVolume = 0.45f;
bool  SoundManager::s_SFXMuted    = false;
bool  SoundManager::s_MusicMuted  = false;

SoundManager::SoundManager()
    : m_AudioDevice(0)
    , m_Interrupted(false)
    , m_NextSoundId(1)
{
    memset(m_Voices, 0, sizeof(m_Voices));
    m_MusicVoice.id      = 0;
    m_MusicVoice.buf     = nullptr;
    m_MusicVoice.cursor  = 0;
    m_MusicVoice.volume  = s_MusicVolume;
    m_MusicVoice.playing = false;
}

SoundManager::~SoundManager() {
    // TODO(wii): ASND_End(); free m_SoundCache entries (see
    // SoundManagerSDL.cpp's dtor for the exact cache-teardown loop to mirror).
}

// TODO(wii): ASND_Init() + register the mixer callback. Must be called
// after fatInitDefault() if sound assets are read from SD/USB (they are --
// see FileSystemWii.cpp).
void SoundManager::Init() {
    LOG_INFO("SoundManager", "Wii ASND backend: Init() scaffolding, no audio device opened");
}

void SoundManager::Initialise(const char* /*basePath*/) {
    // TODO(wii): see SoundManagerSDL.cpp's Initialise TODO -- same gap
    // (cue-file scanning from basePath), applies identically here.
}

MortarSound* SoundManager::CreateNewSound() {
    // Return a valid empty MortarSound (m_Handle==0), NOT nullptr -- mirrors
    // SoundManagerSDL::CreateNewSound. GameSound fills every slot from this at
    // ctor time and calls sound->SetVolume/SetPitch UNGUARDED (GameSound.cpp:73);
    // a null here crashes (SetVolume on null this). An empty sound is safe:
    // SetVolume/SetPitch no-op while m_Handle==0. Real ASND playback is TODO(wii)
    // (see task: wire SoundManagerWii ASND) but the shape must be non-null.
    return new MortarSound();
}

void SoundManager::PreLoadSound(const char* /*name*/) {
    // Defunct in original (base nop @ 0x0018d2d8) -- stub matches upstream shape.
}

void SoundManager::PreLoadSoundEx(const char* /*name*/, bool /*preload*/) {
    // Defunct in original (base nop @ 0x0018ce78) -- stub matches upstream shape.
}

uint32_t SoundManager::SFXPlay(const char* /*name*/, MortarSound* /*sound*/) {
    // TODO(wii): ASND_SetVoice/ASND_SetInfiniteVoice + LoadSound-equivalent
    // cache lookup (mirror SoundManagerSDL.cpp's SFXPlayInternal).
    return 0;
}

void SoundManager::SFXStop(uint32_t /*handle*/) {
    // TODO(wii): ASND_StopVoice(voiceIdx) for the voice owning this handle.
}

void SoundManager::SFXPause(uint32_t /*handle*/) {
    // TODO(wii): ASND_PauseVoice(voiceIdx, 1).
}

void SoundManager::SFXResume(uint32_t /*handle*/) {
    // TODO(wii): ASND_PauseVoice(voiceIdx, 0).
}

void SoundManager::SFXSetVolume(uint32_t /*handle*/, uint8_t /*vol*/) {
    // TODO(wii): ASND_ChangeVolumeVoice(voiceIdx, left, right).
}

bool SoundManager::SFXIsActive(uint32_t /*handle*/) {
    // TODO(wii): ASND_StatusVoice(voiceIdx) == SND_WORKING.
    return false;
}

bool SoundManager::SFXIsPaused(uint32_t /*handle*/) {
    return false;
}

void SoundManager::SFXPauseAll() {
    // TODO(wii): loop ASND_PauseVoice(i, 1) over all active voices.
}

void SoundManager::SFXUnpauseAll() {
    // TODO(wii): loop ASND_PauseVoice(i, 0) over all active voices.
}

void SoundManager::BeginInterruption() {
    m_Interrupted = true;
}

void SoundManager::EndInterruption() {
    m_Interrupted = false;
}

bool SoundManager::IsInterrupted() {
    return m_Interrupted;
}

void SoundManager::SongPlay(const char* /*name*/) {
    // DIFFERS: stub, matches SoundManagerSDL.cpp (mp3 streaming not
    // implemented on any backend yet).
}

void SoundManager::SongStop() {
}

void SoundManager::SongPause() {
}

void SoundManager::SongResume() {
}

void SoundManager::SongSetMemorySize(int /*size*/) {
    // Defunct in original (base nop @ 0x0018c960) -- stub matches upstream shape.
}

void SoundManager::SetMusicVolume(float vol) {
    s_MusicVolume = vol;
}

void SoundManager::SetSFXVolume(float vol) {
    s_SFXVolume = vol;
}

void SoundManager::SyncMutes() {
    // TODO(wii): mirror SoundManagerSDL.cpp's SyncMutes (applies
    // s_SFXMuted/s_MusicMuted to live voice volumes).
}

} // namespace Mortar

#endif // FRUIT_PLATFORM_WII
