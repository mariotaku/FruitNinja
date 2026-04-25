// Analysed: 2026-04-25T10:30
//
// SoundManager -- SDL2 audio backend.
//
// Replaces the bottom two layers of the original architecture:
//   MAMAudioController + MAMAudioThread + BadaSound  ->  SDL2 AudioCallback
//
// PCM format: S16LE, 16kHz mono (MAMAudioThread sampleRate = 16000).
// .wav.pcm header: 5 x int32 = 20 bytes (type, sampleRate, bitDepth, sampleCount, loop).
// Sample shift: all samples >>4 after loading (MAMAudioController::LoadSound).
// Voices: 16 entries (MAMAudioThread voice limit).
// Music: TODO -- stub, mp3 streaming not implemented.
// DIFFERS: music is stubbed (no Osp::Media::Player equivalent yet).

#include "audio/SoundManager.h"
#include "util/StringHash.h"
#include "asset/TextureManager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// .wav.pcm header layout (5 x int32, little-endian, 20 bytes)
// Offset 0: type/format field (always 1)
// Offset 4: sample rate (16000 for all SFX)
// Offset 8: bit depth (16)
// Offset 12: sample count (number of 16-bit samples)
// Offset 16: loop flag (0 or 1)

namespace Mortar {

// Static globals -- default volumes match BadaSound constructor
// DAT_0018b89c = 0.45f (music), DAT_0018b8a0 = 0.4f (sfx)
float SoundManager::s_SFXVolume   = 0.4f;
float SoundManager::s_MusicVolume = 0.45f;
bool  SoundManager::s_SFXMuted    = false;
bool  SoundManager::s_MusicMuted  = false;

SoundManager::SoundManager()
    : m_AudioDevice(0)
    , m_Interrupted(false)
    , m_NextSoundId(1)
{
    // Voice table zeroed by default constructors
    memset(m_Voices, 0, sizeof(m_Voices));
    for (int i = 0; i < VOICE_COUNT; i++) {
        m_Voices[i].id      = 0;
        m_Voices[i].buf     = nullptr;
        m_Voices[i].cursor  = 0;
        m_Voices[i].volume  = 1.0f;
        m_Voices[i].playing = false;
    }
    m_MusicVoice.id      = 0;
    m_MusicVoice.buf     = nullptr;
    m_MusicVoice.cursor  = 0;
    m_MusicVoice.volume  = s_MusicVolume;
    m_MusicVoice.playing = false;
}

SoundManager::~SoundManager() {
    if (m_AudioDevice) {
        SDL_CloseAudioDevice(m_AudioDevice);
        m_AudioDevice = 0;
    }
    // Free sound cache
    for (auto& kv : m_SoundCache) {
        if (kv.second) {
            delete[] kv.second->samples;
            delete kv.second;
        }
    }
    m_SoundCache.clear();
}

// SDL2 audio init. Must be called after SDL_Init(SDL_INIT_AUDIO).
void SoundManager::Init() {
    if (m_AudioDevice) return;  // already open

    // Re-init SDL audio subsystem if not already done
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            fprintf(stderr, "[SoundManager] SDL_InitSubSystem(AUDIO) failed: %s\n",
                    SDL_GetError());
            return;
        }
    }

    SDL_AudioSpec want, got;
    SDL_memset(&want, 0, sizeof(want));
    // MAMAudioThread: sampleRate=16000, 16-bit, mono
    want.freq     = 16000;
    want.format   = AUDIO_S16LSB;
    want.channels = 1;
    // Buffer size: ~10ms at 16kHz = 160 samples.
    // Use 256 as next power-of-two (SDL requires power-of-two).
    want.samples  = 256;
    want.callback = SoundManager::AudioCallback;
    want.userdata = this;

    m_AudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!m_AudioDevice) {
        fprintf(stderr, "[SoundManager] SDL_OpenAudioDevice failed: %s\n",
                SDL_GetError());
        return;
    }

    printf("[SoundManager] Audio opened: freq=%d ch=%d fmt=%d samples=%d\n",
           got.freq, got.channels, got.format, got.samples);

    // Start playback
    SDL_PauseAudioDevice(m_AudioDevice, 0);
}

