// Port specific: web Web Audio backend (DIFFERS: browser Ogg decode + float
// mixing replaces the SDL int software mixer).
//
// Compiled INSTEAD of SoundManagerSDL.cpp when EMSCRIPTEN (see
// src/engine/CMakeLists.txt). Implements the SAME Mortar::SoundManager public
// API (audio/SoundManager.h, unchanged) by driving a small JS module,
// window.FNAudio, via EM_JS. No SDL audio device is opened on web.
//
// Architecture (vs. the SDL software mixer this replaces):
//   - One browser AudioContext. masterSfxGain + musicGain -> destination.
//     The browser decodes Ogg/Vorbis and mixes on its OWN audio thread, so
//     there is no per-callback main-thread mix (the SDL emscripten backend's
//     ScriptProcessorNode ran on the JS main thread and crackled under load).
//   - Assets are transcoded to sfx/<name>.ogg at build time by
//     tools/assets/stage-assets.py --web, plus a sfx/sfx-loops.json loop map.
//   - SFX handles stay a monotonic uint32 + JS-side active[] map for API
//     compatibility with MortarSound / GameSound (which are unchanged).
//
// DIFFERS: the SDL loader applies a >>4 (div-16) amplitude shift for 16-voice
// int-mixer headroom. The browser mixes in float, so full-scale Ogg is shipped
// and single-sound loudness is set by MASTER_SFX_GAIN (0.9) on masterSfxGain.
// DIFFERS: no fixed 16-voice cap -- the browser allows unlimited concurrent
// sources; the monotonic-handle + active[] map is kept only for API compat.
//
// Case-folding: game code passes Title-Case names ("Clean-Slice-1", "Pause",
// "Bomb-Fuse"); on-disk assets are lowercase. The desktop path resolves this
// via Mortar::ResolvePathCI; here the JS backend lowercases every name before
// building the FS path and doing map lookups, and the transcode script writes
// lowercase .ogg names + lowercase loop-map keys to match.

#include "audio/SoundManager.h"
#include "asset/TextureManager.h"
#include "debug/Logger.h"
#include "debug/DebugFlags.h"
#include "debug/OSD.h"
#include <emscripten.h>
#include <cstdio>
#include <string>

// MASTER_SFX_GAIN: tunable master gain on masterSfxGain so single-sound web
// loudness ~matches desktop (which used a >>4 int-headroom shift instead).
// Defined in the JS init below (the single place it is used); this constant
// documents the value for C++-side reference. Tune by ear on the TV.
static const double MASTER_SFX_GAIN = 0.9;

// ---------------------------------------------------------------------------
// JS module: window.FNAudio. Defined once by fnaudio_init(); every other EM_JS
// wrapper below just forwards to a window.FNAudio method (guarded so a call
// before init() is a safe no-op).
// ---------------------------------------------------------------------------

