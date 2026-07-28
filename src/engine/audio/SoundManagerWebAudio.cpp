// Web Audio backend: browser Ogg decode + float mixing replaces the SDL int
// software mixer. Platform-only file (no binary counterpart, not asm-compared).
//
// Compiled INSTEAD of SoundManagerSDL.cpp when EMSCRIPTEN (see
// src/engine/CMakeLists.txt). Implements the SAME Mortar::SoundManager public
// API (audio/SoundManager.h, unchanged) by driving a small JS module,
// window.FNAudio, via EM_JS. No SDL audio device is opened on web.
//
// Architecture (vs. the SDL software mixer this replaces):
//   - One browser AudioContext. masterSfxGain + musicGain -> limiter (a
//     DynamicsCompressorNode configured as a brick-wall limiter, parity with
//     SoundManagerSDL's soft-knee limiter) -> destination. The browser
//     decodes Ogg/Vorbis and mixes on its OWN audio thread, so there is no
//     per-callback main-thread mix (the SDL emscripten backend's
//     ScriptProcessorNode ran on the JS main thread and crackled under load).
//   - Assets are transcoded to sfx/<name>.ogg at build time by
//     tools/assets/stage-assets.py --web. Loop points come from the
//     build-generated C++ table linked into the wasm (SfxLoopStartSeconds,
//     see audio/SfxLoopTable.h) -- same generated TU the SDL/webOS backend
//     uses (SfxLoopStartSamples), so a missing table is a LINK error, never
//     a silent loop degradation at runtime.
//   - SFX handles stay a monotonic uint32 + JS-side active[] map for API
//     compatibility with MortarSound / GameSound (which are unchanged).
//   - A play for a not-yet-decoded name is queued on its async decode
//     (pendingSfx) and starts on completion instead of being dropped; the
//     handle stays live meanwhile (stop/pause cancel it, setVolume retargets
//     it, isActive reports it).
//   - Decodes are THROTTLED (DECODE_CONCURRENCY) and a failed decode is
//     RETRIED (MAX_DECODE_ATTEMPTS). PreloadSounds hands this backend ~40-70
//     PreLoadSound calls in one boot burst; passing all of them straight to
//     decodeAudioData made weak browsers (webOS TV) fail the tail of the burst
//     outright, and the old permanent failed[] mark then dropped every play of
//     those names for the rest of the session. See pumpDecodes/onDecodeError.
//     Only after the retry budget is spent is a name marked permanently failed,
//     and that lands in failLog for the C++ side to re-emit via LOG_ERROR
//     (console.warn alone is invisible on a TV).
//
// Both backends play SFX/music at full scale (unity gain). Bada's own SFX path
// (MAMAudioController::LoadSound @0x0022f46c) applied a >>4 shift, but that was
// only its internal 16-voice mix headroom -- the real output stage was
// Osp::Media::AudioOut::SetVolume (device media-volume slider), which the port
// has no equivalent of, so any pre-attenuation just plays quieter. The SDL
// loader plays full-scale (kSfxHeadroomShift=0), the .ogg here is transcoded
// full-scale from the same .wav.pcm, so MASTER_SFX_GAIN = 1.0 to match.
// No fixed 16-voice cap -- the browser allows unlimited concurrent
// sources; the monotonic-handle + active[] map is kept only for API compat.
//
// Music (sfx/music-*.ogg, sfx/background.ogg) is transcoded by the SAME
// stage-assets.py encode_ogg path as SFX (stage_tree treats every
// sfx/*.wav.pcm identically, is_sfx_dir has no music/sfx split) -- full-scale.
// SDL music shares the same full-scale load path, so MASTER_MUSIC_GAIN = 1.0
// too, symmetric to MASTER_SFX_GAIN.
//
// Case-folding: game code passes Title-Case names ("Clean-Slice-1", "Pause",
// "Bomb-Fuse"); on-disk assets are lowercase. The desktop path resolves this
// via Mortar::ResolvePathCI; here the JS backend lowercases every name before
// building the FS path and doing map lookups, the C++ side lowercases before
// the SfxLoopTable lookup (its keys are strictly lowercase), and the
// transcode script writes lowercase .ogg names + table keys to match.

#include "audio/SoundManager.h"
#include "audio/SfxLoopTable.h"
#include "asset/TextureManager.h"
#include "debug/Logger.h"
#include "debug/DebugFlags.h"
#include "debug/OSD.h"
#include <emscripten.h>
#include <cstdio>
#include <string>

