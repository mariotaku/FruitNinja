// Port specific: Wii ASND audio backend.
//
// Mirrors SoundManagerSDL.cpp / SoundManagerWebAudio.cpp: implements the
// full Mortar::SoundManager class body (the class itself has no per-backend
// subclass -- SDL/WebAudio/Wii each provide their own complete definition of
// every method declared in src/engine/audio/SoundManager.h, selected at
// compile time by which backend .cpp is added to the source list -- see
// src/engine/CMakeLists.txt's SDL-vs-WebAudio branch and
// src/platform/wii/CMakeLists.txt for the Wii branch).
//
// libogc's ASND is a fixed 16-voice PCM mixer -- a direct match for
// SoundManager's voice-table shape (VOICE_COUNT=16, src/engine/audio/
// SoundManager.h), which itself mirrors the original MAMAudioThread voice
// limit. Each SFX Voice slot below maps 1:1 onto an ASND hardware voice
// index (SFXPlay's free-voice scan walks the same [0..14] range
// SoundManager::m_Voices covers). Music is NOT one of these voices -- it
// owns a dedicated ASND voice (15) and is streamed rather than RAM-cached;
// see the "Music streaming" section below.
//
// PCM format: S16, 16kHz mono .wav.pcm assets, staged uncompressed and
// verbatim on Wii (see src/platform/wii/README.md "Assets -- uncompressed").
// No >>4 sample shift -- full scale like SDL/WebAudio (see the DIFFERS
// marker in SoundManagerSDL.cpp LoadSound for the binary rationale).
// Per-voice volume is the raw 0-255 byte handed to ASND as a linear voice
// volume, 1:1 (see SoundManager.h Voice doc -- the binary used the same byte as
// a >5 on/off gate, which the port deliberately does not reproduce).
//
// Endianness: libogc's asndlib DOES have a little-endian mono voice format --
// VOICE_MONO_16BIT_LE -- which consumes 16-bit samples as-is with no DSP-side
// byteswap. Our on-disk .wav.pcm asset is little-endian (produced by the same
// PC-side tooling that stages the SDL/host asset), so this backend uses
// VOICE_MONO_16BIT_LE for every voice format arg and passes sample payloads
// through verbatim -- zero-cost, no runtime PCM byteswap. The 20-byte .wav.pcm
// HEADER (int32 fields) is still stored little-endian on disk and IS
// byteswapped on read via the FN_READ_U32/FN_READ_ARRAY primitives
// (src/engine/util/Endian.h), since this is a big-endian target and the
// header ints must be parsed correctly regardless of voice format.
//
// File access: both SFX and music go through the engine's Mortar::File /
// FileManager / IFileSystem seam (src/engine/asset/File.h) -- the SAME path
// every other asset loader in the port uses (textures, XML, save data). NOT
// a raw fopen() on a hardcoded "sd:/..." string. GameInitialise registers a
// single Mortar::FileSystem_Direct rooted at the staged data dir (see
// src/game/GameInitialise.cpp step 3); FileSystem_Direct is a thin stdio
// wrapper (src/engine/asset/FileSystemPosix.cpp, compiled for Wii since it's
// not _WIN32), so the SAME code transparently reads from wherever libfat (or
// a future optical-drive devoptab) has that root mounted -- sd:/, usb:/, or
// an optical DVD mount -- with zero SoundManagerWii-side path branching.
//
// Only compiled when FRUIT_PLATFORM_WII is set (see
// src/platform/wii/CMakeLists.txt).
#ifdef FRUIT_PLATFORM_WII

#include "audio/SoundManager.h"
#include "audio/MortarSound.h"   // complete type for CreateNewSound's `new MortarSound()`
#include "asset/File.h"
#include "util/StringHash.h"
#include "util/Endian.h"
#include "debug/Logger.h"
#include "platform/wii/SoundManagerWii.h"
#if defined(FN_BLOCK_PRELOAD)
#include "resource/ResBlock.h"
#endif

#include <asndlib.h>
#include <gccore.h>   // DCFlushRange -- DSP-visible buffer requirement, see "DMA buffer rules" below
#include <malloc.h>   // memalign/free -- 32-byte-aligned DMA buffers, see "DMA buffer rules" below

#include <cstring>
#include <string>
#include <set>

// ---------------------------------------------------------------------
// DMA buffer rules (Port specific -- no binary counterpart, libogc/ASND
// hardware constraint):
//
// ASND's DSP reads PCM straight out of main RAM via DMA, bypassing the CPU
// cache entirely. Any buffer handed to ASND_SetVoice/SetInfiniteVoice/
// AddVoice MUST be:
//   1. 32-byte aligned (memalign(32, ...), never new[]/malloc -- those give
//      only 8-byte alignment on this target).
//   2. Flushed from the CPU cache to RAM via DCFlushRange() after the CPU
//      finishes writing sample data into it and BEFORE the first
//      ASND_SetVoice/AddVoice call hands it to the DSP -- otherwise the DSP
//      reads whatever stale bytes happen to be in RAM (a partially-written
//      or leftover-previous-use buffer), which manifests as the SFX/music
//      cutting off early or trailing into garbage.
//   3. Sized to a multiple of 32 bytes -- ASND's DMA reads/writes the
//      buffer in 32-byte blocks and can drop a trailing partial block, so
//      any buffer/length not already a 32-byte multiple must be rounded up
//      (with the pad zero-filled) and the padded byte count passed to ASND,
//      not the raw sample-derived byte count.
//
// FN_ALIGN32() rounds a byte count up to the next 32-byte multiple.
static inline unsigned long FN_ALIGN32(unsigned long bytes) {
    return (bytes + 31UL) & ~31UL;
}

