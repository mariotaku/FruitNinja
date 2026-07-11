#!/usr/bin/env python3
"""
tools/web/transcode-audio-web.py -- Build-phase audio staging for the web target.

Usage:
    python3 tools/web/transcode-audio-web.py <repo_root>

Mirrors FruitNinjaBada/Data into build/web-audio-staging/Data. Every
sfx/*.wav.pcm is TRANSCODED to Ogg/Vorbis (sfx/<name>.ogg) exactly ONCE at
build time; the source .wav.pcm is NOT copied into the staging tree (that would
re-bloat the .data payload we are trying to shrink). Everything else in Data is
copied through unchanged.

Why: the web build no longer runs the SDL software mixer. It uses the Web Audio
API backend (src/engine/audio/SoundManagerWebAudio.cpp), which lets the browser
decode Ogg/Vorbis and mix on its own audio thread -- off the JS main thread (no
GC underrun / crackle on a slow webOS TV) -- and compressed assets shrink the
.data payload from ~72 MB (uncompressed PCM) to a few MB. Desktop keeps the
faithful SDL mixer + raw .wav.pcm untouched.

The staged copy is a BUILD ARTIFACT under build/ (gitignored) -- the real
FruitNinjaBada/Data/*.wav.pcm files (extracted, binary-faithful assets) are
NEVER modified. CMakeLists.txt's EMSCRIPTEN --preload-file points at the staged
directory instead of the real Data dir; see the fn_web_audio_staging custom
target there, which runs this script before fruit-ninja links.

.wav.pcm format (Mortar::SoundManager::LoadSound, MAMAudioController):
    20-byte header, 5 x int32 LE: type(1), sampleRate, bitDepth(16),
    sampleCount, loopStart (0 = no loop). Followed by sampleCount x int16 LE
    mono PCM samples. No RIFF/WAV container -- this is a bare custom header.

Encoder: ffmpeg (Debian package, includes libvorbis). The raw int16 PCM body
(file bytes from offset 20 onward) is piped to ffmpeg's stdin as headerless
s16le; ffmpeg produces the .ogg. No temp WAV, no soundfile/numpy dependency.

    ffmpeg -hide_banner -loglevel error -y -f s16le -ar <rate> -ac 1 \
        -i pipe:0 -c:a libvorbis -q:a 5 <out.ogg>

IMPORTANT: no >>4 amplitude shift is applied here (unlike the desktop loader,
which shifts for 16-voice int-mixer headroom). The web backend mixes in float
with a master gain node, so full-scale audio is shipped and loudness is handled
by MASTER_SFX_GAIN in the JS backend.

Loop metadata: a small JSON file is emitted at
    build/web-audio-staging/Data/sfx/sfx-loops.json
mapping { "<name>": <loopStart_seconds> } for every sfx with loopStart != 0
(loopStart_seconds = loopStart / sampleRate). Keys are the lowercased bare name
(no extension), matching the case-folded lookup the JS backend performs (mirrors
the desktop ResolvePathCI fallback). The C++/JS backend consumes this to set the
Web Audio loop point (source.loop / loopStart / loopEnd) for looping sfx (e.g.
bomb-fuse) and music (music-menu).

Idempotent and incremental: an already-transcoded .ogg is skipped unless its
source .wav.pcm is newer; other files use size+mtime copy-if-different. Pure
Python stdlib + an ffmpeg binary on PATH.
"""

import json
import os
import shutil
import struct
import subprocess
import sys

HEADER_FMT = "<5i"
HEADER_SIZE = 20
SFX_RELPATH = "sfx"
LOOP_JSON_NAME = "sfx-loops.json"
VORBIS_QUALITY = "5"


def read_header(path):
    """Parse the 20-byte .wav.pcm header.
    Returns (kind, rate, bit_depth, count, loop_start)."""
    with open(path, "rb") as f:
        hdr = f.read(HEADER_SIZE)
    return struct.unpack(HEADER_FMT, hdr)


def read_pcm_body(path, count):
    """Return the raw int16 LE PCM body bytes (headerless), clamped to the
    header's sample count."""
    with open(path, "rb") as f:
        f.seek(HEADER_SIZE)
        return f.read(count * 2)


def sfx_name_from_filename(name):
    """'Clean-Slice-1.wav.pcm' -> 'clean-slice-1' (lowercased, no extension)."""
    base = name
    if base.lower().endswith(".wav.pcm"):
        base = base[: -len(".wav.pcm")]
    return base.lower()


def needs_transcode(src_path, dst_ogg):
    if not os.path.isfile(dst_ogg):
        return True
    return os.path.getmtime(dst_ogg) < os.path.getmtime(src_path)


def encode_ogg(pcm_body, rate, dst_ogg):
    """Pipe headerless s16le mono PCM to ffmpeg -> Ogg/Vorbis. Full-scale
    (no amplitude shift)."""
    tmp = dst_ogg + ".tmp.ogg"
    cmd = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "s16le", "-ar", str(int(rate)), "-ac", "1",
        "-i", "pipe:0",
        "-c:a", "libvorbis", "-q:a", VORBIS_QUALITY,
        tmp,
    ]
    proc = subprocess.run(cmd, input=pcm_body)
    if proc.returncode != 0:
        raise RuntimeError("ffmpeg failed ({}) encoding {}".format(proc.returncode, dst_ogg))
    os.replace(tmp, dst_ogg)