// Load .wav.pcm file into heap buffer, apply >>4 sample shift.
// Returns nullptr on any failure (file not found, bad header, etc.)
SoundBuffer* SoundManager::LoadSound(const char* name) {
    // Build path: DATA_DIR/sfx/<name>.wav.pcm
    // Uses TextureManager::GetDataDir() (set at boot by Game::init) to get base data dir.
    std::string path = std::string(TextureManager::GetDataDir()) + "/sfx/" + name + ".wav.pcm";

    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) {
        fprintf(stderr, "[SoundManager] LoadSound: cannot open '%s': %s\n",
                path.c_str(), SDL_GetError());
        return nullptr;
    }

    // Read 20-byte header (5 x int32 LE)
    int32_t hdr[5];
    if (SDL_RWread(rw, hdr, sizeof(int32_t), 5) != 5) {
        fprintf(stderr, "[SoundManager] LoadSound: short header in '%s'\n", path.c_str());
        SDL_RWclose(rw);
        return nullptr;
    }

    // hdr[0] = type (1), hdr[1] = sampleRate, hdr[2] = bitDepth (16),
    // hdr[3] = sampleCount, hdr[4] = loop flag
    int sampleCount = hdr[3];
    bool loop       = (hdr[4] != 0);

    if (sampleCount <= 0 || sampleCount > 4 * 1024 * 1024) {
        fprintf(stderr, "[SoundManager] LoadSound: bad sampleCount %d in '%s'\n",
                sampleCount, path.c_str());
        SDL_RWclose(rw);
        return nullptr;
    }

    int16_t* raw = new int16_t[sampleCount];
    int read = (int)SDL_RWread(rw, raw, sizeof(int16_t), (size_t)sampleCount);
    SDL_RWclose(rw);

    if (read <= 0) {
        fprintf(stderr, "[SoundManager] LoadSound: no sample data in '%s'\n", path.c_str());
        delete[] raw;
        return nullptr;
    }
    // Clamp if file was shorter than header claimed
    if (read < sampleCount) sampleCount = read;

    // Apply >>4 sample shift (MAMAudioController::LoadSound behaviour)
    for (int i = 0; i < sampleCount; i++) {
        raw[i] = raw[i] >> 4;
    }

    SoundBuffer* buf = new SoundBuffer();
    buf->samples     = raw;
    buf->sampleCount = sampleCount;
    buf->loop        = loop;
    return buf;
}

// Find a voice by monotonic ID. Returns nullptr if not found.
Voice* SoundManager::FindVoice(uint32_t id) {
    if (id == 0) return nullptr;
    for (int i = 0; i < VOICE_COUNT; i++) {
        if (m_Voices[i].id == id) return &m_Voices[i];
    }
    return nullptr;
}

// SDL2 audio callback. Runs on the audio thread.
// Mixes all active voices + music voice into the output buffer.
// Output: S16LE mono 16kHz.
void SoundManager::AudioCallback(void* userdata, uint8_t* stream, int len) {
    SoundManager* self = static_cast<SoundManager*>(userdata);
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    int nSamples = len / sizeof(int16_t);

    // Zero output
    SDL_memset(stream, 0, (size_t)len);

    // Mix each SFX voice
    for (int vi = 0; vi < VOICE_COUNT; vi++) {
        Voice& v = self->m_Voices[vi];
        if (v.id == 0 || !v.playing || !v.buf) continue;

        int16_t* src = v.buf->samples;
        int total    = v.buf->sampleCount;
        float vol    = v.volume * s_SFXVolume;
        if (s_SFXMuted) vol = 0.0f;

        for (int s = 0; s < nSamples; ) {
            if (v.cursor >= total) {
                if (v.buf->loop) {
                    v.cursor = 0;
                } else {
                    // Finished -- mark idle (handle reset lets IsPlaying() go false)
                    v.id      = 0;
                    v.playing = false;
                    break;
                }
            }
            // Mix with clamp
            int32_t mixed = out[s] + (int32_t)(src[v.cursor] * vol);
            if (mixed >  32767) mixed =  32767;
            if (mixed < -32768) mixed = -32768;
            out[s] = (int16_t)mixed;
            v.cursor++;
            s++;
        }
    }

    // Mix music voice
    {
        Voice& mv = self->m_MusicVoice;
        if (mv.id != 0 && mv.playing && mv.buf) {
            int16_t* src = mv.buf->samples;
            int total    = mv.buf->sampleCount;
            float vol    = mv.volume * s_MusicVolume;
            if (s_MusicMuted) vol = 0.0f;

            for (int s = 0; s < nSamples; ) {
                if (mv.cursor >= total) {
                    if (mv.buf->loop) {
                        mv.cursor = 0;
                    } else {
                        mv.id      = 0;
                        mv.playing = false;
                        break;
                    }
                }
                int32_t mixed = out[s] + (int32_t)(src[mv.cursor] * vol);
                if (mixed >  32767) mixed =  32767;
                if (mixed < -32768) mixed = -32768;
                out[s] = (int16_t)mixed;
                mv.cursor++;
                s++;
            }
        }
    }
}