EM_JS(void, fnaudio_init, (const char* dataDirPtr, double masterSfxGain), {
    if (window.FNAudio && window.FNAudio.ctx) { return; }
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) { return; }
    var ctx = new AC();
    var masterSfx = ctx.createGain();
    var music = ctx.createGain();
    masterSfx.connect(ctx.destination);
    music.connect(ctx.destination);
    masterSfx.gain.value = masterSfxGain;
    music.gain.value = 0.45;

    var sfxDir = UTF8ToString(dataDirPtr) + '/sfx/';

    var FN = {
        ctx: ctx,
        masterSfx: masterSfx,
        music: music,
        MASTER_SFX_GAIN: masterSfxGain,
        sfxDir: sfxDir,  // '<GetDataDir()>/sfx/'
        buffers: {},     // name -> AudioBuffer
        inflight: {},    // name -> true while decoding
        active: {},      // handle -> sfx entry
        loops: {},       // name -> loopStart (seconds)
        song: null,      // current song entry
        songName: null,  // name of most recent playSong (race guard)
        sfxMuted: false,
        musicMuted: false,
        musicVol: 0.45,

        nm: function(name) { return String(name).toLowerCase(); },

        decode: function(name, cb) {
            name = this.nm(name);
            var self = this;
            if (this.buffers[name]) { if (cb) cb(this.buffers[name]); return; }
            if (this.inflight[name]) { return; }
            var path = this.sfxDir + name + '.ogg';
            var data;
            try { data = FS.readFile(path); } catch (e) { return; }
            this.inflight[name] = true;
            // decodeAudioData detaches its ArrayBuffer; hand it a private copy
            // so the wasm HEAP that FS.readFile viewed is never detached. Also
            // guards against FS.readFile returning a Uint8Array VIEW into a
            // larger backing store (MEMFS often over-allocates) -- slicing by
            // byteOffset/byteLength yields exactly the file's bytes, never the
            // whole heap/backing buffer.
            var ab = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
            var decodePromise = this.ctx.decodeAudioData(ab, function(decoded) {
                self.buffers[name] = decoded;
                delete self.inflight[name];
                if (cb) cb(decoded);
            }, function(err) {
                delete self.inflight[name];
                console.warn('FNAudio decode failed: ' + name, err);
                // Failed/undecoded sound: playSfx/playSong below no-op (no
                // cached buffer) rather than throw. Non-fatal by design.
            });
            // decodeAudioData ALSO returns a Promise (spec, even in callback
            // form) that rejects on the same failure the error callback above
            // already handled. Without a .catch here that promise is left
            // unhandled -- Firefox (and some emscripten shells' global
            // onunhandledrejection hook) surfaces that as a fatal crash
            // overlay ("buffer passed to decodeAudioData contains invalid
            // content...") even though the error callback ran fine. Swallow
            // it; the callback above is the real handler.
            if (decodePromise && typeof decodePromise.catch === 'function') {
                decodePromise.catch(function() {});
            }
        },

        playSfx: function(name, handle, gain) {
            name = this.nm(name);
            var b = this.buffers[name];
            if (!b) { this.decode(name); return; }   // drop this instance
            var self = this;
            var src = this.ctx.createBufferSource();
            src.buffer = b;
            var g = this.ctx.createGain();
            g.gain.value = gain;
            var ls = this.loops[name];
            var looping = (ls !== undefined && ls !== null);
            if (looping) { src.loop = true; src.loopStart = ls; src.loopEnd = b.duration; }
            src.connect(g);
            g.connect(this.masterSfx);
            var entry = {
                src: src, gain: g, buffer: b,
                loop: looping, loopStart: looping ? ls : 0,
                paused: false,
                offsetBase: 0, startTime: this.ctx.currentTime
            };
            this.active[handle] = entry;
            src.onended = function() {
                if (self.active[handle] === entry) delete self.active[handle];
            };
            src.start(0);
        },

        setVolume: function(handle, gain) {
            var e = this.active[handle];
            if (e) e.gain.gain.value = gain;
        },

        stop: function(handle) {
            var e = this.active[handle];
            if (!e) return;
            if (e.src) { try { e.src.onended = null; e.src.stop(0); } catch (x) {} }
            delete this.active[handle];
        },

        pause: function(handle) {
            var e = this.active[handle];
            if (!e || e.paused || !e.src) return;
            var played = this.ctx.currentTime - e.startTime;
            e.offset = e.offsetBase + played;
            try { e.src.onended = null; e.src.stop(0); } catch (x) {}
            e.src = null;
            e.paused = true;
        },

        resume: function(handle) {
            var e = this.active[handle];
            if (!e || !e.paused) return;
            var b = e.buffer;
            if (!b) { delete this.active[handle]; return; }
            var startOffset;
            if (e.loop) {
                startOffset = e.offset % b.duration;
            } else {
                if (e.offset >= b.duration) { delete this.active[handle]; return; }
                startOffset = e.offset;
            }
            var self = this;
            var src = this.ctx.createBufferSource();
            src.buffer = b;
            if (e.loop) { src.loop = true; src.loopStart = e.loopStart; src.loopEnd = b.duration; }
            src.connect(e.gain);
            src.onended = function() {
                if (self.active[handle] === e) delete self.active[handle];
            };
            e.src = src;
            e.offsetBase = e.offset;
            e.startTime = this.ctx.currentTime;
            e.paused = false;
            src.start(0, startOffset);
        },

        isActive: function(handle) {
            var e = this.active[handle];
            return (e && !e.paused) ? 1 : 0;
        },

        isPaused: function(handle) {
            var e = this.active[handle];
            return (e && e.paused) ? 1 : 0;
        },

        pauseAllSfx: function() {
            var keys = Object.keys(this.active);
            for (var i = 0; i < keys.length; ++i) this.pause(keys[i]);
        },

        unpauseAllSfx: function() {
            var keys = Object.keys(this.active);
            for (var i = 0; i < keys.length; ++i) this.resume(keys[i]);
        },

        playSong: function(name, loopStartSec, vol) {
            name = this.nm(name);
            this.songStop();
            if (loopStartSec < 0) { loopStartSec = this.loops[name]; if (loopStartSec === undefined || loopStartSec === null) loopStartSec = 0; }
            this.musicVol = vol;
            this.music.gain.value = this.musicMuted ? 0 : vol;
            var self = this;
            this.songName = name;
            var startSong = function(b) {
                if (self.songName !== name) return;   // superseded by a newer playSong
                var src = self.ctx.createBufferSource();
                src.buffer = b;
                // Match SDL: music always loops. loopStartSec sets the loop point.
                src.loop = true;
                src.loopStart = loopStartSec > 0 ? loopStartSec : 0;
                src.loopEnd = b.duration;
                src.connect(self.music);
                self.song = {
                    src: src, name: name,
                    loopStart: src.loopStart, paused: false,
                    offsetBase: 0, startTime: self.ctx.currentTime
                };
                src.start(0);
            };
            var existing = this.buffers[name];
            if (existing) startSong(existing);
            else this.decode(name, startSong);
        },

        songStop: function() {
            this.songName = null;
            var s = this.song;
            if (!s) return;
            if (s.src) { try { s.src.onended = null; s.src.stop(0); } catch (x) {} }
            this.song = null;
        },

        songPause: function() {
            var s = this.song;
            if (!s || s.paused || !s.src) return;
            var played = this.ctx.currentTime - s.startTime;
            s.offset = s.offsetBase + played;
            try { s.src.stop(0); } catch (x) {}
            s.src = null;
            s.paused = true;
        },

        songResume: function() {
            var s = this.song;
            if (!s || !s.paused) return;
            var b = this.buffers[s.name];
            if (!b) { this.song = null; return; }
            var src = this.ctx.createBufferSource();
            src.buffer = b;
            src.loop = true;
            src.loopStart = s.loopStart;
            src.loopEnd = b.duration;
            src.connect(this.music);
            s.src = src;
            s.offsetBase = s.offset;
            s.startTime = this.ctx.currentTime;
            s.paused = false;
            src.start(0, s.offset % b.duration);
        },

        setMusicVol: function(vol) {
            this.musicVol = vol;
            this.music.gain.value = this.musicMuted ? 0 : vol;
        },

        setSfxMuted: function(muted) {
            this.sfxMuted = !!muted;
            this.masterSfx.gain.value = this.sfxMuted ? 0 : this.MASTER_SFX_GAIN;
        },

        setMusicMuted: function(muted) {
            this.musicMuted = !!muted;
            this.music.gain.value = this.musicMuted ? 0 : this.musicVol;
        },

        // Quit/teardown hard-stop. Stops every SFX source AND the music
        // source (this.stop / this.songStop already do real src.stop(0), not
        // just pause-bookkeeping), then belt-and-suspenders: zero both master
        // gains directly so even a source that somehow outlives the stop
        // calls (e.g. a decode-in-flight source that slips past a race) is
        // silent regardless. Called from mainEmscripten.cpp
        // StopWebAudioAndShutdown before ctx.suspend().
        stopAll: function() {
            var keys = Object.keys(this.active);
            for (var i = 0; i < keys.length; ++i) this.stop(keys[i]);
            this.songStop();
            try { this.masterSfx.gain.value = 0; } catch (x) {}
            try { this.music.gain.value = 0; } catch (x) {}
        }
    };

    // NOTE: must be assigned onto window (not a module-local var) -- the
    // teardown/resume EM_ASM blocks in src/mainEmscripten.cpp reference
    // window.FNAudio from a SEPARATE EM_ASM invocation (a different JS
    // closure), so a bare `var FN` here would be invisible to them.
    window.FNAudio = FN;

    // Load the build-time loop metadata (name -> loopStart seconds).
    try {
        var txt = FS.readFile(sfxDir + 'sfx-loops.json', { encoding: 'utf8' });
        FN.loops = JSON.parse(txt);
    } catch (e) {}
});