def transcode_sfx_file(src_path, dst_ogg, stats):
    if not needs_transcode(src_path, dst_ogg):
        stats["sfx_skipped"] += 1
        return

    _kind, rate, _bit_depth, count, _loop = read_header(src_path)
    pcm_body = read_pcm_body(src_path, count)
    encode_ogg(pcm_body, rate, dst_ogg)
    stats["sfx_transcoded"] += 1


def copy_if_different(src_path, dst_path):
    if os.path.isfile(dst_path):
        s_src = os.stat(src_path)
        s_dst = os.stat(dst_path)
        if s_src.st_size == s_dst.st_size and s_dst.st_mtime >= s_src.st_mtime:
            return False
    shutil.copy2(src_path, dst_path)
    return True


def stage_tree(src_root, dst_root):
    stats = {
        "sfx_transcoded": 0,
        "sfx_skipped": 0,
        "other_copied": 0,
        "other_skipped": 0,
    }
    loops = {}

    for root, _dirs, files in os.walk(src_root):
        rel_dir = os.path.relpath(root, src_root)
        dst_dir = os.path.join(dst_root, rel_dir) if rel_dir != "." else dst_root
        os.makedirs(dst_dir, exist_ok=True)

        rel_norm = rel_dir.replace("\\", "/")
        is_sfx_dir = rel_norm == SFX_RELPATH or rel_norm.startswith(SFX_RELPATH + "/")

        for name in files:
            src_path = os.path.join(root, name)

            if is_sfx_dir and name.lower().endswith(".wav.pcm"):
                short = sfx_name_from_filename(name)
                dst_ogg = os.path.join(dst_dir, short + ".ogg")
                transcode_sfx_file(src_path, dst_ogg, stats)

                # Collect loop metadata straight from the header.
                _k, rate, _bd, _c, loop_start = read_header(src_path)
                if loop_start != 0 and rate > 0:
                    loops[short] = float(loop_start) / float(rate)
            else:
                dst_path = os.path.join(dst_dir, name)
                if copy_if_different(src_path, dst_path):
                    stats["other_copied"] += 1
                else:
                    stats["other_skipped"] += 1

    return stats, loops


def sweep_stale_pcm(dst_root):
    """Remove any stale sfx/*.wav.pcm left in staging by the retired resampler.
    The whole staging Data dir is --preload-file'd, so a lingering .wav.pcm would
    re-bloat .data. Only .ogg + non-pcm files should remain under sfx/."""
    removed = 0
    dst_sfx = os.path.join(dst_root, SFX_RELPATH)
    if not os.path.isdir(dst_sfx):
        return removed
    for name in os.listdir(dst_sfx):
        if name.lower().endswith(".wav.pcm"):
            try:
                os.remove(os.path.join(dst_sfx, name))
                removed += 1
            except OSError:
                pass
    return removed


def main():
    if len(sys.argv) < 2:
        print("Usage: transcode-audio-web.py <repo_root>", file=sys.stderr)
        sys.exit(1)

    if shutil.which("ffmpeg") is None:
        print("ERROR: ffmpeg not found on PATH (apt-get install -y ffmpeg)", file=sys.stderr)
        sys.exit(1)

    repo_root = sys.argv[1]
    src_root = os.path.join(repo_root, "FruitNinjaBada", "Data")
    dst_root = os.path.join(repo_root, "build", "web-audio-staging", "Data")

    if not os.path.isdir(src_root):
        print("ERROR: source Data dir not found: {}".format(src_root), file=sys.stderr)
        sys.exit(1)

    os.makedirs(dst_root, exist_ok=True)

    print("[transcode-audio-web] staging {} -> {} (sfx -> Ogg/Vorbis via ffmpeg)".format(
        src_root, dst_root))
    stats, loops = stage_tree(src_root, dst_root)

    # Emit loop metadata JSON next to the .ogg files.
    loop_json_path = os.path.join(dst_root, SFX_RELPATH, LOOP_JSON_NAME)
    os.makedirs(os.path.dirname(loop_json_path), exist_ok=True)
    with open(loop_json_path, "w") as f:
        json.dump(loops, f, indent=0, sort_keys=True)

    removed = sweep_stale_pcm(dst_root)

    print("[transcode-audio-web] sfx: {} transcoded, {} unchanged (skipped); {} loop points".format(
        stats["sfx_transcoded"], stats["sfx_skipped"], len(loops)))
    print("[transcode-audio-web] other assets: {} copied, {} unchanged (skipped)".format(
        stats["other_copied"], stats["other_skipped"]))
    if removed:
        print("[transcode-audio-web] swept {} stale .wav.pcm from staging sfx/".format(removed))


if __name__ == "__main__":
    main()