// .wav.pcm header layout (5 x int32, little-endian, 20 bytes) -- identical
// to SoundManagerSDL.cpp's LoadSound. See that file's header comment block
// for the full field breakdown (type/sampleRate/bitDepth/sampleCount/loop).

namespace Mortar {

namespace {

const int MUSIC_ASND_VOICE = 15;   // reserved, never handed to SFX allocation below

// Per-ASND-voice loop info for SFX, read by FinishLoopCallback (audio-
// interrupt context) to arm the infinite tail voice once the non-looping
// intro voice finishes. Indexed by ASND voice index (0..14) -- the callback
// only gets a voice index from ASND, so this table is the sole source of
// loop metadata reachable from interrupt context.
//
// No allocation, no locking: every field is POD, written on the main thread
// before ASND_SetVoice arms the voice, and read once by the callback on
// completion. A benign race (main thread reassigning the slot the same tick
// the old voice's finish callback fires) can at worst arm a loop with the
// new voice's belated-but-matching data, since SFXPlay always repopulates
// this table before (re)arming ASND_SetVoice on that same index.
struct LoopInfo {
    volatile bool     armed;        // true if this voice should loop on finish
    const int16_t*    samples;      // full sample buffer (little-endian, as loaded)
    int                loopStart;   // sample index to resume from
    int                sampleCount; // total samples in buffer
    int                vol;         // 0..255, applied to the infinite tail too
};

LoopInfo s_LoopInfo[16];

// ASND finish callback -- runs in the audio-interrupt context. Kept minimal
// per the ASND contract: no allocation, no locks, no logging, no file I/O.
// ASM-spec: no binary counterpart -- ASND has no loop-point support, so this
// callback is the port-specific mechanism that reproduces the binary's
// MAMAudioThread::FillBuffer rewind-to-loopStart behaviour (see
// SoundManagerSDL.cpp's AudioCallback comment) for RAM-resident SFX, using
// two chained ASND voices: a one-shot intro [0..loopStart) then an infinite
// tail [loopStart..sampleCount). Music uses a different mechanism (streamed
// double-buffer, see MusicStream below) since it is never RAM-resident.
void FinishLoopCallback(int32_t voice) {
    if (voice < 0 || voice >= 16) return;
    LoopInfo& li = s_LoopInfo[voice];
    if (!li.armed || !li.samples) return;
    li.armed = false;

    int tailCount = li.sampleCount - li.loopStart;
    if (tailCount <= 0) return;

    ASND_SetInfiniteVoice(voice, VOICE_MONO_16BIT_LE, 16000, 0,
                           (void*)(li.samples + li.loopStart),
                           tailCount * (int)sizeof(int16_t),
                           li.vol, li.vol);
}

// ---------------------------------------------------------------------
// Music streaming.
//
// Unlike SFX (short, RAM-cached in full), music tracks are streamed in
// small chunks from disk via two ping-pong buffers, so the whole track is
// never resident in RAM. ASND's queueing model (per libogc asndlib):
// ASND_SetVoice(..., callback) arms a voice with buffer A, starts it
// playing immediately, and invokes `callback` from the audio interrupt the
// instant ASND finishes consuming buffer A (i.e. moves on -- to the queued
// buffer if ASND_AddVoice supplied one before A drained, or to silence
// otherwise). ASND_AddVoice queues buffer B to play back-to-back right
// after the current buffer drains, and fails (returns non-SND_OK) if a
// buffer is already queued -- there is no way to queue more than one chunk
// ahead.
//
// Refilling must happen off the audio interrupt (file reads can block on
// SD/USB/DVD), so the interrupt-context callback below does the minimum
// safe thing -- increment a counter, no I/O, no allocation -- and
// AudioStreamPump() (called once per frame from mainWii.cpp's main loop,
// see SoundManagerWii.h) does the real work: whenever the counter shows
// ASND has advanced to a new buffer, refill the now-free slot from disk and
// hand it to ASND_AddVoice for the NEXT hand-off. Each ASND_SetVoice/
// AddVoice call is re-armed with the finish callback so the counter keeps
// advancing indefinitely -- this is what makes the buffer chain self-
// sustaining across the whole (looping) track instead of just the first
// two chunks.
//
// Looping is seamless: at EOF (a short read), RefillChunk seeks back to the
// loop point (loopStart samples past the header if the track has one, else
// sample 0 -- see "always-loop" note on SongPlay) and keeps reading within
// the SAME chunk fill, so a chunk can straddle the loop point without any
// audible gap or dropped callback.
// ---------------------------------------------------------------------

const int kStreamChunkSamples = 4096;   // ~256ms per chunk @ 16kHz -- a few KB, well within a few-hundred-ms double-buffer budget

struct MusicStream {
    Mortar::File* file;          // open handle for the duration of playback
    // 32-byte aligned + kStreamChunkSamples*2 (8192) bytes is already a
    // 32-byte multiple -- see "DMA buffer rules". DCFlushRange after every
    // RefillChunk (below) covers requirement #2; the fixed chunk size
    // covers #3, so no padding is needed for the music buffers.
    int16_t       buf[2][kStreamChunkSamples] __attribute__((aligned(32)));
    int            bufSamples[2];  // valid sample count in each buffer
    int            playingSlot;    // slot ASND is currently consuming (last one hand-off to Set/AddVoice)
    int            queuedSlot;     // slot handed to ASND_AddVoice and not yet consumed, or -1 if none queued
    volatile int   finishCount;    // bumped by the ASND finish callback each time a buffer completes (interrupt context)
    int            consumedCount;  // main-thread mirror of finishCount, advanced as the pump reacts to each completion
    int            dataStart;      // byte offset in file where sample data begins (20-byte header)
    int            loopStartByte;  // byte offset to seek to on EOF (dataStart + loopStart*2, or dataStart if no loop point)
    int            vol;            // 0..255, current ASND volume for the music voice
    bool           streaming;      // true while SongPlay is active (false after SongStop)