EM_JS(void, fnaudio_decode, (const char* namePtr), {
    if (window.FNAudio) window.FNAudio.decode(UTF8ToString(namePtr));
});

EM_JS(void, fnaudio_play_sfx, (const char* namePtr, unsigned handle, double gain), {
    if (window.FNAudio) window.FNAudio.playSfx(UTF8ToString(namePtr), handle, gain);
});

EM_JS(void, fnaudio_set_volume, (unsigned handle, double gain), {
    if (window.FNAudio) window.FNAudio.setVolume(handle, gain);
});

EM_JS(void, fnaudio_stop, (unsigned handle), {
    if (window.FNAudio) window.FNAudio.stop(handle);
});

EM_JS(void, fnaudio_pause, (unsigned handle), {
    if (window.FNAudio) window.FNAudio.pause(handle);
});

EM_JS(void, fnaudio_resume, (unsigned handle), {
    if (window.FNAudio) window.FNAudio.resume(handle);
});

EM_JS(int, fnaudio_is_active, (unsigned handle), {
    return window.FNAudio ? window.FNAudio.isActive(handle) : 0;
});

EM_JS(int, fnaudio_is_paused, (unsigned handle), {
    return window.FNAudio ? window.FNAudio.isPaused(handle) : 0;
});

EM_JS(void, fnaudio_pause_all_sfx, (void), {
    if (window.FNAudio) window.FNAudio.pauseAllSfx();
});