// 0x0018cab8 -- allocates MortarSoundMAM (port: plain MortarSound)
MortarSound* SoundManager::CreateNewSound() {
    return new MortarSound();
}

// 0x0018d2d8 -- stub nop
void SoundManager::PreLoadSound(const char* name) {
    if (!name || !*name) return;
    uint32_t hash = StringHash(name);
    if (m_SoundCache.count(hash)) return;  // already loaded
    SoundBuffer* buf = LoadSound(name);
    if (buf) m_SoundCache[hash] = buf;
}

// 0x0018ce78 -- stub nop
void SoundManager::PreLoadSoundEx(const char* name, bool /*preload*/) {
    PreLoadSound(name);
}

// 0x0018d388 -- loads buffer if not cached, finds free voice, assigns ID.
// If sound != NULL, stores the new handle into sound->m_Handle.
uint32_t SoundManager::SFXPlay(const char* name, MortarSound* sound) {
    if (!m_AudioDevice) {
        // Audio not initialised -- silently ignore (game may call before Init)
        return 0;
    }
    if (!name || !*name) return 0;

    // GetSound: lookup or load
    uint32_t hash = StringHash(name);
    SoundBuffer* buf = nullptr;
    {
        auto it = m_SoundCache.find(hash);
        if (it != m_SoundCache.end()) {
            buf = it->second;
        } else {
            buf = LoadSound(name);
            if (!buf) return 0;
            m_SoundCache[hash] = buf;
        }
    }

    // Assign new monotonic ID (matches MAMAudioController::m_NextSoundId increment)
    uint32_t newId = m_NextSoundId++;
    if (m_NextSoundId == 0) m_NextSoundId = 1;  // skip 0 (idle sentinel)

    // Find a free voice and assign it (SDL_LockAudioDevice guards voice table)
    SDL_LockAudioDevice(m_AudioDevice);
    {
        // Look for idle voice first
        Voice* slot = nullptr;
        for (int i = 0; i < VOICE_COUNT; i++) {
            if (m_Voices[i].id == 0) { slot = &m_Voices[i]; break; }
        }
        if (!slot) {
            // All voices busy -- drop the oldest (voice[0], wrap-around)
            // Matches MAMAudioThread behaviour: oldest voice is evicted
            slot = &m_Voices[0];
        }
        slot->id      = newId;
        slot->buf     = buf;
        slot->cursor  = 0;
        slot->volume  = 1.0f;
        slot->playing = true;
    }
    SDL_UnlockAudioDevice(m_AudioDevice);

    // Store handle into MortarSound if provided (SFXPlayInternal branch)
    if (sound) {
        sound->m_Handle = newId;
        sound->m_State  = 2;  // playing
    }

    return newId;
}

// Stop voice by ID (immediate, no fade -- fadeTime always 0.0f in binary)
void SoundManager::SFXStop(uint32_t handle) {
    if (!m_AudioDevice || handle == 0) return;
    SDL_LockAudioDevice(m_AudioDevice);
    Voice* v = FindVoice(handle);
    if (v) {
        v->id      = 0;
        v->playing = false;
        v->buf     = nullptr;
        v->cursor  = 0;
    }
    SDL_UnlockAudioDevice(m_AudioDevice);
}

// Pause voice (preserve cursor)
void SoundManager::SFXPause(uint32_t handle) {
    if (!m_AudioDevice || handle == 0) return;
    SDL_LockAudioDevice(m_AudioDevice);
    Voice* v = FindVoice(handle);
    if (v && v->playing) {
        v->playing = false;
    }
    SDL_UnlockAudioDevice(m_AudioDevice);
}

// Resume paused voice
void SoundManager::SFXResume(uint32_t handle) {
    if (!m_AudioDevice || handle == 0) return;
    SDL_LockAudioDevice(m_AudioDevice);
    Voice* v = FindVoice(handle);
    if (v && v->id != 0 && !v->playing) {
        v->playing = true;
    }
    SDL_UnlockAudioDevice(m_AudioDevice);
}

// SetVolume: vol is 0-255 byte (from MortarSound::SetVolume clamp)
void SoundManager::SFXSetVolume(uint32_t handle, uint8_t vol) {
    if (!m_AudioDevice || handle == 0) return;
    SDL_LockAudioDevice(m_AudioDevice);
    Voice* v = FindVoice(handle);
    if (v) {
        v->volume = vol / 255.0f;
    }
    SDL_UnlockAudioDevice(m_AudioDevice);
}