    MusicStream() : file(nullptr), playingSlot(0), queuedSlot(-1),
                     finishCount(0), consumedCount(0), dataStart(0),
                     loopStartByte(0), vol(0), streaming(false) {
        bufSamples[0] = bufSamples[1] = 0;
    }
};

MusicStream s_Music;

// ASND finish callback for the music voice -- runs in the audio interrupt.
// Deliberately does nothing but increment a counter: no I/O, no allocation,
// no locking (single-writer-here/single-reader-in-pump on a plain int is
// safe on this single-core-audio-interrupt target). AudioStreamPump()
// polls finishCount vs its own consumedCount to notice completions and do
// the real refill work on the main thread.
void MusicFinishCallback(int32_t /*voice*/) {
    s_Music.finishCount++;
}

// Reads one chunk into buf[slot] verbatim (no byteswap -- VOICE_MONO_16BIT_LE
// consumes little-endian samples as-is; see file-header "Endianness" note).
// Seeks back to the loop point and keeps reading if EOF is hit (seamless loop -- see
// MusicStream comment). Returns the number of samples filled (always
// kStreamChunkSamples on success; the loop-wrap read always tops the buffer
// back up before returning, so short reads should not occur in practice
// short of a truncated/corrupt asset).
//
// Uses File::Size()/GetPosition() to compute exactly how many samples
// remain before EOF rather than issuing a Read() and reacting to failure --
// Mortar::IFile::Read() requires the FULL requested byte count to report
// success (IFile_Direct::Read: `fread(...) == n`), so a failed/short read
// gives no usable partial-byte-count; sizing every read to never cross EOF
// sidesteps that ambiguity entirely and means no sample data is ever lost
// at a loop-point boundary.
int RefillChunk(int slot) {
    if (!s_Music.file) return 0;

    int16_t* dst = s_Music.buf[slot];
    int filled = 0;
    int loopGuard = 0;

    while (filled < kStreamChunkSamples) {
        unsigned long fileSize = s_Music.file->Size();
        unsigned long pos      = s_Music.file->GetPosition();
        long remainingBytes    = (long)fileSize - (long)pos;
        int remainingSamples   = remainingBytes > 0 ? (int)(remainingBytes / (long)sizeof(int16_t)) : 0;

        if (remainingSamples <= 0) {
            // Seamless loop: seek back to the loop point and keep filling
            // the SAME chunk, so a chunk can straddle the loop boundary
            // with no audible gap.
            s_Music.file->Seek(FSEEK_SET, s_Music.loopStartByte);
            if (++loopGuard > 4) break;  // avoid a busy spin on a zero-length/broken asset
            continue;
        }

        int want = kStreamChunkSamples - filled;
        if (want > remainingSamples) want = remainingSamples;
        if (want > kStreamChunkSamples) want = kStreamChunkSamples;

        if (!s_Music.file->Read(dst + filled, (unsigned long)want * sizeof(int16_t))) {
            break;  // unexpected I/O failure -- stop, return what we have so far
        }
        filled += want;
    }

    // Flush the whole chunk buffer (not just `filled` bytes) to RAM before
    // it's handed to ASND -- the buffer is 32-byte aligned and its size is
    // already a 32-byte multiple (kStreamChunkSamples*2 = 8192), so
    // flushing the fixed slot size is simplest and correct; a short/zero
    // fill just means the flushed tail is stale-but-unplayed data past
    // `filled`, which the caller never passes to ASND (see the *_ADD_VOICE
    // call sites, which size the DMA transfer to `filled`, not the slot).
    DCFlushRange(dst, sizeof(s_Music.buf[0]));

    return filled;
}

} // namespace

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
    for (int i = 0; i < VOICE_COUNT; i++) {
        m_Voices[i].id      = 0;
        m_Voices[i].buf     = nullptr;
        m_Voices[i].cursor  = 0;   // repurposed on Wii as the ASND voice index
        m_Voices[i].volume  = 255;
        m_Voices[i].playing = false;
    }
    m_MusicVoice.id      = 0;
    m_MusicVoice.buf     = nullptr;
    m_MusicVoice.cursor  = MUSIC_ASND_VOICE;
    m_MusicVoice.volume  = 255;  // unused for music -- s_MusicVolume drives the ASND volume
    m_MusicVoice.playing = false;
}

SoundManager::~SoundManager() {
    if (m_AudioDevice) {
        if (s_Music.file) {
            delete s_Music.file;
            s_Music.file = nullptr;
        }
        ASND_End();
        m_AudioDevice = 0;
    }
    for (std::map<uint32_t, SoundBuffer*>::iterator it = m_SoundCache.begin(); it != m_SoundCache.end(); ++it) {
        if (it->second) {
            free(it->second->samples);   // memalign'd in LoadSound -- see "DMA buffer rules"
            delete it->second;
        }
    }
    m_SoundCache.clear();
}