EM_JS(void, fnaudio_unpause_all_sfx, (void), {
    if (window.FNAudio) window.FNAudio.unpauseAllSfx();
});

EM_JS(void, fnaudio_play_song, (const char* namePtr, double loopStartSec, double vol), {
    if (window.FNAudio) window.FNAudio.playSong(UTF8ToString(namePtr), loopStartSec, vol);
});

EM_JS(void, fnaudio_song_stop, (void), {
    if (window.FNAudio) window.FNAudio.songStop();
});

EM_JS(void, fnaudio_song_pause, (void), {
    if (window.FNAudio) window.FNAudio.songPause();
});

EM_JS(void, fnaudio_song_resume, (void), {
    if (window.FNAudio) window.FNAudio.songResume();
});

EM_JS(void, fnaudio_set_music_vol, (double vol), {
    if (window.FNAudio) window.FNAudio.setMusicVol(vol);
});

EM_JS(void, fnaudio_set_sfx_muted, (int muted), {
    if (window.FNAudio) window.FNAudio.setSfxMuted(muted);
});

EM_JS(void, fnaudio_set_music_muted, (int muted), {
    if (window.FNAudio) window.FNAudio.setMusicMuted(muted);
});

namespace Mortar {

// Static globals -- default volumes match BadaSound constructor (same as SDL).
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
    // Voice table / cache are unused on web (the browser owns buffers + mixing);
    // they exist only so the shared SoundManager.h layout compiles. Handles are
    // still monotonic (m_NextSoundId) for MortarSound/GameSound API compat.
}

SoundManager::~SoundManager() {
    // Process-lifetime singleton; browser tears the AudioContext down on unload.
}

// FNAudio.init() -- no SDL_OpenAudioDevice on web. The data dir
// (TextureManager::GetDataDir(), e.g. "/FruitNinjaBada/Data") is the MEMFS base
// the backend reads sfx/<name>.ogg + sfx/sfx-loops.json from.
void SoundManager::Init() {
    std::string dataDir = TextureManager::GetDataDir();
    fnaudio_init(dataDir.c_str(), MASTER_SFX_GAIN);
    LOG_INFO("SoundManager", "Web Audio backend initialised (Ogg/Vorbis, no SDL device)");
}

// Cue-file scanning is a no-op on both backends.
void SoundManager::Initialise(const char* /*basePath*/) {
}

// 0x0018cab8 -- allocates MortarSoundMAM (port: plain MortarSound)
MortarSound* SoundManager::CreateNewSound() {
    return new MortarSound();
}

// PreLoad = decode hook. PreloadSounds.cpp's list decodes common sfx during
// the loading screen so they are ready before first play.
void SoundManager::PreLoadSound(const char* name) {
    if (!name || !*name) return;
    fnaudio_decode(name);
}

void SoundManager::PreLoadSoundEx(const char* name, bool /*preload*/) {
    PreLoadSound(name);
}

// Assign monotonic handle, kick a JS source. Volume arrives right after via
// SFXSetVolume (GameSound::SFXPlay computes the final per-play gain).
uint32_t SoundManager::SFXPlay(const char* name, MortarSound* sound) {
    if (!name || !*name) return 0;

    // Port specific: dev-tool SFX readout (?osdsfx / F4), display-only.
    if (FN::g_bOsdSfx) {
        char osd[64];
        snprintf(osd, sizeof(osd), "[%06u] %s", Debug::g_LogTick, name);
        OSD_AddMessage(osd);
    }

    uint32_t newId = m_NextSoundId++;
    if (m_NextSoundId == 0) m_NextSoundId = 1;   // skip 0 (idle sentinel)

    fnaudio_play_sfx(name, newId, 1.0);

    if (sound) {
        sound->m_Handle = newId;
        sound->m_State  = 2;   // playing
    }
    return newId;
}

