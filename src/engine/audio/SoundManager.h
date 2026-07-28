// Analysed: 2026-04-25T10:30
#ifndef MORTAR_SOUND_MANAGER_H
#define MORTAR_SOUND_MANAGER_H

#include "audio/MortarSound.h"
#include "core/Singleton.h"
#include <cstdint>
#include <map>
#include <string>

namespace Mortar {

// Matches original SoundManager (40 bytes)
// Abstract base singleton for sound playback. Concrete impl: SoundManagerMAM.
// +0x00 vtable, +0x04 List<MortarSound*> m_SoundList (36 bytes)
//
// Port: SDL2 backend replaces MAMAudioController + MAMAudioThread + BadaSound.
// Voice table: 16 voices matching MAMAudioThread limit.
// PCM format: S16LE, 16kHz mono (matches MAMAudioThread sampleRate=16000).

// Loaded sound buffer (replaces MAMSound / SoundEffectBada)
struct SoundBuffer {
    int16_t* samples;     // heap-allocated, full scale (no pre-attenuation --
                          // see the DIFFERS marker in SoundManagerSDL.cpp LoadSound)
    int      sampleCount; // total samples (not bytes)
    bool     loop;
    int      loopStart;   // sample to rewind to when looping (0 = file start)
    SoundBuffer() : samples(nullptr), sampleCount(0), loop(false), loopStart(0) {}
};

// 16-voice table (matches MAMAudioThread voice count)
//
// volume is stored as the RAW voice byte (voice+0xf in the binary), exactly as
// MortarSound::SetVolume produced it -- including its truncate-and-wrap
// behaviour above 1.0. Every backend converts it to a linear gain at the point
// it mixes (byte * 1/255), so the byte here always matches what the game wrote
// and stays meaningful to the SFX OSD's `v=` readout.
//
// DIFFERS: original = mute gate, byte > 5 plays at FULL amplitude with samples
// mixed raw (v1.6.1 MAMAudioThread::FillBuffer @0x0022f7f0); port scales by the
// byte instead because reproducing the gate turns every in-game fade into an
// abrupt on/off and forces sounds the game intends at 1-7% to full volume -- a
// limitation of the 2010 mixer rather than a design choice.
//
// A silent (byte 0) voice is NOT paused: its cursor keeps advancing, loops
// wrap, and completion is still detected -- muting never stalls playback. That
// part IS faithful and is load-bearing (GameSound reaps the slot on completion).
struct Voice {
    uint32_t   id;           // monotonic ID; 0 = idle
    SoundBuffer* buf;        // pointer into s_soundCache
    int        cursor;       // current sample position
    uint8_t    volume;       // raw 0-255 volume byte; linear gain = volume/255
    bool       playing;      // true = active; false = paused or idle
    Voice() : id(0), buf(nullptr), cursor(0), volume(255), playing(false) {}
};

static const int VOICE_COUNT = 16;

class SoundManager : public Singleton<SoundManager> {
    friend class Singleton<SoundManager>;

public:
    // Static globals (GOT-relative in binary, not instance fields)
    // s_MusicVolume default 0.45f (DAT_0018b89c), s_SFXVolume default 0.4f (DAT_0018b8a0)
    static float s_SFXVolume;
    static float s_MusicVolume;
    static bool  s_SFXMuted;
    static bool  s_MusicMuted;

    // SoundManager vtable methods (SoundManagerFns, 28 bytes, 7 slots)
    // +0x00 PreLoadSound, +0x04 PreLoadSoundEx, +0x08 SFXPauseAll,
    // +0x0c SFXUnpauseAll, +0x10 BeginInterruption, +0x14 EndInterruption,
    // +0x18 IsInterrupted

    // SDL2 backend initialisation. Must be called after SDL_Init.
    // Opens audio device at 16kHz mono S16LE (matches MAMAudioThread).
    void Init();

    // Binary: Mortar::SoundManager::Initialise(this, const char* basePath)
    //   @ 0x0010557c (PLT thunk).
    // Called from GameInit step 23 @ 0x0016cc6c.
    // basePath in binary = "Sound/Win32Project/Win/FruitNinja" (0x001BC978).
    // DIFFERS: Bada path is meaningless on SDL2; port receives translated path.
    // TODO: implement cue-file scanning from basePath.
    void Initialise(const char* basePath);

    // Allocates MortarSoundMAM (0x10 bytes) -- 0x0018cab8
    virtual MortarSound* CreateNewSound();

    // Stubs (base nop) -- 0x0018d2d8 / 0x0018ce78
    virtual void PreLoadSound(const char* name);
    virtual void PreLoadSoundEx(const char* name, bool preload);