// ASND_Init() needs no fat/video and is safe to call from GameInitialise
// (after boot). ASND_Init() returns void and leaves the global mixer PAUSED
// -- ASND_Pause(0) must be called once afterward to start it, or no voice
// ever produces sound. ASND's mixer runs in an audio interrupt with no
// per-frame pump required for SFX; music streaming DOES need a per-frame
// pump (file reads can't happen in the interrupt) -- see AudioStreamPump()
// below, called from mainWii.cpp's main loop.
void SoundManager::Init() {
    if (m_AudioDevice) return;  // already open

    ASND_Init();
    ASND_Pause(0);  // ASND starts paused after Init -- resume the global mixer

    memset(s_LoopInfo, 0, sizeof(s_LoopInfo));
    m_AudioDevice = 1;  // reused as a nonzero-on-success flag, mirrors SDL's device-id semantics

    LOG_INFO("SoundManager", "ASND audio initialised (16kHz mono S16, 16 voices)");
}

// Binary: Mortar::SoundManager::Initialise(this, const char* basePath) @ 0x0010557c.
// DIFFERS: Bada sound-cue path is meaningless on this port; see
// SoundManagerSDL.cpp's Initialise for the identical unimplemented gap.
void SoundManager::Initialise(const char* /*basePath*/) {
    // TODO: implement SoundManager::Initialise.
}

// 0x0018cab8 -- allocates MortarSoundMAM (port: plain MortarSound)
MortarSound* SoundManager::CreateNewSound() {
    return new MortarSound();
}

// 0x0018d2d8 -- stub nop
void SoundManager::PreLoadSound(const char* name) {
    if (!name || !*name) return;
    uint32_t hash = StringHash(name);
    if (m_SoundCache.count(hash)) return;
    SoundBuffer* buf = LoadSound(name);
    if (buf) m_SoundCache[hash] = buf;
}

// 0x0018ce78 -- stub nop
void SoundManager::PreLoadSoundEx(const char* name, bool /*preload*/) {
    PreLoadSound(name);
}