// Query active/paused state of a voice by handle (no voice table mutation)
bool SoundManager::SFXIsActive(uint32_t handle) {
    if (!m_AudioDevice || handle == 0) return false;
    SDL_LockAudioDevice(m_AudioDevice);
    Voice* v = FindVoice(handle);
    bool active = (v != nullptr && v->id != 0 && v->playing);
    SDL_UnlockAudioDevice(m_AudioDevice);
    return active;
}

bool SoundManager::SFXIsPaused(uint32_t handle) {
    if (!m_AudioDevice || handle == 0) return false;
    SDL_LockAudioDevice(m_AudioDevice);
    Voice* v = FindVoice(handle);
    bool paused = (v != nullptr && v->id != 0 && !v->playing);
    SDL_UnlockAudioDevice(m_AudioDevice);
    return paused;
}

// Pause all SFX voices -- vtable +0x08
void SoundManager::SFXPauseAll() {
    if (!m_AudioDevice) return;
    SDL_LockAudioDevice(m_AudioDevice);
    for (int i = 0; i < VOICE_COUNT; i++) {
        if (m_Voices[i].id != 0) m_Voices[i].playing = false;
    }
    SDL_UnlockAudioDevice(m_AudioDevice);
}

// Unpause all SFX voices -- vtable +0x0c
void SoundManager::SFXUnpauseAll() {
    if (!m_AudioDevice) return;
    SDL_LockAudioDevice(m_AudioDevice);
    for (int i = 0; i < VOICE_COUNT; i++) {
        if (m_Voices[i].id != 0) m_Voices[i].playing = true;
    }
    SDL_UnlockAudioDevice(m_AudioDevice);
}

// Interruption (phone call / OS focus loss) -- vtable +0x10/+0x14/+0x18
void SoundManager::BeginInterruption() {
    m_Interrupted = true;
    SFXPauseAll();
}

void SoundManager::EndInterruption() {
    m_Interrupted = false;
    SFXUnpauseAll();
}

bool SoundManager::IsInterrupted() { return m_Interrupted; }

// Music control
// DIFFERS: SongPlay is a stub -- original used Osp::Media::Player (mp3 streaming).
// PCM music would need the same .wav.pcm format, but music tracks are likely mp3.
// TODO: implement mp3 streaming via a third-party decoder or SDL2_mixer fallback.
void SoundManager::SongPlay(const char* name) {
    (void)name;
    // TODO: implement music streaming (mp3 decoding not yet implemented)
    // DIFFERS: original = Osp::Media::Player::OpenFile + Play (streaming)
}

void SoundManager::SongStop() {
    if (!m_AudioDevice) return;
    SDL_LockAudioDevice(m_AudioDevice);
    m_MusicVoice.id      = 0;
    m_MusicVoice.playing = false;
    m_MusicVoice.buf     = nullptr;
    m_MusicVoice.cursor  = 0;
    SDL_UnlockAudioDevice(m_AudioDevice);
}

void SoundManager::SongPause() {
    if (!m_AudioDevice) return;
    SDL_LockAudioDevice(m_AudioDevice);
    if (m_MusicVoice.id != 0) m_MusicVoice.playing = false;
    SDL_UnlockAudioDevice(m_AudioDevice);
}

void SoundManager::SongResume() {
    if (!m_AudioDevice) return;
    SDL_LockAudioDevice(m_AudioDevice);
    if (m_MusicVoice.id != 0) m_MusicVoice.playing = true;
    SDL_UnlockAudioDevice(m_AudioDevice);
}

// 0x0018c960 -- stub nop
void SoundManager::SongSetMemorySize(int size) { (void)size; }

// 0x0018ca78
void SoundManager::SetMusicVolume(float vol) {
    s_MusicVolume = vol;
    SyncMutes();
    // Update music voice volume
    if (m_AudioDevice) {
        SDL_LockAudioDevice(m_AudioDevice);
        m_MusicVoice.volume = vol;
        SDL_UnlockAudioDevice(m_AudioDevice);
    }
}

// 0x0018ca98
void SoundManager::SetSFXVolume(float vol) {
    s_SFXVolume = vol;
    SyncMutes();
}

// 0x0018c9d4 -- set music/sfx mute based on volume==0 or muted flag
void SoundManager::SyncMutes() {
    s_SFXMuted   = (s_SFXVolume   <= 0.0f);
    s_MusicMuted = (s_MusicVolume <= 0.0f);
}

} // namespace Mortar
