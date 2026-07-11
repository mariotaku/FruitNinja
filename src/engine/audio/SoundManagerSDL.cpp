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
//
// Port specific (web only): the .wav.pcm assets this file loads are 16kHz on
// disk for desktop, but on the Emscripten build the CMake preload is pointed
// at a build-time-resampled 48kHz copy (tools/web/resample-audio-web.py,
// fn_web_audio_staging target) so the SDL device (opened at 48000 Hz in
// Init(), see below) never needs a per-callback resample. This file's
// loading/mixing code is unaware of the swap -- it just reads whatever is at
// the (unchanged) virtual data path.

#include "audio/SoundManager.h"
#include <SDL.h>            // SDL audio backend (SoundManager is SDL-bound)
#include "util/StringHash.h"
#include "util/PathCI.h"
#include "asset/TextureManager.h"
#include "debug/Logger.h"
#include "debug/DebugFlags.h"
#include "debug/OSD.h"
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
    for (std::map<uint32_t, SoundBuffer*>::iterator it = m_SoundCache.begin(); it != m_SoundCache.end(); ++it) {
        if (it->second) {
            delete[] it->second->samples;
            delete it->second;
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
            LOG_ERROR("SoundManager", "SDL_InitSubSystem(AUDIO) failed: %s",
                      SDL_GetError());
            return;
        }
    }

    SDL_AudioSpec want, got;
    SDL_memset(&want, 0, sizeof(want));
    // MAMAudioThread: sampleRate=16000, 16-bit, mono
    want.format   = AUDIO_S16LSB;
    want.channels = 1;
    // Buffer size: ~10ms at 16kHz = 160 samples.
    // Use 256 as next power-of-two (SDL requires power-of-two).
#if defined(__EMSCRIPTEN__)
    // Port specific: open the device at 48000 Hz -- the common browser
    // AudioContext native rate -- instead of the source 16000 Hz. Every SFX/
    // music .wav.pcm asset is pre-resampled to 48000 Hz ONCE at build time
    // (tools/web/resample-audio-web.py, staged into build/web-audio-staging/
    // and preloaded in place of the real FruitNinjaBada/Data/sfx -- see the
    // fn_web_audio_staging CMake target). With device rate == AudioContext
    // rate == buffer rate, emscripten's SDL2 backend never has to resample
    // a mix-callback buffer, which matters on a slow device (webOS TV
    // Chrome). AudioCallback below is unchanged: it plays buffer samples
    // 1:1 into the device either way.
    want.freq = 48000;

    // Buffer size: ~64ms at 48000 Hz, rounded up to a power of two (SDL
    // requires power-of-two) -> 4096. Emscripten's SDL2 audio backend runs
    // the mix callback on the JS main thread via a ScriptProcessorNode; a
    // short buffer is a tight deadline that a slow device misses under
    // normal frame/GC pressure, causing underruns (crackle/pops). ~64ms
    // gives enough headroom to absorb a missed callback without audible
    // dropout (was a hardcoded 1024 @16kHz = 64ms; 4096 @48kHz is the same
    // 64ms budget at the new rate).
    want.samples = 4096;
#else
    want.freq     = 16000;
    want.samples  = 256;