// Load .wav.pcm file fully into a heap buffer via the engine File seam
// (see file-header "File access" note). No >>4 sample shift, matching
// SoundManagerSDL.cpp's LoadSound (see its DIFFERS marker) -- ASND mixes
// voices in its own DSP, which provides the pile-up headroom. PCM sample
// payload is read verbatim (no byteswap) -- VOICE_MONO_16BIT_LE consumes
// little-endian samples directly (see file-header "Endianness" note); only
// the 20-byte header's int32 fields need byteswapping on this big-endian
// target.
// TODO: tune -- feeding full-amplitude samples into up to 15 simultaneous
// SFX ASND voices could clip if many loud SFX overlap; the DSP's own
// per-voice mixing may already prevent this (unverified without hardware/
// Dolphin audio testing). Revisit if clipping is heard once this is
// HLE/Dolphin verified.
SoundBuffer* SoundManager::LoadSound(const char* name) {
    std::string path = std::string("sfx/") + name + ".wav.pcm";

#if defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 1 -- fail-loud instrumentation (log-only; no preload yet,
    // see tmp/wii/loader-blueprint.md section 6/7). Fires once per unique name
    // so a Dolphin run's log enumerates the per-block SFX set without
    // per-frame/per-play spam (LoadSound is the once-per-cache-miss disk read;
    // repeated plays hit m_SoundCache and never reach here).
    {
        static std::set<std::string> s_LoggedNames;
        if (s_LoggedNames.insert(name).second) {
            LOG_INFO("BlockLoad", "[BlockLoad] block=%s loading %s (SFX)",
                     fn::wii::GetCurrentBlockName(), name);
        }
    }
#endif

    Mortar::File f(path.c_str(), /*openMode=*/0, /*systemID=*/0);
    if (!f.Open()) {
        LOG_ERROR("SoundManager", "LoadSound: cannot open '%s'", path.c_str());
        return nullptr;
    }

    uint8_t hdrBytes[20];
    if (!f.Read(hdrBytes, sizeof(hdrBytes))) {
        LOG_ERROR("SoundManager", "LoadSound: short header in '%s'", path.c_str());
        return nullptr;
    }

    // hdr[0]=type(1) hdr[1]=sampleRate(16000) hdr[2]=bitDepth(16)
    // hdr[3]=sampleCount hdr[4]=loopStart (0 = no loop). File is little-
    // endian on disk; FN_READ_U32 byteswaps on this (big-endian) target.
    int sampleCount = (int)FN_READ_U32(hdrBytes + 12);
    int loopStart   = (int)FN_READ_U32(hdrBytes + 16);
    bool loop       = (loopStart != 0);

    if (sampleCount <= 0 || sampleCount > 4 * 1024 * 1024) {
        LOG_ERROR("SoundManager", "LoadSound: bad sampleCount %d in '%s'", sampleCount, path.c_str());
        return nullptr;
    }

    // Port specific: memalign(32)/DCFlushRange/32-byte-multiple padding --
    // see "DMA buffer rules" comment near the top of this file. This
    // buffer is handed to ASND_SetVoice/SetInfiniteVoice by SFXPlay/
    // FinishLoopCallback and must survive as long as it's cached, so it's
    // freed with free() (matching memalign), not delete[].
    unsigned long rawBytes    = (unsigned long)sampleCount * sizeof(int16_t);
    unsigned long alignedBytes = FN_ALIGN32(rawBytes);
    int16_t* raw = (int16_t*)memalign(32, alignedBytes);
    if (!raw) {
        LOG_ERROR("SoundManager", "LoadSound: memalign(%lu) failed for '%s'", alignedBytes, path.c_str());
        return nullptr;
    }
    if (alignedBytes > rawBytes) {
        memset((uint8_t*)raw + rawBytes, 0, alignedBytes - rawBytes);
    }
    if (!f.Read(raw, rawBytes)) {
        LOG_ERROR("SoundManager", "LoadSound: short sample data in '%s'", path.c_str());
        free(raw);
        return nullptr;
    }
    // No byteswap -- VOICE_MONO_16BIT_LE consumes little-endian samples
    // as-is; see file-header "Endianness" note.
    DCFlushRange(raw, alignedBytes);

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

// 0x0018d388/0x0018d39c -- loads buffer if not cached, finds a free ASND
// voice, arms it, assigns a monotonic handle ID.
//
// Voice allocation: SoundManager::Voice::cursor (unused for playback
// position on this backend, since ASND owns the playback cursor in
// hardware) is repurposed to store the ASND voice index the slot owns.
// Music reserves ASND voice 15 (MUSIC_ASND_VOICE) so SFX allocation below
// never steals it -- mirrors SDL's separate m_MusicVoice mixed independently
// of the 16 SFX voices; here SFX get ASND voices [0..14].
uint32_t SoundManager::SFXPlay(const char* name, MortarSound* sound) {
    if (!m_AudioDevice) return 0;
    if (!name || !*name) return 0;

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

    uint32_t newId = m_NextSoundId++;
    if (m_NextSoundId == 0) m_NextSoundId = 1;

    // Find a free SoundManager voice slot. ASND voices [0..14] are reserved
    // for SFX; slot i maps directly onto ASND voice index i (m_MusicVoice,
    // slot 15, is never part of this loop -- see the class comment above).
    //
    // Port specific: a slot's id is never cleared when its one-shot ASND
    // voice finishes naturally (no per-frame SFX reap/tick exists, unlike
    // AudioStreamPump for music) -- SFXStop/SFXPause are the only writers
    // that touch id, and neither fires on natural completion. Without
    // reclaiming finished voices here, every slot fills permanently after
    // VOICE_COUNT distinct SFX have each played once and no SFX could ever
    // play again. Fix: treat a slot as free if either id==0 OR its ASND
    // voice already finished playing (mirrors SFXIsActive's own
    // ASND_StatusVoice check below). This reclaim substitutes for the
    // missing reap only -- it never touches a voice that is still playing.
    Voice* slot = nullptr;
    for (int i = 0; i < VOICE_COUNT; i++) {
        if (m_Voices[i].id == 0) { slot = &m_Voices[i]; break; }
        // A loop-armed voice (intro playing, tail not yet armed) must not be
        // reclaimed here even if the intro's ASND voice briefly reports
        // finished right before FinishLoopCallback re-arms it -- armed is
        // cleared by that callback the instant the tail takes over, so this
        // guard only protects the narrow intro-to-tail handoff window.
        if (!s_LoopInfo[i].armed && ASND_StatusVoice(m_Voices[i].cursor) != SND_WORKING) {
            slot = &m_Voices[i];
            break;
        }
    }
    // ASM-spec v1.6.1 MAMAudioThread::PlayNewSound @0x0022f6c4: all voices
    // busy -> new sound is dropped (SendSoundStoppedCmd), never steals a
    // playing voice; FindFreeVoice @0x0022f330. Load-bearing invariant:
    // GameUpdate's bomb-fuse block holds a raw MortarSound* with no IsValid
    // guard, correct only because a playing voice (the looping fuse) can
    // never be killed by another play.
    if (!slot) return 0;
    int asndVoice = (int)(slot - m_Voices);

    // Fresh voice starts at full volume (byte 255, matches SDL); express the
    // live SFX-category mute as ASND volume 0 -- ASND has no separate mute,
    // and a silenced voice must keep playing to completion, not pause. The
    // per-play level arrives one call later via SFXSetVolume.
    int vol255 = s_SFXMuted ? 0 : 255;

    slot->id      = newId;
    slot->buf     = buf;
    slot->cursor  = asndVoice;
    slot->volume  = 255;
    slot->playing = true;

    s_LoopInfo[asndVoice].armed = false;  // disarm any stale loop-continuation from a prior occupant

    if (!buf->loop) {
        // Length is the 32-byte-aligned allocation size (LoadSound pads +
        // zero-fills the tail) so ASND's DMA never drops a trailing
        // partial block -- see "DMA buffer rules". The zero-sample pad is
        // silent and appended after the real audio, so it cannot cause an
        // audible early cutoff.
        unsigned long lenBytes = FN_ALIGN32((unsigned long)buf->sampleCount * sizeof(int16_t));
        ASND_SetVoice(asndVoice, VOICE_MONO_16BIT_LE, 16000, 0,
                       (void*)buf->samples, (int)lenBytes,
                       vol255, vol255, nullptr);
    } else {
        // Two-stage loop: one-shot intro [0..loopStart), then an infinite
        // tail [loopStart..sampleCount) armed by FinishLoopCallback once the
        // intro completes. See file-header note + FinishLoopCallback comment.
        s_LoopInfo[asndVoice].samples     = buf->samples;
        s_LoopInfo[asndVoice].loopStart   = buf->loopStart;
        s_LoopInfo[asndVoice].sampleCount = buf->sampleCount;
        s_LoopInfo[asndVoice].vol         = vol255;
        s_LoopInfo[asndVoice].armed       = true;

        ASND_SetVoice(asndVoice, VOICE_MONO_16BIT_LE, 16000, 0,
                       (void*)buf->samples, buf->loopStart * (int)sizeof(int16_t),
                       vol255, vol255, FinishLoopCallback);
    }

    if (sound) {
        sound->m_Handle = newId;
        sound->m_State  = 2;
    }

    return newId;
}

// Stop voice by ID (immediate, no fade -- fadeTime is always 0.0f in binary)
void SoundManager::SFXStop(uint32_t handle) {
    if (!m_AudioDevice || handle == 0) return;
    Voice* v = FindVoice(handle);
    if (v) {
        s_LoopInfo[v->cursor].armed = false;
        ASND_StopVoice(v->cursor);
        v->id      = 0;
        v->playing = false;
        v->buf     = nullptr;
        v->cursor  = 0;
    }
}

void SoundManager::SFXPause(uint32_t handle) {
    if (!m_AudioDevice || handle == 0) return;
    Voice* v = FindVoice(handle);
    if (v && v->playing) {
        ASND_PauseVoice(v->cursor, 1);
        v->playing = false;
    }
}

void SoundManager::SFXResume(uint32_t handle) {
    if (!m_AudioDevice || handle == 0) return;
    Voice* v = FindVoice(handle);
    if (v && v->id != 0 && !v->playing) {
        ASND_PauseVoice(v->cursor, 0);
        v->playing = true;
    }
}

// SetVolume: vol is the raw 0-255 byte from MortarSound::SetVolume, handed
// straight to ASND as this voice's linear volume (ASND's 0-255 voice volume is
// the same scale, so the mapping is 1:1).
//
// DIFFERS: original = mute gate, byte > 5 plays at FULL amplitude with samples
// mixed raw (v1.6.1 MAMAudioThread::FillBuffer @0x0022f7f0); port scales by the
// byte instead because reproducing the gate turns every in-game fade into an
// abrupt on/off and forces sounds the game intends at 1-7% to full volume -- a
// limitation of the 2010 mixer rather than a design choice.
//
// A zero-volume ASND voice keeps consuming samples (silent, not paused) -- that
// part IS faithful -- so a silenced voice still completes and its loop tail
// still arms via FinishLoopCallback. The LoopInfo vol is updated too so a tail
// armed AFTER the volume change doesn't come back at the old level.
void SoundManager::SFXSetVolume(uint32_t handle, uint8_t vol) {
    if (!m_AudioDevice || handle == 0) return;
    Voice* v = FindVoice(handle);
    if (v) {
        v->volume = vol;
        int vol255 = s_SFXMuted ? 0 : (int)vol;
        s_LoopInfo[v->cursor].vol = vol255;
        ASND_ChangeVolumeVoice(v->cursor, vol255, vol255);
    }
}

bool SoundManager::SFXIsActive(uint32_t handle) {
    if (!m_AudioDevice || handle == 0) return false;
    Voice* v = FindVoice(handle);
    if (!v || v->id == 0) return false;
    return ASND_StatusVoice(v->cursor) == SND_WORKING;
}

bool SoundManager::SFXIsPaused(uint32_t handle) {
    if (!m_AudioDevice || handle == 0) return false;
    Voice* v = FindVoice(handle);
    return (v != nullptr && v->id != 0 && !v->playing);
}

// Pause all SFX voices -- vtable +0x08
void SoundManager::SFXPauseAll() {
    if (!m_AudioDevice) return;
    for (int i = 0; i < VOICE_COUNT; i++) {
        if (m_Voices[i].id != 0 && m_Voices[i].playing) {
            ASND_PauseVoice(m_Voices[i].cursor, 1);
            m_Voices[i].playing = false;
        }
    }
}

// Unpause all SFX voices -- vtable +0x0c
void SoundManager::SFXUnpauseAll() {
    if (!m_AudioDevice) return;
    for (int i = 0; i < VOICE_COUNT; i++) {
        if (m_Voices[i].id != 0 && !m_Voices[i].playing) {
            ASND_PauseVoice(m_Voices[i].cursor, 0);
            m_Voices[i].playing = true;
        }
    }
}

// Interruption (phone call / OS focus loss on the original; here repurposed
// for e.g. HOME-menu focus loss) -- vtable +0x10/+0x14/+0x18.
void SoundManager::BeginInterruption() {
    m_Interrupted = true;
    ASND_Pause(1);
}

void SoundManager::EndInterruption() {
    m_Interrupted = false;
    ASND_Pause(0);
}

bool SoundManager::IsInterrupted() { return m_Interrupted; }

// Music -- streamed via MusicStream (see the anonymous-namespace section
// above) rather than RAM-cached like SFX; owns dedicated ASND voice
// MUSIC_ASND_VOICE, kept out of the SFX allocator above so it's never
// evicted.
//
// DIFFERS: original = Osp::Media::Player streaming .caf; see
// SoundManagerSDL.cpp's SongPlay comment for the loopStart-from-header
// background. Port-specific addition: music ALWAYS loops on this backend
// (even when the header's loopStart==0, the whole track from sample 0
// loops forever) -- there is no music cue in this game meant to play once
// and stop, so a defensive one-shot-to-silence would be a regression, not
// fidelity.
void SoundManager::SongPlay(const char* name) {
    if (!m_AudioDevice || !name) return;

    std::string lower(name);
    for (size_t i = 0; i < lower.size(); ++i) {
        if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] + ('a' - 'A'));
    }

    if (s_Music.file) {
        delete s_Music.file;
        s_Music.file = nullptr;
    }
    s_Music.streaming = false;

    std::string path = std::string("sfx/") + lower + ".wav.pcm";
    Mortar::File* f = new Mortar::File(path.c_str(), /*openMode=*/0, /*systemID=*/0);
    if (!f->Open()) {
        LOG_WARN("MUSIC", "SongPlay('%s'): asset not found, skipping", lower.c_str());
        delete f;
        return;
    }

    uint8_t hdrBytes[20];
    if (!f->Read(hdrBytes, sizeof(hdrBytes))) {
        LOG_WARN("MUSIC", "SongPlay('%s'): short header, skipping", lower.c_str());
        delete f;
        return;
    }
    int sampleCount = (int)FN_READ_U32(hdrBytes + 12);
    int loopStart   = (int)FN_READ_U32(hdrBytes + 16);
    (void)sampleCount;