    // SFX playback -- delegates to SFXPlayInternal (0x0018d388 / 0x0018d39c)
    // Returns new monotonic voice handle, or 0 when the sound cannot play
    // (no audio device, load failure, or ALL voices busy -- the binary DROPS
    // the new sound rather than stealing a playing voice; see
    // MAMAudioThread::PlayNewSound @0x0022f6c4 in SoundManagerSDL.cpp).
    // On drop, sound (if non-NULL) is left untouched: m_Handle stays 0 so
    // GameSound::Update reaps the slot on its next poll. Never kills a
    // playing voice -- callers (e.g. the bomb-fuse block in GameInit.cpp)
    // rely on this to hold raw MortarSound* without IsValid guards.
    // If the sound plays and sound != NULL, stores handle into sound->m_Handle.
    virtual uint32_t SFXPlay(const char* name, MortarSound* sound = nullptr);

    // Backend voice commands -- SDL2 backend implements these.
    // All mutate the voice table; must be called with audio device locked.
    virtual void SFXStop(uint32_t handle);
    virtual void SFXPause(uint32_t handle);
    virtual void SFXResume(uint32_t handle);
    // vol is the raw 0-255 byte from MortarSound::SetVolume (truncated
    // vol*255, wraps above 1.0 -- see MortarSound.cpp). Stored verbatim in
    // Voice::volume; each backend applies it as a LINEAR GAIN (vol/255) when
    // it mixes -- see the DIFFERS on Voice above, the binary treated the same
    // byte as a >5 on/off gate. A silent (vol 0) voice keeps playing to
    // completion on every backend rather than pausing.
    virtual void SFXSetVolume(uint32_t handle, uint8_t vol);   // 0-255 volume byte

    // Query whether a voice is still active (not finished, not stopped).
    // Used by MortarSound::IsPlaying/IsPaused to detect voice completion.
    // Replaces MAMAudioController completion callback (ListenPair) zeroing m_Handle.
    virtual bool SFXIsActive(uint32_t handle);
    virtual bool SFXIsPaused(uint32_t handle);

    // Pause/unpause ALL SFX -- v1.6.1 SoundManagerMAM::SFXPauseAll @0x002302d0 /
    // SFXUnpauseAll @0x002302d4 are nops; the live pair is SFXPauseAll @0x00230ec8 /
    // SFXUnpauseAll @0x00230ecc
    virtual void SFXPauseAll();
    virtual void SFXUnpauseAll();

    // Interruption (phone call / OS focus loss) -- vtable +0x10/+0x14/+0x18
    virtual void BeginInterruption();
    virtual void EndInterruption();
    virtual bool IsInterrupted();

    // Music control -- SongPlay is stub (mp3 not implemented); PCM stubs are wired.
    // DIFFERS: SongPlay is a stub (TODO mp3 streaming); original used Osp::Media::Player
    virtual void SongPlay(const char* name);
    virtual void SongStop();
    virtual void SongPause();
    virtual void SongResume();
    virtual void SongSetMemorySize(int size);  // stub nop; v1.6.1 @0x00230650

    // Volume (static globals + SyncMutes)
    float GetMusicVolume() const { return s_MusicVolume; }
    void  SetMusicVolume(float vol);  // v1.6.1 @0x00230534
    void  SetSFXVolume(float vol);    // v1.6.1 @0x002304f4
    // Mirrors the binary's internal-linkage Mortar::SyncMutes @0x002302e4:
    // per channel, muted = (MasterMute || vol < 0.1) -> SetMusicMute / SetSfxMute.
    void  SyncMutes();

    virtual ~SoundManager();

private:
    // SDL2 audio device
    uint32_t m_AudioDevice;            // SDL_AudioDeviceID; 0 = not opened (typedef of uint32_t in SDL2)
    bool              m_Interrupted;   // interruption state

    // Voice table (accessed from audio callback -- always lock device before touching)
    Voice m_Voices[VOICE_COUNT];

    // Monotonic ID counter (matches MAMAudioController::m_NextSoundId)
    uint32_t m_NextSoundId;

    // Sound buffer cache: name -> SoundBuffer (keyed by short name, no path/ext)
    // Maps StringHash of name to SoundBuffer* (matches SoundManagerMAM::GetSound map)
    std::map<uint32_t, SoundBuffer*> m_SoundCache;

    // Music voice -- separate from SFX voices.
    // DIFFERS: music path uses same PCM voice rather than streaming (mp3 TODO)
    Voice m_MusicVoice;

    // Load .wav.pcm file into SoundBuffer, full scale (see the DIFFERS marker
    // in SoundManagerSDL.cpp LoadSound about the binary's fixed >>4).
    // Returns nullptr on failure (logs to stderr).
    SoundBuffer* LoadSound(const char* name);

    // Find a voice by handle. Returns nullptr if not found or idle.
    Voice* FindVoice(uint32_t id);

    // SDL2 audio callback (static)
    static void AudioCallback(void* userdata, uint8_t* stream, int len);

    SoundManager();
};

} // namespace Mortar

#endif