#endif
    want.callback = SoundManager::AudioCallback;
    want.userdata = this;

    m_AudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!m_AudioDevice) {
        LOG_ERROR("SoundManager", "SDL_OpenAudioDevice failed: %s", SDL_GetError());
        return;
    }

    LOG_INFO("SoundManager", "Audio opened: freq=%d ch=%d fmt=%d samples=%d",
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
        // Case-insensitive POSIX fallback. Binary uses Title-Case literals
        // ("Pause", "Bomb-Fuse", "Game-start") that Bada's sound loader
        // resolves CI; the SDL port matches via Mortar::ResolvePathCI.
        std::string ciPath = Mortar::ResolvePathCI(path.c_str());
        if (!ciPath.empty()) {
            rw = SDL_RWFromFile(ciPath.c_str(), "rb");
            if (rw) path = std::move(ciPath);
        }
    }
    if (!rw) {
        LOG_ERROR("SoundManager", "LoadSound: cannot open '%s': %s",
                  path.c_str(), SDL_GetError());
        return nullptr;
    }

    // Read 20-byte header (5 x int32 LE)
    int32_t hdr[5];
    if (SDL_RWread(rw, hdr, sizeof(int32_t), 5) != 5) {
        LOG_ERROR("SoundManager", "LoadSound: short header in '%s'", path.c_str());
        SDL_RWclose(rw);
        return nullptr;
    }

    // hdr[0] = type (1), hdr[1] = sampleRate (16000), hdr[2] = bitDepth (16),
    // hdr[3] = sampleCount, hdr[4] = loop-start sample offset (0 = no loop).
    // ASM-verified: MAMAudioController::LoadSound v1.6.1 binary @ 0x0018c468.
    // Bomb-Fuse hdr[4] = 12736: skip the 0.8s ignition intro, loop the 5.4s
    // burn tail forever. Matches the binary's MAMAudioThread::FillBuffer
    // rewind-to-loopStart behaviour.
    int sampleCount = hdr[3];
    int loopStart   = hdr[4];
    bool loop       = (loopStart != 0);

    if (sampleCount <= 0 || sampleCount > 4 * 1024 * 1024) {
        LOG_ERROR("SoundManager", "LoadSound: bad sampleCount %d in '%s'",
                  sampleCount, path.c_str());
        SDL_RWclose(rw);
        return nullptr;
    }

    int16_t* raw = new int16_t[sampleCount];
    int read = (int)SDL_RWread(rw, raw, sizeof(int16_t), (size_t)sampleCount);
    SDL_RWclose(rw);

    if (read <= 0) {
        LOG_ERROR("SoundManager", "LoadSound: no sample data in '%s'", path.c_str());
        delete[] raw;
        return nullptr;
    }
    // Clamp if file was shorter than header claimed
    if (read < sampleCount) sampleCount = read;

    // Apply >>4 sample shift (MAMAudioController::LoadSound behaviour).
    // ASM-verified: v1.6.1 @0x0022f46c. Keeps the binary's 16-voice headroom
    // (32767>>4=2047, 16*2047=32752 <= 32767) so summed voices never clip.
    // (Tried >>2 for +12dB louder -- it audibly cracked/clipped in busy play,
    // reverted; the faithful >>4 default is the right level.)
    for (int i = 0; i < sampleCount; i++) {
        raw[i] = raw[i] >> 4;
    }

    SoundBuffer* buf = new SoundBuffer();
    buf->samples     = raw;
    buf->sampleCount = sampleCount;
    buf->loop        = loop;
    buf->loopStart   = loopStart;
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


    // Mix each SFX voice. Matches MAMAudioThread::FillBuffer (0x0018c020):
    // samples were attenuated >>4 in LoadSound to leave headroom for 16
    // simultaneous voices, then mixed RAW (out += src). s_SFXMuted /
    // s_MusicMuted preserve the master mute gating.
    //
    // DIFFERS: port additionally multiplies each voice by v.volume in the
    // mix. The binary's MAMAudioThread::FillBuffer skipped per-voice
    // attenuation -- its Voice struct used byte mute flags
    // (field_0xc/field_0xd) instead, so MortarSound::SetVolume(handle, 0)
    // produced an unconditional silence via that flag, and any non-zero
    // volume restored full-amplitude playback. The port stores volume as
    // a float in Voice::volume; without applying it in the mixer,
    // SFXSetVolume() becomes a no-op and persistent-loop SFX (e.g.
    // Bomb-Fuse, controlled by GameUpdate's SetVolume(0)-on-no-bomb mute
    // pattern at 0x0016c4c8..0x0016c5ca) never actually go silent.
    // Port-side multiply is the lossless equivalent of the binary's
    // boolean mute when volume is 0 or 1; intermediate values produce
    // smoother fades than the binary supported, which is harmless.
    for (int vi = 0; vi < VOICE_COUNT; vi++) {
        Voice& v = self->m_Voices[vi];
        if (v.id == 0 || !v.playing || !v.buf) continue;

        int16_t* src = v.buf->samples;
        int total    = v.buf->sampleCount;
        bool muted   = s_SFXMuted;
        const float voiceVol = v.volume;   // 0.0..1.0; 1.0 = passthrough.

        for (int s = 0; s < nSamples; ) {
            if (v.cursor >= total) {
                if (v.buf->loop) {
                    // Rewind to loopStart, not 0. Matches binary's
                    // MAMAudioThread::FillBuffer @ 0x0018c020.
                    v.cursor = v.buf->loopStart;
                } else {
                    v.id      = 0;
                    v.playing = false;
                    break;
                }
            }
            if (!muted && voiceVol > 0.0f) {
                int32_t scaled = (int32_t)((float)src[v.cursor] * voiceVol);
                int32_t mixed  = out[s] + scaled;
                if (mixed >  32767) mixed =  32767;
                if (mixed < -32768) mixed = -32768;
                out[s] = (int16_t)mixed;
            }
            v.cursor++;
            s++;
        }
    }

    // Mix music voice. Port specific: applies s_MusicVolume the same way the
    // SFX loop above applies per-voice v.volume (float multiply + saturating
    // clamp) so music actually responds to SetMusicVolume(). Previously this
    // branch mixed src[] raw and ignored s_MusicVolume entirely -- SongPlay's
    // existing "global s_MusicVolume scales in callback" comment (see the
    // m_MusicVoice.volume = 1.0f init) documents this as the intended design
    // that was never wired up.
    {
        Voice& mv = self->m_MusicVoice;
        if (mv.id != 0 && mv.playing && mv.buf) {
            int16_t* src = mv.buf->samples;
            int total    = mv.buf->sampleCount;
            bool muted   = s_MusicMuted;
            const float musicVol = s_MusicVolume;   // 0.0..1.0; 1.0 = passthrough.

            for (int s = 0; s < nSamples; ) {
                if (mv.cursor >= total) {
                    if (mv.buf->loop) {
                        mv.cursor = mv.buf->loopStart;
                    } else {
                        mv.id      = 0;
                        mv.playing = false;
                        break;
                    }
                }
                if (!muted && musicVol > 0.0f) {
                    int32_t scaled = (int32_t)((float)src[mv.cursor] * musicVol);
                    int32_t mixed  = out[s] + scaled;
                    if (mixed >  32767) mixed =  32767;
                    if (mixed < -32768) mixed = -32768;
                    out[s] = (int16_t)mixed;
                }
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
        std::map<uint32_t, SoundBuffer*>::iterator it = m_SoundCache.find(hash);
        if (it != m_SoundCache.end()) {
            buf = it->second;
        } else {
            buf = LoadSound(name);
            if (!buf) return 0;
            m_SoundCache[hash] = buf;
        }
    }

    // Port specific: dev-tool SFX readout -- when FN::g_bOsdSfx is ON, toast
    // "[tick] <name>" for every SFX that actually plays. Display-only; the
    // audio path below is never gated. OSD stacks up to 6 toasts (oldest
    // evicted), so several SFX in one frame remain visible in sequence.
    if (FN::g_bOsdSfx) {
        char osd[64];
        snprintf(osd, sizeof(osd), "[%06u] %s", Debug::g_LogTick, name);
        OSD_AddMessage(osd);
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
// DIFFERS: original = Osp::Media::Player::OpenFile + Play streaming .caf files.
// Bada package only ships music-menu.wav.pcm (one PCM track); background.caf
// source is absent. We route the PCM through the existing voice mixer with
// loop=true. Loop point comes from musicdesc.xml (Music-menu loopPoint=66162);
// for now hardcoded for the only known track.
//
// Name mapping: original calls SongPlay("Music-menu") (CamelCase, .caf), but
// the shipped asset is music-menu.wav.pcm (lowercase). Lowercase the name
// before passing to LoadSound. TODO: parse musicdesc.xml for full schema.
void SoundManager::SongPlay(const char* name) {
    if (!m_AudioDevice || !name) return;

    // Lowercase the name so "Music-menu" -> "music-menu" matches the asset.
    std::string lower(name);
    for (size_t i = 0; i < lower.size(); ++i) {
        if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] + ('a' - 'A'));
    }

    SoundBuffer* buf = LoadSound(lower.c_str());
    if (!buf) {
        // background.caf isn't shipped — silent fallthrough so caller-side
        // music attempts don't crash. TODO: log if this becomes noisy.
        return;
    }
    buf->loop = true;
    // DIFFERS: loopPoint from musicdesc.xml not parsed yet. The XML says
    // Music-menu loopPoint=66162 (samples). Hardcode for now.
    if (lower == "music-menu") {
        buf->loopStart = 66162;
        if (buf->loopStart >= buf->sampleCount) buf->loopStart = 0;
    } else {
        buf->loopStart = 0;
    }

    SDL_LockAudioDevice(m_AudioDevice);
    m_MusicVoice.id      = ++m_NextSoundId;
    m_MusicVoice.buf     = buf;
    m_MusicVoice.cursor  = 0;
    m_MusicVoice.volume  = 1.0f;  // global s_MusicVolume scales in callback
    m_MusicVoice.playing = true;
    SDL_UnlockAudioDevice(m_AudioDevice);
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

// Binary: SoundManager::Initialise(this, const char* basePath) @ 0x0010557c (PLT thunk).
// Called from GameInit step 23 @ 0x0016cc6c.
// basePath in binary = "Sound/Win32Project/Win/FruitNinja".
// DIFFERS: Bada sound path is meaningless on SDL2; port receives translated path.
// TODO: implement cue-file scanning.
void SoundManager::Initialise(const char* /*basePath*/) {
    // TODO: implement SoundManager::Initialise.
}

// 0x0018ca98
void SoundManager::SetSFXVolume(float vol) {
    s_SFXVolume = vol;
    SyncMutes();
}

// 0x0018c9d4 -- compares per-channel volume against 0.1 threshold
// (DAT_0018ca48). Binary also OR's a master-mute byte at MortarSoundState+0x4
// (DAT_0018ca54) but that byte has NO writer in FruitNinja (MuteSound has
// no callers); omitted. If MuteSound() is ever ported, OR its result here.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0018c9d4 (asm-inspector)
void SoundManager::SyncMutes() {
    s_SFXMuted   = ((double)s_SFXVolume   < 0.1);
    s_MusicMuted = ((double)s_MusicVolume < 0.1);
}

} // namespace Mortar