#if defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 1 -- fail-loud instrumentation (log-only; no preload yet,
    // see tmp/wii/loader-blueprint.md section 6/7). BGM is streamed (not
    // resident, see file-header "BGM" note in the blueprint), so this logs
    // the stream-open rather than a full-file load; fires once per unique
    // song name so a Dolphin run's log shows which song each block plays.
    {
        static std::set<std::string> s_LoggedSongs;
        if (s_LoggedSongs.insert(lower).second) {
            LOG_INFO("BlockLoad", "[BlockLoad] block=%s loading %s (BGM)",
                     fn::wii::GetCurrentBlockName(), lower.c_str());
        }
    }
#endif

    ASND_StopVoice(MUSIC_ASND_VOICE);

    s_Music.file          = f;
    s_Music.dataStart     = (int)sizeof(hdrBytes);
    // Always-loop: loopStart!=0 loops the tail, loopStart==0 loops the
    // whole track from sample 0 (see function-header DIFFERS note).
    s_Music.loopStartByte = s_Music.dataStart + loopStart * (int)sizeof(int16_t);
    s_Music.playingSlot   = 0;
    s_Music.queuedSlot     = -1;
    s_Music.finishCount    = 0;
    s_Music.consumedCount  = 0;

    int vol255 = s_MusicMuted ? 0 : (int)(s_MusicVolume * 255.0f);
    if (vol255 < 0) vol255 = 0;
    if (vol255 > 255) vol255 = 255;
    s_Music.vol = vol255;

    s_Music.bufSamples[0] = RefillChunk(0);
    if (s_Music.bufSamples[0] <= 0) {
        LOG_WARN("MUSIC", "SongPlay('%s'): empty stream, skipping", lower.c_str());
        delete s_Music.file;
        s_Music.file = nullptr;
        return;
    }

    m_MusicVoice.id      = ++m_NextSoundId;
    m_MusicVoice.buf     = nullptr;   // streamed -- no RAM-resident SoundBuffer
    m_MusicVoice.cursor  = MUSIC_ASND_VOICE;
    m_MusicVoice.volume  = 255;       // unused for music -- s_MusicVolume applied via ASND volume
    m_MusicVoice.playing = true;
    s_Music.streaming    = true;

    // MusicFinishCallback fires each time ASND finishes a buffer -- both
    // the initial buf[0] and every subsequent ASND_AddVoice hand-off keep
    // supplying it as the callback, so AudioStreamPump can keep the chain
    // going indefinitely (see MusicStream comment above).
    ASND_SetVoice(MUSIC_ASND_VOICE, VOICE_MONO_16BIT_LE, 16000, 0,
                   (void*)s_Music.buf[0], s_Music.bufSamples[0] * (int)sizeof(int16_t),
                   vol255, vol255, MusicFinishCallback);

    // Prime the second buffer + queue it immediately so AudioStreamPump()
    // has a full buffer ahead of playback from frame 1.
    s_Music.bufSamples[1] = RefillChunk(1);
    if (ASND_AddVoice(MUSIC_ASND_VOICE, (void*)s_Music.buf[1],
                       s_Music.bufSamples[1] * (int)sizeof(int16_t)) == SND_OK) {
        s_Music.queuedSlot = 1;
    }

    LOG_INFO("MUSIC", "SongPlay('%s') loopStart=%d (streamed)", lower.c_str(), loopStart);
}