void SoundManager::SFXStop(uint32_t handle) {
    if (handle == 0) return;
    fnaudio_stop(handle);
}

void SoundManager::SFXPause(uint32_t handle) {
    if (handle == 0) return;
    fnaudio_pause(handle);
}

void SoundManager::SFXResume(uint32_t handle) {
    if (handle == 0) return;
    fnaudio_resume(handle);
}

// vol is 0-255 byte (from MortarSound::SetVolume clamp) -> 0.0..1.0 gain.
void SoundManager::SFXSetVolume(uint32_t handle, uint8_t vol) {
    if (handle == 0) return;
    fnaudio_set_volume(handle, vol / 255.0);
}

bool SoundManager::SFXIsActive(uint32_t handle) {
    if (handle == 0) return false;
    return fnaudio_is_active(handle) != 0;
}

bool SoundManager::SFXIsPaused(uint32_t handle) {
    if (handle == 0) return false;
    return fnaudio_is_paused(handle) != 0;
}

void SoundManager::SFXPauseAll() {
    fnaudio_pause_all_sfx();
}

void SoundManager::SFXUnpauseAll() {
    fnaudio_unpause_all_sfx();
}

void SoundManager::BeginInterruption() {
    m_Interrupted = true;
    SFXPauseAll();
}

void SoundManager::EndInterruption() {
    m_Interrupted = false;
    SFXUnpauseAll();
}

bool SoundManager::IsInterrupted() { return m_Interrupted; }

// Music. Lowercase the name (asset is lowercase); loop point from the JS loop
// map, except music-menu which keeps the binary's hardcoded loopStart=66162
// (musicdesc.xml) at the 16000 Hz asset rate. Music always loops (matches SDL).
void SoundManager::SongPlay(const char* name) {
    if (!name) return;

    std::string lower(name);
    for (size_t i = 0; i < lower.size(); ++i) {
        if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] + ('a' - 'A'));
    }

    double loopStartSec;
    if (lower == "music-menu") {
        loopStartSec = 66162.0 / 16000.0;
    } else {
        loopStartSec = -1.0;   // JS resolves from the loop map (or 0)
    }

    fnaudio_play_song(lower.c_str(), loopStartSec, s_MusicVolume);

    LOG_INFO("MUSIC", "SongPlay('%s') loopStart=%f", lower.c_str(), loopStartSec);

    // Port specific: dev-tool music readout -- mirrors the SFX toast in SFXPlay.
    if (FN::g_bOsdSfx) {
        char osd[64];
        snprintf(osd, sizeof(osd), "[music] %s", lower.c_str());
        OSD_AddMessage(osd);
    }
}

void SoundManager::SongStop()   { fnaudio_song_stop(); LOG_INFO("MUSIC", "SongStop"); }
void SoundManager::SongPause()  { fnaudio_song_pause(); LOG_INFO("MUSIC", "SongPause"); }
void SoundManager::SongResume() { fnaudio_song_resume(); LOG_INFO("MUSIC", "SongResume"); }

// 0x0018c960 -- stub nop
void SoundManager::SongSetMemorySize(int size) { (void)size; }

// 0x0018ca78
void SoundManager::SetMusicVolume(float vol) {
    s_MusicVolume = vol;
    SyncMutes();
    fnaudio_set_music_vol(s_MusicVolume);
}

// 0x0018ca98
void SoundManager::SetSFXVolume(float vol) {
    s_SFXVolume = vol;
    SyncMutes();
}

// 0x0018c9d4 -- per-channel volume vs 0.1 threshold (DAT_0018ca48), then push
// the resulting mute state to the JS gain gates.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x0018c9d4 (asm-inspector)
void SoundManager::SyncMutes() {
    s_SFXMuted   = ((double)s_SFXVolume   < 0.1);
    s_MusicMuted = ((double)s_MusicVolume < 0.1);
    fnaudio_set_sfx_muted(s_SFXMuted ? 1 : 0);
    fnaudio_set_music_muted(s_MusicMuted ? 1 : 0);
}

} // namespace Mortar