// MASTER_SFX_GAIN: unity (full scale). The web .ogg is transcoded full-scale
// from the .wav.pcm and the SDL loader plays full-scale too (kSfxHeadroomShift
// =0), so no make-up gain is needed here. Defined in the JS init below (the
// single place it is used); this constant documents the value for C++ side.
static const double MASTER_SFX_GAIN = 1.0;

// MASTER_MUSIC_GAIN: unity, matching MASTER_SFX_GAIN and the SDL full-scale
// load path (music shares LoadSound).
static const double MASTER_MUSIC_GAIN = 1.0;

// ---------------------------------------------------------------------------
// JS module: window.FNAudio. Defined once by fnaudio_init(); every other EM_JS
// wrapper below just forwards to a window.FNAudio method (guarded so a call
// before init() is a safe no-op).
// ---------------------------------------------------------------------------

EM_JS(void, fnaudio_init, (const char* dataDirPtr, double masterSfxGain, double masterMusicGain), {
    if (window.FNAudio && window.FNAudio.ctx) { return; }
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) { return; }
    var ctx = new AC();
    // Port specific: audio-consent overlay -- capture the AudioContext's
    // BORN state right here, once, synchronously. This is the reliable
    // signal for "does this load already have audio permission" (browsers
    // create the context 'suspended' when a gesture is required, and
    // 'running' when one isn't -- PWA/home-screen launch, a prior sticky
    // activation carried over a navigation, a site-level autoplay grant,
    // etc.). Deliberately NOT a ctx.resume()-then-poll-state check: calling
    // resume() here would be an unrequested/non-gesture resume attempt
    // racing against whatever the caller (mainEmscripten.cpp BootWait's
    // _fnMaybeShowAudioConsent) does next, and polling ctx.state after an
    // async resume() settles is exactly the racy approach this replaces.
    // The born state needs no async wait at all -- read once, right after
    // construction, and never touched again.
    var bornSuspended = (ctx.state === 'suspended');
    var masterSfx = ctx.createGain();
    var music = ctx.createGain();

    // Brick-wall limiter: WebAudio sums every playing SFX/music node in
    // float on the AudioContext's own graph, so stacked SFX can push the
    // summed signal past |1.0| (0dBFS) -- the browser's device output then
    // hard-clips. DynamicsCompressorNode is the browser-native way to tame
    // that: with a hard knee and a high ratio it behaves as a limiter rather
    // than a musical compressor.
    //
    // threshold is 0dB, i.e. exactly full scale -- NOT below it. The SFX
    // assets are mastered to 0dBFS, so a threshold under full scale catches
    // every LONE sound and ducks it by ~1dB with a 3ms attack, which is
    // audible pumping on literally every slice. At 0dB a single sound sits at
    // the threshold and passes essentially untouched, and only a genuine SUM
    // over full scale is limited -- the same "singles pass bit-exact, only
    // stacks are reduced" contract as the SDL backend's peak limiter
    // (SoundManagerSDL.cpp, kLimiterCeiling). Both master buses feed this ONE
    // shared limiter before the destination, so SFX-vs-music stacking is
    // caught too.
    var limiter = ctx.createDynamicsCompressor();
    limiter.threshold.value = 0.0;   // dB, full scale -- safety net, not a bus compressor
    limiter.knee.value = 0.0;        // hard knee -> brick-wall
    limiter.ratio.value = 20.0;      // limiting (not gentle compression)
    limiter.attack.value = 0.002;    // fast, catch stacked transients
    limiter.release.value = 0.15;
    limiter.connect(ctx.destination);

    masterSfx.connect(limiter);
    music.connect(limiter);
    masterSfx.gain.value = masterSfxGain;
    music.gain.value = 0.45 * masterMusicGain;

    var sfxDir = UTF8ToString(dataDirPtr) + '/sfx/';

    var FN = {
        ctx: ctx,
        bornSuspended: bornSuspended,  // audio-consent overlay show/skip signal, see capture point above
        masterSfx: masterSfx,
        music: music,
        MASTER_SFX_GAIN: masterSfxGain,
        MASTER_MUSIC_GAIN: masterMusicGain,
        sfxDir: sfxDir,  // '<GetDataDir()>/sfx/'
        buffers: {},        // name -> AudioBuffer
        inflight: {},       // name -> true while queued / decoding / retry-scheduled
        pendingCb: {},      // name -> [cb] queued on an in-flight decode
        pendingSfx: {},     // handle -> {name, gain}: play waiting on a decode
        failed: {},         // name -> true once PERMANENTLY undecodable (budget spent)
        failCount: {},      // name -> decodeAudioData failures so far
        retryScheduled: {}, // name -> true while a retry timer is pending
        failLog: [],        // "name: reason" lines drained by the C++ side -> LOG_ERROR
        decodeQueue: [],    // names waiting for a decode slot (throttle)
        decodeRunning: 0,   // decodeAudioData calls currently outstanding
        active: {},         // handle -> sfx entry
        song: null,      // current song entry
        songName: null,  // name of most recent playSong (race guard)
        sfxMuted: false,
        musicMuted: false,
        musicVol: 0.45,

        // Decode scheduling knobs. See pumpDecodes / onDecodeError.
        MAX_DECODE_ATTEMPTS: 3,   // decodeAudioData tries per name before giving up
        DECODE_CONCURRENCY: 3,    // decodeAudioData calls allowed in flight at once
        RETRY_DELAY_MS: 200,      // backoff base; attempt N waits N * this

        nm: function(name) { return String(name).toLowerCase(); },

        // decode(name, cb, lowPriority): ensure <name> is decoded, then
        // cb(buffer) -- or cb(null) once the name is PERMANENTLY undecodable
        // (file absent, or MAX_DECODE_ATTEMPTS decodeAudioData failures).
        //
        // A cb arriving while a decode is queued / running / retry-scheduled is
        // QUEUED onto it (pendingCb), not dropped -- and it stays parked ACROSS
        // a retry, so a play deferred on a decode that failed once still fires
        // when the retry succeeds. Only a permanent failure flushes with null.
        //
        // lowPriority (the PreLoadSound path) appends to the decode queue;
        // everything else (a play or a song actively waiting on this decode)
        // jumps the queue, so a cold on-demand sound is never stuck behind the
        // whole boot preload list.
        decode: function(name, cb, lowPriority) {
            name = this.nm(name);
            if (this.buffers[name]) { if (cb) cb(this.buffers[name]); return; }
            if (this.failed[name]) { if (cb) cb(null); return; }
            if (cb) (this.pendingCb[name] = this.pendingCb[name] || []).push(cb);
            if (this.inflight[name]) return;
            this.inflight[name] = true;
            this.enqueueDecode(name, !lowPriority);
        },

        enqueueDecode: function(name, front) {
            if (front) this.decodeQueue.unshift(name);
            else this.decodeQueue.push(name);
            this.pumpDecodes();
        },

        // Throttle. PreloadSounds (src/game/PreloadSounds.cpp, ASM-verified
        // faithful to v1.6.1 PreloadSounds @0x0011bb94) calls PreLoadSound
        // ~40-70 times back to back at boot. Handing every one of those to
        // decodeAudioData simultaneously overwhelmed weaker browsers (webOS
        // TV), which then failed the LAST decodes of the burst -- the swipe and
        // visceral-impact sounds, which the binary's preload order puts at the
        // very end. Bounding the in-flight count fixes that without making the
        // preload synchronous: nothing here blocks the boot thread or the first
        // frame, the queue just drains over the following frames.
        pumpDecodes: function() {
            while (this.decodeRunning < this.DECODE_CONCURRENCY && this.decodeQueue.length > 0) {
                this.startDecode(this.decodeQueue.shift());
            }
        },

        startDecode: function(name) {
            if (this.buffers[name] || this.failed[name]) return;
            var self = this;
            var path = this.sfxDir + name + '.ogg';
            var data;
            try { data = FS.readFile(path); } catch (e) {
                // An absent file never fixes itself -- permanent, no retry.
                this.failPermanently(name, 'missing sfx file ' + path);
                return;
            }
            this.decodeRunning++;
            // decodeAudioData detaches its ArrayBuffer; hand it a private copy
            // so the wasm HEAP that FS.readFile viewed is never detached. Also
            // guards against FS.readFile returning a Uint8Array VIEW into a
            // larger backing store (MEMFS often over-allocates) -- slicing by
            // byteOffset/byteLength yields exactly the file's bytes, never the
            // whole heap/backing buffer. Re-read per attempt so a retry decodes
            // a fresh (non-detached) copy rather than the consumed one.
            var ab = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength);
            var decodePromise = this.ctx.decodeAudioData(ab, function(decoded) {
                self.decodeRunning--;
                self.buffers[name] = decoded;
                delete self.inflight[name];
                delete self.failCount[name];
                self.flushCbs(name, decoded);
                self.pumpDecodes();
            }, function(err) {
                self.decodeRunning--;
                self.onDecodeError(name, err);
                self.pumpDecodes();
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

        flushCbs: function(name, buf) {
            var cbs = this.pendingCb[name];
            delete this.pendingCb[name];
            if (cbs) for (var i = 0; i < cbs.length; ++i) cbs[i](buf);
        },

        // A decodeAudioData failure is RETRYABLE: a browser can refuse a decode
        // for transient reasons (decoder resources exhausted by a concurrent
        // burst) and then succeed on the very same bytes moments later. The old
        // code marked the name failed[] forever on the first refusal, which is
        // what silenced the swipe SFX for a whole session. Note the parked cbs
        // are deliberately NOT flushed here -- they wait for the retry.
        onDecodeError: function(name, err) {
            var n = (this.failCount[name] || 0) + 1;
            this.failCount[name] = n;
            var msg = (err && err.message) ? err.message : String(err);
            if (n >= this.MAX_DECODE_ATTEMPTS) {
                this.failPermanently(name, 'decode failed ' + n + 'x: ' + msg);
                return;
            }
            console.warn('FNAudio decode attempt ' + n + ' failed, will retry: ' + name, err);
            var self = this;
            this.retryScheduled[name] = true;
            setTimeout(function() {
                delete self.retryScheduled[name];
                if (self.buffers[name] || self.failed[name]) return;
                self.enqueueDecode(name, true);   // a retry jumps the queue
            }, this.RETRY_DELAY_MS * n);
        },

        // Budget spent (or the file is simply absent): stop retrying, drop the
        // parked cbs with null, and record ONE line for the C++ drain to re-emit
        // through LOG_ERROR. One entry per name, not per attempt.
        failPermanently: function(name, reason) {
            this.failed[name] = true;
            delete this.inflight[name];
            delete this.retryScheduled[name];
            var line = name + ': ' + reason;
            console.warn('FNAudio giving up -- ' + line);
            if (this.failLog.length < 32) this.failLog.push(line);
            this.flushCbs(name, null);
        },

        // loopStartSec comes from the C++ caller (SFXPlay, via the linked
        // SfxLoopTable's SfxLoopStartSeconds); 0 = non-looping (the table's
        // no-entry sentinel -- entries only exist for loopStart > 0).
        playSfx: function(name, handle, gain, loopStartSec) {
            name = this.nm(name);
            var b = this.buffers[name];
            if (!b) {
                // Not decoded yet (name outside PreloadSounds' list, or a
                // retrigger while its decode is in flight): queue the play on
                // the decode instead of dropping it, so a cold sound's first
                // play arrives a few hundred ms late rather than never.
                // pendingSfx keeps the handle honest meanwhile: stop()/pause()
                // cancel it, setVolume() retargets it, isActive() reports it.
                var outer = this;
                var p = { name: name, gain: gain, loopStartSec: loopStartSec };
                this.pendingSfx[handle] = p;
                this.decode(name, function(decoded) {
                    if (outer.pendingSfx[handle] !== p) return;  // stopped/stale
                    delete outer.pendingSfx[handle];
                    if (!decoded) return;   // permanently failed: drop
                    outer.playSfx(name, handle, p.gain, p.loopStartSec);  // fast path now
                });
                return;
            }
            var self = this;
            var src = this.ctx.createBufferSource();
            src.buffer = b;
            var g = this.ctx.createGain();
            g.gain.value = gain;
            var ls = loopStartSec;
            var looping = (ls > 0);
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
            var p = this.pendingSfx[handle];
            if (p) { p.gain = gain; return; }   // decode-pending: retarget the queued play
            var e = this.active[handle];
            if (e) e.gain.gain.value = gain;
        },

        stop: function(handle) {
            delete this.pendingSfx[handle];   // cancel a decode-pending play
            var e = this.active[handle];
            if (!e) return;
            if (e.src) { try { e.src.onended = null; e.src.stop(0); } catch (x) {} }
            delete this.active[handle];
        },

        pause: function(handle) {
            // A decode-pending play is dropped on pause: starting it later,
            // mid-pause, would leak sound into the pause screen.
            if (this.pendingSfx[handle]) { delete this.pendingSfx[handle]; return; }
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
            if (this.pendingSfx[handle]) return 1;   // decode-pending counts as playing
            var e = this.active[handle];
            return (e && !e.paused) ? 1 : 0;
        },

        isPaused: function(handle) {
            var e = this.active[handle];
            return (e && e.paused) ? 1 : 0;
        },

        pauseAllSfx: function() {
            this.pendingSfx = {};   // drop decode-pending plays (see pause())
            var keys = Object.keys(this.active);
            for (var i = 0; i < keys.length; ++i) this.pause(keys[i]);
        },

        unpauseAllSfx: function() {
            var keys = Object.keys(this.active);
            for (var i = 0; i < keys.length; ++i) this.resume(keys[i]);
        },

        // loopStartSec comes from the C++ caller (SongPlay, via the linked
        // SfxLoopTable's SfxLoopStartSeconds); 0 = loop from the start.
        playSong: function(name, loopStartSec, vol) {
            name = this.nm(name);
            this.songStop();
            this.musicVol = vol;
            this.music.gain.value = this.musicMuted ? 0 : (vol * this.MASTER_MUSIC_GAIN);
            var self = this;
            this.songName = name;
            var startSong = function(b) {
                if (!b) return;                       // decode failed/missing
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
            this.music.gain.value = this.musicMuted ? 0 : (vol * this.MASTER_MUSIC_GAIN);
        },

        setSfxMuted: function(muted) {
            this.sfxMuted = !!muted;
            this.masterSfx.gain.value = this.sfxMuted ? 0 : this.MASTER_SFX_GAIN;
        },

        setMusicMuted: function(muted) {
            this.musicMuted = !!muted;
            this.music.gain.value = this.musicMuted ? 0 : (this.musicVol * this.MASTER_MUSIC_GAIN);
        },

        // Quit/teardown hard-stop. Stops every SFX source AND the music
        // source (this.stop / this.songStop already do real src.stop(0), not
        // just pause-bookkeeping), then belt-and-suspenders: zero both master
        // gains directly so even a source that somehow outlives the stop
        // calls (e.g. a decode-in-flight source that slips past a race) is
        // silent regardless. Called from mainEmscripten.cpp
        // StopWebAudioAndShutdown before ctx.suspend().
        stopAll: function() {
            this.pendingSfx = {};   // cancel decode-pending plays
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
    //
    // Port specific: audio-consent overlay ordering -- the overlay's tap
    // handler (shell.html #audio-consent-overlay) is the actual browser
    // gesture that must unlock this AudioContext. In the current boot design
    // (mainEmscripten.cpp BootWait), the overlay is only ever shown AFTER
    // g_game.init() -> SoundManager::Init -> fnaudio_init has already run, so
    // window.FNAudio.ctx normally already exists by tap time and the tap
    // handler resumes it directly -- this resumePending path exists only as a
    // defensive fallback for an ordering this codebase does not currently
    // produce (e.g. a future boot-flow change that shows the overlay before
    // init). window.FNAudio.resumePending, if a caller ever sets it before
    // this ctx exists, is honoured here immediately after creation.
    var pendingResume = (window.FNAudio && window.FNAudio.resumePending) === true;
    window.FNAudio = FN;
    if (pendingResume) {
        try { ctx.resume(); } catch (e) {}
    }

});

// PreLoadSound's decode hook only -- lowPriority=true, so the boot preload
// burst queues BEHIND any on-demand decode a play/song is actively waiting on.
EM_JS(void, fnaudio_decode, (const char* namePtr), {
    if (window.FNAudio) window.FNAudio.decode(UTF8ToString(namePtr), null, true);
});

EM_JS(void, fnaudio_play_sfx, (const char* namePtr, unsigned handle, double gain, double loopStartSec), {
    if (window.FNAudio) window.FNAudio.playSfx(UTF8ToString(namePtr), handle, gain, loopStartSec);
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

// Port specific: dev-tool ("?osdsfx=1" / F4) snapshot of everything on the JS
// side that can make a play inaudible, packed into one int so the OSD toast
// needs a single call. No state of its own -- pure read of FNAudio's maps.
//   bits 0-2  decode state of <name>: 0 undecoded (playSfx will defer it),
//             1 decoded (plays immediately), 2 queued/decoding (deferred),
//             3 PERMANENTLY failed (the queued play is dropped, never heard),
//             4 retry scheduled after a failed attempt (still deferred)
//   bits 4-5  ctx.state: 0 no context, 1 running, 2 suspended, 3 closed/other
//   bit  6    sfxMuted (masterSfx bus forced to gain 0)
//   bit  7    masterSfx.gain.value > 0
EM_JS(int, fnaudio_sfx_diag, (const char* namePtr), {
    var A = window.FNAudio;
    if (!A) return 0;
    var n = A.nm(UTF8ToString(namePtr));
    var d = A.buffers[n] ? 1
          : (A.failed[n] ? 3
          : (A.retryScheduled[n] ? 4
          : (A.inflight[n] ? 2 : 0)));
    var c = 0;
    if (A.ctx) {
        if (A.ctx.state === 'running') c = 1;
        else if (A.ctx.state === 'suspended') c = 2;
        else c = 3;
    }
    var m = A.sfxMuted ? 1 : 0;
    var g = (A.masterSfx && A.masterSfx.gain.value > 0) ? 1 : 0;
    return d | (c << 4) | (m << 6) | (g << 7);
});

// Port specific: pop one "name: reason" line off FNAudio.failLog into <out>
// (a C++ buffer of <cap> bytes), returning 1 if a line was written and 0 when
// the log is empty. Writes bytes through HEAPU8 by hand and clamps every byte
// to printable ASCII ('?' otherwise) -- browser error text can carry curly
// quotes / non-Latin characters, and the project's runtime-output rule is
// ASCII only. Deliberately does NOT use stringToNewUTF8 / a JS->wasm callback:
// that would need a new EXPORTED_FUNCTIONS entry in CMakeLists.txt, and the
// existing history of a call site silently no-op'ing because a function was
// missing from that list is not worth repeating for a diagnostic path.
EM_JS(int, fnaudio_take_decode_failure, (char* out, int cap), {
    var A = window.FNAudio;
    if (!A || !A.failLog || A.failLog.length === 0) return 0;
    var s = String(A.failLog.shift());
    var lim = cap - 1;
    var n = 0;
    for (var i = 0; i < s.length && n < lim; ++i) {
        var ch = s.charCodeAt(i);
        HEAPU8[out + n] = (ch >= 32 && ch < 127) ? ch : 63;   // 63 = '?'
        ++n;
    }
    HEAPU8[out + n] = 0;
    return 1;
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
// the backend reads sfx/<name>.ogg from.
void SoundManager::Init() {
    std::string dataDir = TextureManager::GetDataDir();
    fnaudio_init(dataDir.c_str(), MASTER_SFX_GAIN, MASTER_MUSIC_GAIN);
    LOG_INFO("SoundManager", "Web Audio backend initialised (Ogg/Vorbis, no SDL device)");
}

// Cue-file scanning is a no-op on both backends.
void SoundManager::Initialise(const char* /*basePath*/) {
}

// 0x0018cab8 -- allocates MortarSoundMAM (port: plain MortarSound)
MortarSound* SoundManager::CreateNewSound() {
    return new MortarSound();
}

// Port specific: re-emit permanent decode failures the JS side recorded.
// Decodes fail asynchronously, long after the PreLoadSound/SFXPlay call that
// kicked them, and console.warn is invisible on a TV -- so JS parks one line
// per dead name in FNAudio.failLog and this drains it into LOG_ERROR, which
// survives the web build's FN_LOG_ERRORS_ONLY stripping. Called from the
// audio entry points below rather than a per-frame hook because SoundManager
// has no Update; one EM_JS call that usually returns 0 immediately.
static void DrainDecodeFailures() {
    char line[192];
    while (fnaudio_take_decode_failure(line, (int)sizeof(line)) != 0) {
        LOG_ERROR("SoundManager", "web sfx decode gave up: %s", line);
        if (FN::g_bOsdSfx) {
            char osd[224];
            snprintf(osd, sizeof(osd), "DECODE DEAD %s", line);
            OSD_AddMessage(osd);
        }
    }
}

// PreLoad = decode hook. PreloadSounds.cpp's list decodes common sfx during
// the loading screen so they are ready before first play. The decode itself is
// queued + throttled JS-side (FNAudio.pumpDecodes) -- this never blocks.
void SoundManager::PreLoadSound(const char* name) {
    if (!name || !*name) return;
    DrainDecodeFailures();
    fnaudio_decode(name);
}

void SoundManager::PreLoadSoundEx(const char* name, bool /*preload*/) {
    PreLoadSound(name);
}

// Initial per-source GainNode value handed to playSfx. Unity: the 0-255 byte
// that actually decides the level only arrives one call later, via
// SFXSetVolume (MortarSound::SetVolume), which retargets this same node to
// byte/255. Named so the dev-tool readout below cannot drift from the value
// really passed.
static const double INITIAL_SFX_GAIN = 1.0;

// Port specific: handle of the most recent play that posted an OSD toast, so
// SFXSetVolume can complete that toast with the volume byte. 0 = nothing
// pending (already completed, or the flag is off).
static uint32_t s_OsdVolHandle = 0;

// Assign monotonic handle, kick a JS source. Volume arrives right after via
// SFXSetVolume (GameSound::SFXPlay computes the per-play byte, applied as a
// linear gain -- see SFXSetVolume below).
uint32_t SoundManager::SFXPlay(const char* name, MortarSound* sound) {
    if (!name || !*name) return 0;

    DrainDecodeFailures();

    uint32_t newId = m_NextSoundId++;
    if (m_NextSoundId == 0) m_NextSoundId = 1;   // skip 0 (idle sentinel)

    // Lowercased basename is the SfxLoopTable key (generated table is keyed
    // strictly lowercase; game code passes Title-Case) -- same fold as the
    // SDL backend's LoadSound and SongPlay below.
    std::string lower(name);
    for (size_t i = 0; i < lower.size(); ++i) {
        if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] + ('a' - 'A'));
    }

    // Port specific: dev-tool SFX readout (?osdsfx / F4), display-only -- the
    // audio path below is never gated by it. Sampled BEFORE the play call so
    // d= reports the state the play actually meets (playSfx itself kicks a
    // decode for an undecoded name). Fields:
    //   d=1 decoded (starts now)   d=0 undecoded -> deferred until its decode
    //   d=P queued/decoding        d=R a decode attempt failed, retry pending
    //                                  (still deferred -- it fires if the retry
    //                                   succeeds)
    //   d=F decode PERMANENTLY failed (retry budget spent) -> play DROPPED.
    //       A "DECODE DEAD <name>: <reason>" OSD line names the cause.
    //   g=  initial GainNode value passed to playSfx (NOT the volume byte)
    //   c=r ctx running  c=s suspended (no user gesture -> everything silent)
    //   c=c closed/other  c=? no AudioContext at all
    //   m=1 SFX bus muted (masterSfx gain forced to 0); m=0 unmuted
    //   M=0 masterSfx gain is 0 despite m=0 (bus zeroed by teardown)
    //   v=  the raw 0-255 volume byte, appended by SFXSetVolume -- the source's
    //       linear gain is v/255 (255 full, 0 silent, 20 about 8%). A line with
    //       NO v= never got a SetVolume for this handle.
    if (FN::g_bOsdSfx) {
        const int diag = fnaudio_sfx_diag(lower.c_str());
        static const char kDecChar[8] = { '0', '1', 'P', 'F', 'R', '?', '?', '?' };
        static const char kCtxChar[4] = { '?', 'r', 's', 'c' };
        char osd[64];
        const int masterOn = (diag >> 7) & 1;
        const int muted    = (diag >> 6) & 1;
        if (masterOn || muted) {
            snprintf(osd, sizeof(osd), "[%06u] %s d=%c g=%.1f c=%c m=%d",
                     Debug::g_LogTick, name, kDecChar[diag & 7],
                     INITIAL_SFX_GAIN, kCtxChar[(diag >> 4) & 3], muted);
        } else {
            // Bus silent without the mute flag -- worth calling out explicitly.
            snprintf(osd, sizeof(osd), "[%06u] %s d=%c g=%.1f c=%c m=0 M=0",
                     Debug::g_LogTick, name, kDecChar[diag & 7],
                     INITIAL_SFX_GAIN, kCtxChar[(diag >> 4) & 3]);
        }
        OSD_AddMessage(osd);
        s_OsdVolHandle = newId;
    }

    fnaudio_play_sfx(lower.c_str(), newId, INITIAL_SFX_GAIN,
                     Mortar::SfxLoopStartSeconds(lower.c_str()));

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

// vol is the raw 0-255 byte from MortarSound::SetVolume, applied as this
// source's linear GainNode gain (vol/255).
//
// DIFFERS: original = mute gate, byte > 5 plays at FULL amplitude with samples
// mixed raw (v1.6.1 MAMAudioThread::FillBuffer @0x0022f7f0); port scales by the
// byte instead because reproducing the gate turns every in-game fade into an
// abrupt on/off and forces sounds the game intends at 1-7% to full volume -- a
// limitation of the 2010 mixer rather than a design choice.
//
// A zero-gain source keeps playing (silent, not paused) -- that part IS
// faithful -- so a silenced sound still runs to completion and its onended
// handler still retires the handle.
void SoundManager::SFXSetVolume(uint32_t handle, uint8_t vol) {
    if (handle == 0) return;

    // Port specific: dev-tool -- complete this play's OSD line with the volume
    // byte (SFXPlay had to toast before MortarSound::SetVolume computed it).
    // One-shot per play: later volume changes on the same handle (bomb-fuse
    // ramp) do not re-append. Display-only, never gates the call below.
    if (FN::g_bOsdSfx && handle == s_OsdVolHandle) {
        s_OsdVolHandle = 0;
        char suffix[16];
        snprintf(suffix, sizeof(suffix), " v=%u", (unsigned)vol);
        OSD_AppendToLast(suffix);
    }

    fnaudio_set_volume(handle, (double)vol / 255.0);
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

// ASM-spec v1.6.1 SoundManager::SongPlay @0x002307a0 -> SFXPlayInternal @0x002306ec
//  -> MAMAudioController::PlaySound(isMusic=1) @0x0022fd40. Music runs through the
// same MAM voice mixer as SFX; isMusic only routes mute. Loop point is from the PCM
// header word[4] via MAMAudioController::LoadSound @0x0022f46c (music-menu=24004,
// NOT musicdesc.xml's defunct .caf 66162); PlayNewSound @0x0022f6c4 sets
// loop=(loopStart!=0); FillBuffer @0x0022f7f0 rewinds to loopStart on end.
//
// Loop point resolved from the linked SfxLoopTable (SfxLoopStartSeconds),
// which is header-derived (music-menu=24004, music-dojo=1, background=261549
// samples @ 16000 Hz) -- not musicdesc.xml's defunct .caf value.
void SoundManager::SongPlay(const char* name) {
    if (!name) return;

    std::string lower(name);
    for (size_t i = 0; i < lower.size(); ++i) {
        if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] + ('a' - 'A'));
    }

    double loopStartSec = Mortar::SfxLoopStartSeconds(lower.c_str());   // 0 = loop from start

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

// v1.6.1 Mortar::SoundManager::SongSetMemorySize @0x00230650 -- stub nop
void SoundManager::SongSetMemorySize(int size) { (void)size; }

// v1.6.1 Mortar::SoundManager::SetMusicVolume @0x00230534
void SoundManager::SetMusicVolume(float vol) {
    s_MusicVolume = vol;
    SyncMutes();
    fnaudio_set_music_vol(s_MusicVolume);
}

// v1.6.1 Mortar::SoundManager::SetSFXVolume @0x002304f4
void SoundManager::SetSFXVolume(float vol) {
    s_SFXVolume = vol;
    SyncMutes();
}

// Binary per channel: muted = (MasterMute || vol < 0.1) -> SetMusicMute / SetSfxMute.
// MasterMute has no writer (SoundManager::MuteSound @0x00230578 has zero in-binary
// callers), so the port drops the OR and pushes the result to the JS gain gates.
// ASM-verified: 2026-04-29T00:00Z v1.6.1 Mortar::SyncMutes @ 0x002302e4 (asm-inspector)
void SoundManager::SyncMutes() {
    s_SFXMuted   = ((double)s_SFXVolume   < 0.1);
    s_MusicMuted = ((double)s_MusicVolume < 0.1);
    fnaudio_set_sfx_muted(s_SFXMuted ? 1 : 0);
    fnaudio_set_music_muted(s_MusicMuted ? 1 : 0);
}

} // namespace Mortar