void SoundManager::SongStop() {
    if (!m_AudioDevice) return;
    ASND_StopVoice(MUSIC_ASND_VOICE);
    if (s_Music.file) {
        delete s_Music.file;
        s_Music.file = nullptr;
    }
    s_Music.streaming   = false;
    s_Music.queuedSlot  = -1;
    m_MusicVoice.id      = 0;
    m_MusicVoice.playing = false;
    m_MusicVoice.buf     = nullptr;
    m_MusicVoice.cursor  = MUSIC_ASND_VOICE;

    LOG_INFO("MUSIC", "SongStop");
}

void SoundManager::SongPause() {
    if (!m_AudioDevice) return;
    if (m_MusicVoice.id != 0) {
        ASND_PauseVoice(MUSIC_ASND_VOICE, 1);
        m_MusicVoice.playing = false;
    }

    LOG_INFO("MUSIC", "SongPause");
}

void SoundManager::SongResume() {
    if (!m_AudioDevice) return;
    if (m_MusicVoice.id != 0) {
        ASND_PauseVoice(MUSIC_ASND_VOICE, 0);
        m_MusicVoice.playing = true;
    }

    LOG_INFO("MUSIC", "SongResume");
}

// 0x0018c960 -- stub nop
void SoundManager::SongSetMemorySize(int /*size*/) {
    // Defunct in original (base nop @ 0x0018c960) -- stub matches upstream shape.
}

// 0x0018ca78
void SoundManager::SetMusicVolume(float vol) {
    s_MusicVolume = vol;
    SyncMutes();
    if (m_AudioDevice && m_MusicVoice.id != 0) {
        int vol255 = s_MusicMuted ? 0 : (int)(vol * 255.0f);
        if (vol255 < 0) vol255 = 0;
        if (vol255 > 255) vol255 = 255;
        s_Music.vol = vol255;
        ASND_ChangeVolumeVoice(MUSIC_ASND_VOICE, vol255, vol255);
    }
}

// 0x0018ca98
void SoundManager::SetSFXVolume(float vol) {
    s_SFXVolume = vol;
    SyncMutes();
}

// 0x0018c9d4 -- compares per-channel volume against 0.1 threshold. Mirrors
// SoundManagerSDL.cpp's SyncMutes exactly, plus (Wii-specific) pushes the
// resulting mute state down to live ASND voices, since ASND has no separate
// "muted" concept -- silence is expressed as volume 0.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0018c9d4 (asm-inspector)
void SoundManager::SyncMutes() {
    bool wasSFXMuted   = s_SFXMuted;
    bool wasMusicMuted = s_MusicMuted;

    s_SFXMuted   = ((double)s_SFXVolume   < 0.1);
    s_MusicMuted = ((double)s_MusicVolume < 0.1);

    if (!m_AudioDevice) return;

    if (s_SFXMuted != wasSFXMuted) {
        for (int i = 0; i < VOICE_COUNT; i++) {
            if (m_Voices[i].id == 0) continue;
            // volume is the raw byte and IS the ASND volume 1:1 (see SFXSetVolume).
            int vol255 = s_SFXMuted ? 0 : (int)m_Voices[i].volume;
            s_LoopInfo[m_Voices[i].cursor].vol = vol255;
            ASND_ChangeVolumeVoice(m_Voices[i].cursor, vol255, vol255);
        }
    }
    if (s_MusicMuted != wasMusicMuted && m_MusicVoice.id != 0) {
        int vol255 = s_MusicMuted ? 0 : (int)(s_MusicVolume * 255.0f);
        if (vol255 < 0) vol255 = 0;
        if (vol255 > 255) vol255 = 255;
        s_Music.vol = vol255;
        ASND_ChangeVolumeVoice(MUSIC_ASND_VOICE, vol255, vol255);
    }
}

} // namespace Mortar

// ---------------------------------------------------------------------
// fn::wii::AudioStreamPump() -- see SoundManagerWii.h for the full
// rationale (main-thread-only file I/O feeding the ASND music voice).
// ---------------------------------------------------------------------
namespace fn {
namespace wii {

void AudioStreamPump() {
    using namespace Mortar;
    if (!s_Music.streaming || !s_Music.file) return;

    // MusicFinishCallback (audio interrupt) bumps finishCount every time
    // ASND finishes consuming a buffer -- both the very first buf[0] and
    // every later ASND_AddVoice hand-off re-supply the same callback, so
    // finishCount keeps advancing for as long as the voice keeps playing.
    // Drain every completion this pump has not yet reacted to (normally
    // zero or one per frame at a 4096-sample/~256ms chunk size and 60Hz
    // polling, but draining in a loop is robust to an occasional missed
    // frame/hitch).
    while (s_Music.consumedCount != s_Music.finishCount) {
        s_Music.consumedCount++;

        // The buffer that just finished was s_Music.playingSlot; ASND has
        // now moved on to play whatever was queued (s_Music.queuedSlot, if
        // any -- it becomes the new playingSlot) and the OLD playingSlot
        // buffer is free to refill and queue for the hand-off after that.
        int freedSlot = s_Music.playingSlot;
        if (s_Music.queuedSlot >= 0) {
            s_Music.playingSlot = s_Music.queuedSlot;
            s_Music.queuedSlot  = -1;
        }
        // else: nothing was queued in time (a rare underrun) -- ASND has
        // gone silent on this voice; playingSlot is left as-is and the
        // refill below re-queues into freedSlot regardless, re-arming
        // playback on the next AddVoice acceptance.

        s_Music.bufSamples[freedSlot] = RefillChunk(freedSlot);
        if (s_Music.bufSamples[freedSlot] <= 0) continue;  // nothing to feed this tick; try again once more data exists

        int rc = ASND_AddVoice(MUSIC_ASND_VOICE, (void*)s_Music.buf[freedSlot],
                                s_Music.bufSamples[freedSlot] * (int)sizeof(int16_t));
        if (rc == SND_OK) {
            s_Music.queuedSlot = freedSlot;
        }
        // else: a buffer is already queued (queuedSlot was already set by
        // an earlier iteration this same frame, or ASND hasn't drained the
        // prior one yet) -- leave freedSlot's freshly-read data as a ready
        // spare; the next completion will pick it up via the same path.
    }

    // Steady-state top-up: even with no NEW completion this frame, keep the
    // queue full if a previous iteration (or SongPlay's initial prime)
    // couldn't queue immediately.
    if (s_Music.queuedSlot < 0) {
        int nextSlot = s_Music.playingSlot ^ 1;
        int filled = RefillChunk(nextSlot);
        if (filled > 0) {
            s_Music.bufSamples[nextSlot] = filled;
            if (ASND_AddVoice(MUSIC_ASND_VOICE, (void*)s_Music.buf[nextSlot],
                               filled * (int)sizeof(int16_t)) == SND_OK) {
                s_Music.queuedSlot = nextSlot;
            }
        }
    }
}

} // namespace wii
} // namespace fn

#endif // FRUIT_PLATFORM_WII
