#!/usr/bin/env python3
"""
tools/assets/stage-assets.py -- Build-phase asset staging, shared by BOTH the
host and web targets.

Usage:
    python3 stage-assets.py <repo_root> <out_staging_data_dir> [--web|--wii] \
        [--subset-font] [--ogg-audio] [--gen-loop-table <path>]

Mirrors FruitNinjaBada/Data into <out_staging_data_dir>. The real
FruitNinjaBada/Data files are never modified; the staged copy is a build/
artifact (gitignored). CMakeLists.txt's fn_asset_staging custom target runs
this before fruit-ninja links (host: FN_DATA_DIR_PATH points straight at the
staging dir via GameSDL.cpp's existing SetDataDir call, no runtime code
changes; web: additionally preloaded into MEMFS via --preload-file).

WII MODE (--wii): the Wii target ships pre-tiled native GX textures and raw
.wav.pcm audio (no Ogg/Vorbis; ASND/AESND plays raw PCM). The whole Wii
staging path is pure Python stdlib (struct/os/shutil) -- NO ffmpeg, NO
fontTools, NO node/svg-to-webp dependency and NO Pillow -- because the msys2
Python the Wii build uses has no pip/Pillow (the CMake Wii branch does not
even resolve NODE_EXECUTABLE -- see the FRUIT_PLATFORM_WII guard in the root
CMakeLists.txt).

  - GAME TEXTURES: every transcodable Tex1 *.tex is decoded to RGBA8888
    (tools/lib/tex_decoder.py) and re-encoded as a pre-tiled "GXT1" container
    (tools/lib/gx_encoder.py encode_gxtx; reader ReadGxtx in
    src/engine/asset/TextureFileFormat.cpp, uploader Wii_UploadTiledGX in
    src/engine/render/gl_funcsWii.cpp) under the same relative path/basename.
    The GX format PRESERVES the source's bit-depth so MEM1 isn't bloated by
    expanding everything to 32bpp at load time: RGBA8888 -> GX_TF_RGBA8
    (lossless), RGB565/RGB888 -> GX_TF_RGB565, RGBA5551/RGBA4444 ->
    GX_TF_RGB5A3 (see gx_encoder.TEX1_TO_GX). Non-transcodable .tex
    (Tex2/Tex3/DDS/PVRTC/unknown) copy verbatim as before.
  - WIDGET ART: the Wii build compiles ReadWebP out (no libwebp), so the
    WebP .tex files are ignored; instead each BASE widget's raw-RGBA sidecar
    (assets/ui-widgets/generated/<name>.rgba: "RRAW" magic + u16le
    width/height + w*h*4 RGBA8 bytes, little-endian -- emitted next to the
    WebP by svg-to-webp.mjs during a prior non-Wii configure; never generated
    under --wii) is re-encoded as a GXT1 (always GX_TF_RGBA8 -- no Tex1
    source to preserve a bit-depth from), staged under the same <name>.tex
    basename. hd_* art is skipped (Wii HD is disabled). If generated/ has no
    .rgba sidecars (clean checkout, no prior host/web node run yet), Wii
    SettingsScreen widgets fall back to placeholder art (same non-fatal
    contract as the host/web "node not found" case).
  - PREBAKED TTF FONTS (task #51, extended #54): tools/wii/bake-fonts.py
    rasterizes the used glyph set (tmp/prebake/bake_plan.json, produced by a
    separate offline planning pass -- see tools/wii/prebaked-font-format.md)
    with FreeType and packs it into native IA8 GXT1 atlas pages + a metrics
    index (FNT3 -- glyph rects/metrics AND face-level ascender/descender/
    lineHeight). Staged under fonts/prebaked/<lang>/<size>.idx +
    <size>_pN.gxtx. This step needs freetype-py (a bake-time host dependency,
    NOT pure-stdlib like the rest of --wii staging) and the bake_plan.json +
    chars_<lang>.txt inputs; both are optional here -- if either is missing
    (freetype-py not pip-installed on this machine, or the tmp/prebake/ plan
    hasn't been generated yet) this prints one warning and continues
    non-fatally, same "missing tool -> skip, don't fail the whole stage"
    contract as the widget-art node/Pillow cases above. Task #54: this baked
    atlas is now the ONLY glyph/face-metric source on Wii -- neither
    stb_truetype nor FreeType is linked into the Wii runtime at all (see
    src/engine/render/FontCacheObjectTTF.cpp), so a baked miss renders no
    glyph rather than falling back to an on-device rasterizer. The runtime
    fontstruetype/*.ttf sources are therefore excluded from the Wii SD stage
    (see stage_tree_raw below) -- they remain in the repo as bake-time-only
    inputs to this script.

ALWAYS (both host and web):

  1. TEXTURES (// Port specific: compressed textures via Pillow/libwebp):
     every *.tex whose header parses as a Tex1 with a known pixel format is
     decoded to RGBA8888 and re-encoded as WebP via Pillow (not ffmpeg --
     this is the point: it works on any host with no ffmpeg-libwebp, no
     shell dependency), then written back under the SAME relative path +
     SAME .tex filename (WebP bytes inside a .tex-named file). The engine
     loader (Mortar::TextureFileFormat) dispatches by CONTENT, not
     extension: a WebP reader registered at g_readers[0] detects the
     RIFF/WEBP magic and decodes; real Tex1 .tex fall through to the Tex1
     reader. So no path, preload, or platform-branch change is needed --
     only the file bytes shrink. Textures that do NOT parse as Tex1
     (Tex2/Tex3/DDS/PVRTC/unknown format) are copied verbatim so their own
     readers still handle them.

  2. WIDGET ART MERGE: after the Data mirror, every pre-generated file under
     assets/ui-widgets/generated/*.tex (repo-relative, sibling of Data, NOT
     under FruitNinjaBada -- see tools/assets/svg-to-webp.mjs, a Node/sharp
     build step that the fn_asset_staging CMake target runs BEFORE this
     script, on every platform including Windows) is copied verbatim into
     <out>/textures/, overwriting. These are already WebP-encoded lossless
     .tex files, so no re-encode. If assets/ui-widgets/generated/ is missing
     or empty (e.g. node wasn't found at configure time), this prints one
     warning line and continues (non-fatal -- matches the existing "widgets
     fall back to placeholder art" contract in
     SettingsScreen::LoadOrPlaceholder).

  Everything else in Data is copied through unchanged (mtime-based
  copy-if-different).

AUDIO (--ogg-audio flag, IMPLIED by --web):

  3. AUDIO: every sfx/*.wav.pcm is TRANSCODED to Ogg/Vorbis (sfx/<name>.ogg)
     exactly ONCE at build time via ffmpeg; the source .wav.pcm is NOT
     copied into the staging tree (that would re-bloat the payload we are
     shrinking). Under --web this also emits sfx/sfx-loops.json (loop points
     in SECONDS, consumed by the web-JS Web Audio backend only). Without
     --ogg-audio (plain host build), *.wav.pcm files copy through verbatim
     like any other file -- the host build plays raw PCM via the existing
     SDL mixer, unchanged.

     This step is decoupled from --web (mirrors the --subset-font
     decoupling below) so a native/SDL target that wants the smaller Ogg
     payload but NOT the rest of --web can request it standalone via
     --ogg-audio. Since .ogg has no 20-byte custom header, the loopStart
     sample offset the native loader used to read from the .wav.pcm header
     is no longer available at runtime; --gen-loop-table <path> emits a
     small generated C++ source (Mortar::SfxLoopStartSamples, see
     gen_loop_table_cpp) mapping each looping sound's lowercased bare name
     to its loopStart IN SAMPLES (not seconds -- unlike sfx-loops.json),
     read straight from the header this script already parses. If
     --ogg-audio is passed without --gen-loop-table, the C++ emit is simply
     skipped (logged, non-fatal) -- e.g. plain --web today still only needs
     the JSON, not the table.

FONT SUBSET (--subset-font flag, IMPLIED by --web):

  4. FONT (fontstruetype/gangofchinese.ttf, ~5 MB): SUBSET down to only the
     Unicode code points that appear in the loaded stringtables, via
     fonttools' pyftsubset. gangofchinese.ttf is the SHARED default TTF face
     for every NON-Arabic language (PreloadFontsTTF @0x0011c1fc picks
     arabic.ttf only when languageFlag == 0x14 "arabic"; every other
     language in kLanguageSuffix -- english_us/uk, french, spanish, german,
     italian, dutch, swedish, danish, norwegian, finnish, korean, japanese,
     (traditional) chinese, latin spanish, polish, portuguese (pt/br),
     russian, fake debug language -- shares this one face; see
     src/game/PreloadFontsTTF.cpp, src/engine/util/StringTable.cpp). Any
     --subset-font build ships one bundle for every language (the user can
     switch language at runtime), so the used-glyph set is the UNION of
     every translations_<lang>.str BODY file except translations_arabic.str
     (own font) and translations_header.str (ASCII lookup keys, never
     rendered) -- plus printable ASCII (digits/latin appear inside every
     language's strings too: mode names, scores). arabic.ttf (99 KB) and
     every other fontstruetype file are copied verbatim -- only
     gangofchinese.ttf is subsetted. Glyph lookup is plain FreeType
     FT_Get_Char_Index per code point (see FontCacheObjectTTF.cpp) -- no
     HarfBuzz shaping -- so a code-point-based subset (no OpenType layout
     table retention) renders identically. Plain host build (no flags) ships
     the full unsubsetted TTF (copied verbatim like any other file). --wii
     never reaches this path (its own prebaked-atlas pipeline replaces TTF
     entirely, see WII MODE above).

     This step is decoupled from --web so the webOS target (FRUIT_PLATFORM_WEBOS,
     which does NOT want the --web Ogg/WebP-audio transcoding but DOES want a
     smaller IPK) can request just the font subset via --subset-font without
     --web. --web implies --subset-font so existing web behaviour is
     unchanged. If fontTools/pyftsubset isn't importable and can't be
     auto-installed, subset_cjk_font degrades to a verbatim copy of the full
     TTF (same non-fatal "missing tool -> skip, don't fail the stage"
     contract as the widget-art/Pillow cases).

Why WEB-only audio transcoding: the web build no longer runs the SDL
software mixer. It uses the Web Audio API backend
(src/engine/audio/SoundManagerWebAudio.cpp), which lets the browser decode
Ogg/Vorbis and mix on its own audio thread -- off the JS main thread (no GC
underrun / crackle on a slow webOS TV) -- and compressed assets shrink the
.data payload from ~72 MB (uncompressed PCM) to a few MB. Desktop keeps the
faithful SDL mixer + raw .wav.pcm untouched. The font subset is likewise
only needed to shrink the preload/install payload (wasm MEMFS preload for
web, IPK size for webOS); a plain host build has no payload-size constraint
and ships the full font.

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
with a master gain node, so full-scale audio is shipped and loudness is
handled by MASTER_SFX_GAIN in the JS backend.

Loop metadata (web only): a small JSON file is emitted at
    <out>/sfx/sfx-loops.json
mapping { "<name>": <loopStart_seconds> } for every sfx with loopStart != 0
(loopStart_seconds = loopStart / sampleRate). Keys are the lowercased bare
name (no extension), matching the case-folded lookup the JS backend performs
(mirrors the desktop ResolvePathCI fallback). The C++/JS backend consumes
this to set the Web Audio loop point (source.loop / loopStart / loopEnd) for
looping sfx (e.g. bomb-fuse) and music (music-menu).

Idempotent and incremental: an already-transcoded .ogg is skipped unless its
source .wav.pcm is newer; other files use size+mtime copy-if-different. This
script self-provisions Pillow (both host + web need it for texture WebP
encode) and, under --web or --ogg-audio, ffmpeg (audio transcode; texture
transcode itself no longer uses ffmpeg). Under --web or --subset-font, it also
tries to self-provision fontTools (CJK font subset): when a tool is missing AND
it is running as root with apt-get available (i.e. inside the
emscripten/emsdk container), it apt-get installs the tool. ffmpeg is
mandatory for --web/--ogg-audio (raises a clear error naming the tool if it
can't be found/installed) -- but fontTools is best-effort: if it can't be imported
or auto-installed, the font subset step is skipped and the full
gangofchinese.ttf is copied verbatim instead (same non-fatal contract as
the widget-art/node-not-found case), so --subset-font never hard-fails a
build over a missing dev dependency. The image's Python is PEP 668
externally-managed, so fontTools is installed via the apt `fonttools`
package (not pip). The staged gangofchinese.ttf subset is
skipped unless it is older than the source .ttf or any translations_*.str
body file that feeds its charset.
"""

import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "lib"))
import gx_encoder
import tex_decoder

HEADER_FMT = "<5i"
HEADER_SIZE = 20
SFX_RELPATH = "sfx"
LOOP_JSON_NAME = "sfx-loops.json"
VORBIS_QUALITY = "5"

# Generated C++ loop table (see gen_loop_table_cpp): emitted when
# --gen-loop-table <path> is passed alongside --ogg-audio (or --web, which
# implies --ogg-audio). Carries each looping sfx's loopStart in SAMPLES (the
# raw .wav.pcm header field) so the native/SDL Ogg loader -- which no longer
# has the 20-byte header to read once audio ships as .ogg -- can still find
# the loop point. sfx-loops.json (SECONDS, web-JS-only) keeps shipping under
# --web unchanged; this table is the native/C++ equivalent, keyed the same
# way (lowercased basename, no extension).
LOOP_TABLE_HEADER = (
    "// Auto-generated by stage-assets.py --ogg-audio. Do not edit.\n"
    "#include \"engine/audio/SfxLoopTable.h\"\n"
    "#include <string.h>\n"
    "namespace Mortar {\n"
    "namespace {\n"
    "struct SfxLoopEntry { const char* name; uint32_t loopStart; }; "
    "// loopStart in SAMPLES\n"
    "static const SfxLoopEntry kSfxLoops[] = {\n"
)
LOOP_TABLE_FOOTER = (
    "};\n"
    "}\n"
    "uint32_t SfxLoopStartSamples(const char* name) {\n"
    "    if (!name) return 0u;\n"
    "    for (unsigned i = 0; i < sizeof(kSfxLoops)/sizeof(kSfxLoops[0]); ++i)\n"
    "        if (strcmp(kSfxLoops[i].name, name) == 0) return kSfxLoops[i].loopStart;\n"
    "    return 0u;\n"
    "}\n"
    "} // namespace Mortar\n"
)

# Pre-generated widget art (see tools/assets/svg-to-webp.mjs, run by the
# fn_asset_staging CMake target before this script): repo-relative, sibling
# of FruitNinjaBada/Data.
WIDGET_TEX_RELDIR = os.path.join("assets", "ui-widgets", "generated")

# Wii prebaked-font plan inputs (task #51): repo-relative, gitignored scratch
# dir -- see tools/wii/prebaked-font-format.md. Not committed, so this step
# is best-effort (see stage_wii_prebaked_fonts).
WII_PREBAKE_PLAN_RELDIR = os.path.join("tmp", "prebake")
WII_PREBAKED_FONTS_RELDIR = os.path.join("fonts", "prebaked")

# --- Texture transcoding (host + web): Tex1 .tex -> WebP-in-.tex -------------
# WEBP_QUALITY is the lossy quality 0..100 (higher = better/larger). If UI/text
# looks soft or shows compression artifacts, flip WEBP_LOSSLESS to True for
# pixel-exact (larger) output. These are for real game photo-textures; the
# pre-generated widget art (WIDGET_TEX_RELDIR) is lossless by its own script.
WEBP_QUALITY = "90"
WEBP_LOSSLESS = False

# Tex1 header/format constants + the decode core live in tools/lib/tex_decoder.py
# (shared with tools/assets/convert_tex.py).

# --- "Lite" branding erase (build-time asset edit) ---------------------------
# DIFFERS: the original title texture hd_sml_title.tex reads "FRUIT NINJA LITE"
# (v1.6.1 is the free Lite SKU). This port ships the full game, so the "LITE"
# wordmark is erased by punching its glyphs to transparent (alpha=0) at decode
# time -- the real .tex under FruitNinjaBada/Data is never touched, only the
# staged WebP copy. The small sml_title.tex has no "LITE".
#
# The last NINJA kanji's lower-right stroke overlaps LITE's "L", so a plain
# rectangle would clip the kanji. Erase is therefore color-aware over the LITE
# bounding box [x0,y0,x1,y1] (inclusive, DECODED 512x128 coords): right of
# x_safe it is pure LITE -> full erase; in the narrow overlap strip [x0,x_safe)
# the kanji (neutral silver, r~=g~=b) and LITE's L (warm orange/brown,
# r-b>=warm) interleave, so in the strip only WARM *and BRIGHT* pixels are
# erased -- LITE's orange body/bevel is bright, while the kanji's dark border
# is warm-ish only from anti-alias (e.g. rgb(17,0,0)) and must be kept, else it
# leaves a transparent hole in the outline. Measured from the orange LITE glyph
# mass (x366..426, y72..106).
# box = (x0, y0, x1, y1, x_safe, warm_threshold, bright_min)
TEX_ERASE_BOXES = {
    "hd_sml_title.tex": [(364, 68, 427, 108, 367, 6, 100)],
}


# --- tool self-provisioning (install-where-used) -----------------------------
# ffmpeg (audio transcode, --web/--ogg-audio) and fontTools (CJK font subset,
# --web/--subset-font) are external tools this script needs. Install them
# here, at the point of use, rather than in build.sh -- so a direct `python3
# stage-assets.py` (IDE, bare `cmake --build`) provisions the same tools CI
# does. Idempotent: skip if already present. Auto-install only when we can
# (root + apt-get, i.e. inside the emscripten/emsdk container); otherwise
# raise a clear, actionable error. Pillow is self-provisioned unconditionally
# (both host + web need it for texture WebP encode) via pip.
def _can_apt_install():
    """True if we can apt-get install: running as root with apt-get on PATH."""
    if shutil.which("apt-get") is None:
        return False
    geteuid = getattr(os, "geteuid", None)
    return geteuid is not None and geteuid() == 0


def _apt_install(pkg):
    print("[stage-assets] installing {} via apt-get".format(pkg))
    if subprocess.run(["apt-get", "update", "-qq"]).returncode != 0:
        raise RuntimeError("apt-get update failed while installing {}".format(pkg))
    if subprocess.run(["apt-get", "install", "-y", "-qq", pkg]).returncode != 0:
        raise RuntimeError("apt-get install {} failed".format(pkg))


def _ensure_pillow():
    try:
        import PIL  # noqa: F401
        return
    except ImportError:
        pass
    # Inside the emscripten/emsdk container the Python is PEP 668
    # externally-managed, so `pip install` is refused -- install via apt
    # (python3-pil), same as ensure_fonttools. On a bare host (MSYS2/Windows,
    # not externally-managed) fall back to pip.
    if _can_apt_install():
        _apt_install("python3-pil")
    else:
        print("[stage-assets] Pillow not found, installing via pip")
        proc = subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", "pillow"])
        if proc.returncode != 0:
            print("[stage-assets] ERROR: pip install pillow failed", file=sys.stderr)
            sys.exit(1)
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("[stage-assets] ERROR: pillow installed but still not importable", file=sys.stderr)
        sys.exit(1)


def ensure_ffmpeg():
    """ffmpeg (Debian package bundles libvorbis) must be on PATH."""
    if shutil.which("ffmpeg") is not None:
        return
    if _can_apt_install():
        _apt_install("ffmpeg")
        if shutil.which("ffmpeg") is not None:
            return
    raise RuntimeError(
        "ffmpeg not found on PATH and cannot auto-install (needs root + apt-get, "
        "i.e. inside the emscripten/emsdk container). Install it: "
        "apt-get install -y ffmpeg")


def ensure_fonttools():
    """Try to make the fontTools module (apt `fonttools`) importable. The
    image's Python is PEP 668 externally-managed, so install via apt, not pip.

    Returns True if fontTools is importable afterwards, False otherwise.
    Unlike ensure_ffmpeg, this never raises -- the font subset is a
    size-optimization, not a hard requirement (see subset_cjk_font's
    graceful-skip-to-verbatim-copy fallback), so a missing/uninstallable
    fontTools must not fail the whole asset stage."""
    try:
        import fontTools  # noqa: F401
        return True
    except ImportError:
        pass
    import importlib
    if _can_apt_install():
        # PEP 668 externally-managed container Python -> apt, not pip.
        try:
            _apt_install("fonttools")
        except RuntimeError as e:
            print("[stage-assets] WARNING: {}".format(e), file=sys.stderr)
    else:
        # Bare host (native emsdk on Windows/MSYS2, not externally-managed) -> pip.
        print("[stage-assets] fontTools not found, installing via pip")
        proc = subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", "fonttools"])
        if proc.returncode != 0:
            print("[stage-assets] WARNING: pip install fonttools failed", file=sys.stderr)
    importlib.invalidate_caches()
    try:
        import fontTools  # noqa: F401
        return True
    except ImportError:
        pass
    print("[stage-assets] WARNING: fontTools not found and could not be "
          "auto-installed (apt-get install -y fonttools, or pip install "
          "fonttools) -- shipping the full unsubsetted font instead.",
          file=sys.stderr)
    return False


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


def is_webp_file(path):
    """True if path already holds WebP bytes (RIFF....WEBP magic)."""
    try:
        with open(path, "rb") as f:
            head = f.read(12)
    except OSError:
        return False
    return len(head) >= 12 and head[0:4] == b"RIFF" and head[8:12] == b"WEBP"


def needs_tex_transcode(src_path, dst_tex):
    if not os.path.isfile(dst_tex):
        return True
    if not is_webp_file(dst_tex):
        return True  # staged as verbatim/other before; redo
    return os.path.getmtime(dst_tex) < os.path.getmtime(src_path)


def encode_webp(rgba_bytes, w, h, dst_tex):
    """Encode flat RGBA8888 to WebP via Pillow and atomically write dst_tex
    (same .tex name, WebP bytes)."""
    _ensure_pillow()
    from PIL import Image
    img = Image.frombuffer("RGBA", (w, h), bytes(rgba_bytes), "raw", "RGBA", 0, 1)
    tmp = dst_tex + ".tmp.webp"
    img.save(tmp, format="WEBP", quality=int(WEBP_QUALITY), lossless=WEBP_LOSSLESS)
    os.replace(tmp, dst_tex)


def erase_boxes_rgba(rgba, w, h, boxes):
    """Zero the alpha channel of the LITE wordmark inside each box of a flat
    top-left-origin RGBA8888 buffer (in place).
    Box = (x0,y0,x1,y1,x_safe,warm,bright_min): inclusive bounds; at x >= x_safe
    every opaque pixel is LITE (full erase); in the overlap strip [x0, x_safe)
    only pixels that are both WARM (red-minus-blue >= warm, i.e. LITE's orange)
    and BRIGHT (max channel >= bright_min) are erased, so the neutral NINJA
    kanji AND its dark (warm-ish only from anti-alias) outline both survive."""
    for (x0, y0, x1, y1, x_safe, warm, bright_min) in boxes:
        x0 = max(0, x0); y0 = max(0, y0)
        x1 = min(w - 1, x1); y1 = min(h - 1, y1)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                i = (y * w + x) * 4
                r, g, b = rgba[i], rgba[i + 1], rgba[i + 2]
                if x >= x_safe or ((r - b) >= warm and max(r, g, b) >= bright_min):
                    rgba[i + 3] = 0


def transcode_tex_file(src_path, dst_tex, stats):
    src_size = os.path.getsize(src_path)
    stats["tex_src_bytes"] += src_size

    # Files with a branding-erase box always re-transcode (bypass the mtime
    # skip) so the hole is (re)applied even when the staged copy looks current.
    erase_boxes = TEX_ERASE_BOXES.get(os.path.basename(src_path).lower())

    if erase_boxes is None and not needs_tex_transcode(src_path, dst_tex):
        stats["tex_skipped"] += 1
        stats["tex_webp_bytes"] += os.path.getsize(dst_tex)
        return

    with open(src_path, "rb") as f:
        raw = f.read()

    parsed = tex_decoder.parse_tex1(raw)
    if parsed is None:
        # Not a transcodable Tex1 (Tex2/Tex3/DDS/PVRTC/unknown) -- copy verbatim.
        copy_if_different(src_path, dst_tex)
        stats["tex_copied_verbatim"] += 1
        stats["tex_webp_bytes"] += os.path.getsize(dst_tex)
        return

    fmt, w, h, body = parsed
    rgba = tex_decoder.unpack_tex1_to_rgba(fmt, w, h, body)
    if erase_boxes:
        rgba = bytearray(rgba)
        erase_boxes_rgba(rgba, w, h, erase_boxes)
    encode_webp(rgba, w, h, dst_tex)
    stats["tex_transcoded"] += 1
    stats["tex_webp_bytes"] += os.path.getsize(dst_tex)


# GXT1 encode (tiling + container) lives in tools/lib/gx_encoder.py, shared
# with its --selftest (registered as the `gx_encoder` ctest case).


def transcode_tex_file_wii(src_path, dst_tex, stats):
    """--wii: decode a transcodable Tex1 .tex to RGBA8888 and re-encode as a
    pre-tiled GXT1, preserving the source bit-depth via gx_encoder.TEX1_TO_GX
    (RGBA8888->GX_TF_RGBA8, RGB565/RGB888->GX_TF_RGB565, RGBA5551/RGBA4444->
    GX_TF_RGB5A3). Non-transcodable .tex copy verbatim. Incremental: an
    up-to-date (mtime) staged file whose magic confirms a previous GXT1
    encode is skipped; mtime alone can't prove it -- an earlier verbatim
    copy2() preserved the source's mtime while leaving raw Tex1 bytes."""
    erase_boxes = TEX_ERASE_BOXES.get(os.path.basename(src_path).lower())

    if erase_boxes is None and os.path.isfile(dst_tex) and \
            os.path.getmtime(dst_tex) >= os.path.getmtime(src_path):
        with open(dst_tex, "rb") as f:
            head = f.read(9)
        if len(head) == 9 and head[:4] == b"GXT1":
            gx = head[8]
            stats["gx_counts"][gx] = stats["gx_counts"].get(gx, 0) + 1
            stats["tex_skipped"] += 1
            return
        # Staged bytes aren't GXT1: either a verbatim copy of a
        # non-transcodable source (re-verified below) or a stale pre-GXT1
        # verbatim Tex1 from an older --wii run -- re-derive from source.

    with open(src_path, "rb") as f:
        raw = f.read()

    parsed = tex_decoder.parse_tex1(raw)
    if parsed is None:
        # Not a transcodable Tex1 (Tex2/Tex3/DDS/PVRTC/unknown) -- copy
        # verbatim as before (these don't load on Wii anyway).
        copy_if_different(src_path, dst_tex)
        stats["tex_verbatim"] += 1
        return

    fmt, w, h, body = parsed
    gx = gx_encoder.TEX1_TO_GX[fmt]  # keys == TEX_KNOWN_FORMATS by contract
    rgba = tex_decoder.unpack_tex1_to_rgba(fmt, w, h, body)
    if erase_boxes:
        # Same "Lite" branding erase as the host/web WebP path (see
        # TEX_ERASE_BOXES) -- keeps the staged trees consistent even though
        # the only erase target is hd_* art, which Wii doesn't load.
        rgba = bytearray(rgba)
        erase_boxes_rgba(rgba, w, h, erase_boxes)
    blob = gx_encoder.encode_gxtx(rgba, w, h, gx)
    tmp = dst_tex + ".tmp.gxtx"
    with open(tmp, "wb") as f:
        f.write(blob)
    os.replace(tmp, dst_tex)
    stats["gx_counts"][gx] = stats["gx_counts"].get(gx, 0) + 1
    stats["tex_transcoded"] += 1


def merge_widget_textures(repo_root, dst_root, stats, is_wii=False):
    """Copy pre-generated assets/ui-widgets/generated/*.tex (already WebP,
    lossless -- see svg-to-webp.mjs) into <dst_root>/textures/, overwriting.
    Non-fatal if the dir is missing/empty (widgets fall back to placeholder
    art).

    Wii (is_wii=True): the Wii build has no WebP decoder (ReadWebP compiled
    out under FRUIT_PLATFORM_WII), so the WebP .tex files are ignored;
    instead each BASE widget's raw-RGBA sidecar <name>.rgba (written by
    svg-to-webp.mjs next to the WebP: "RRAW" + u16le w/h + w*h*4 RGBA8,
    little-endian) is read with pure stdlib (no Pillow -- the msys2 Python
    the Wii build uses has no pip) and re-encoded as a pre-tiled GX RGBA8
    "GXT1" container under the same <name>.tex basename (see
    gx_encoder.encode_gxtx). hd_* files are skipped -- Wii HD is disabled
    (FN_ENABLE_HD_ASSETS=OFF)."""
    src_dir = os.path.join(repo_root, WIDGET_TEX_RELDIR)
    dst_dir = os.path.join(dst_root, "textures")

    if not os.path.isdir(src_dir):
        print("[stage-assets] WARNING: {} not found -- widget textures not "
              "generated yet (needs node on PATH; see tools/assets/"
              "svg-to-webp.mjs run by fn_asset_staging); widgets will fall "
              "back to placeholder art".format(src_dir))
        return

    # Wii consumes the .rgba sidecars (no WebP decoder / no Pillow);
    # host/web copy the WebP .tex verbatim.
    ext = ".rgba" if is_wii else ".tex"
    names = [n for n in os.listdir(src_dir) if n.lower().endswith(ext)]
    if not names:
        print("[stage-assets] WARNING: {} has no {} files -- widgets will "
              "fall back to placeholder art (run a host/web build first so "
              "svg-to-webp.mjs generates them)".format(src_dir, ext))
        return

    os.makedirs(dst_dir, exist_ok=True)

    if not is_wii:
        for name in names:
            shutil.copy2(os.path.join(src_dir, name), os.path.join(dst_dir, name))
            stats["widget_tex_copied"] += 1
        return

    # Wii: read RRAW sidecar -> tile -> GXT1. Pure stdlib.
    for name in names:
        if name.lower().startswith("hd_"):
            continue  # Wii HD disabled; don't ship 2x art (no hd_ sidecars are emitted anyway)
        src_path = os.path.join(src_dir, name)
        dst_path = os.path.join(dst_dir, name[:-len(".rgba")] + ".tex")
        if os.path.isfile(dst_path) and \
                os.path.getmtime(dst_path) >= os.path.getmtime(src_path):
            # mtime alone can't prove the staged copy is GXT1: an earlier
            # verbatim copy2() preserved the source's mtime while leaving WebP
            # bytes. Only skip when the magic confirms a previous encode.
            with open(dst_path, "rb") as f:
                if f.read(4) == b"GXT1":
                    stats["widget_tex_copied"] += 1
                    continue
        with open(src_path, "rb") as f:
            blob_in = f.read()
        magic, w, h = struct.unpack("<4sHH", blob_in[:8])
        if magic != b"RRAW" or len(blob_in) != 8 + w * h * 4:
            raise ValueError("{}: bad RRAW sidecar (magic {!r}, {}x{}, {} bytes; "
                             "want {} bytes) -- regenerate via svg-to-webp.mjs"
                             .format(src_path, magic, w, h, len(blob_in), 8 + w * h * 4))
        # Widgets have no Tex1 source (RRAW RGBA sidecar only), so there is
        # no source bit-depth to preserve: always GX_TF_RGBA8.
        blob = gx_encoder.encode_gxtx(blob_in[8:], w, h, gx_encoder.GX_TF_RGBA8)
        tmp = dst_path + ".tmp.gxtx"
        with open(tmp, "wb") as f:
            f.write(blob)
        os.replace(tmp, dst_path)
        stats["widget_tex_copied"] += 1


def stage_wii_prebaked_fonts(repo_root, dst_root, stats):
    """Invoke tools/wii/bake-fonts.py to produce the prebaked TTF glyph
    atlases (task #51 -- see tools/wii/prebaked-font-format.md). Non-fatal:
    prints a warning and returns if either input is missing (freetype-py not
    installed, or the tmp/prebake/ plan hasn't been generated yet) -- same
    contract as merge_widget_textures' missing-node case."""
    stats["prebaked_fonts_baked"] = 0
    stats["prebaked_fonts_skipped_reason"] = None

    plan_dir = os.path.join(repo_root, WII_PREBAKE_PLAN_RELDIR)
    plan_path = os.path.join(plan_dir, "bake_plan.json")
    if not os.path.isfile(plan_path):
        reason = "{} not found (run the #51 offline planning pass first)".format(plan_path)
        stats["prebaked_fonts_skipped_reason"] = reason
        print("[stage-assets] WARNING: prebaked fonts skipped -- {} -- Wii will "
              "fall back to runtime rasterization for this build".format(reason))
        return

    try:
        import freetype  # noqa: F401
    except ImportError:
        reason = "freetype-py not installed (pip install freetype-py)"
        stats["prebaked_fonts_skipped_reason"] = reason
        print("[stage-assets] WARNING: prebaked fonts skipped -- {} -- Wii will "
              "fall back to runtime rasterization for this build".format(reason))
        return

    font_dir = os.path.join(repo_root, "FruitNinjaBada", "Data", FONT_RELPATH)
    out_dir = os.path.join(dst_root, WII_PREBAKED_FONTS_RELDIR)

    # bake-fonts.py's filename has a dash (not a valid module identifier), so
    # import it by file path rather than by module name.
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "wii_bake_fonts", os.path.join(repo_root, "tools", "wii", "bake-fonts.py"))
    bake_fonts = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bake_fonts)

    plan = bake_fonts.load_bake_plan(plan_dir)
    os.makedirs(out_dir, exist_ok=True)
    report = {}
    for lang in sorted(plan["plan"].keys()):
        for size in plan["canonical_sizes"]:
            bake_fonts.bake_one(plan, plan_dir, font_dir, out_dir, lang, size, report)
            stats["prebaked_fonts_baked"] += 1

    total_bytes = sum(e["bytes"] for lang_r in report.values() for e in lang_r.values())
    total_missing = sum(e["missing"] for lang_r in report.values() for e in lang_r.values())
    stats["prebaked_fonts_bytes"] = total_bytes
    stats["prebaked_fonts_missing"] = total_missing


# --- Font subsetting (web only): CJK/shared TTF subset by used code points --
FONT_RELPATH = "fontstruetype"
CJK_FONT_FILENAME = "gangofchinese.ttf"
STRINGTABLES_RELPATH = "stringtables"
STRINGTABLE_HEADER_NAME = "translations_header.str"
# arabic.ttf renders that language (own font, copied verbatim); the header
# file holds ASCII lookup keys, never user-visible text.
STRINGTABLE_EXCLUDE = {"translations_arabic.str", STRINGTABLE_HEADER_NAME}
ASCII_PRINTABLE = range(0x20, 0x7F)

# StringEntry body-file layout (see docs/engine/localisation.md /
# Mortar::StringTable::LoadLanguage in src/engine/util/StringTable.cpp):
#   FileHeader wrapper: magic(4) + token[64] + blob_byte_size(4) @0x44 + count(4) @0x48
#   StringEntry[count] @ 0x4c, 12 bytes each: str_offset(4), len(4), len2(4)
#   str_blob (null-terminated UTF-8) starts right after the entry array.
STR_ENTRIES_OFFSET = 0x4c
STR_ENTRY_SIZE = 12

# Must stay in sync with kLanguageNames in src/screens/SettingsScreen.cpp --
# the SettingsScreen language ComboBox renders these hardcoded native names
# with gangofchinese.ttf, but they aren't sourced from any translations_*.str
# file, so gather_cjk_font_charset() would otherwise drop their glyphs from
# the web subset. "Arabic" (index 20) is the ASCII fallback name (that font
# has no Arabic glyphs), so it needs no extra codepoints here.
NATIVE_LANG_NAMES = [
    "English (US)", "English (UK)", "Français", "Español", "Deutsch", "Italiano",
    "Nederlands", "Svenska", "Dansk", "Norsk", "Suomi", "한국어",
    "日本語", "中文", "繁體中文", "Español (LA)", "Polski",
    "Português (PT)", "Português (BR)", "Русский", "Arabic",
]


def parse_str_body_codepoints(path):
    """Parse a translations_<lang>.str BODY file and return the set of Unicode
    code points appearing in its null-terminated UTF-8 string blob. Returns an
    empty set if the file is too short / doesn't validate as this format."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < STR_ENTRIES_OFFSET + 4:
        return set()
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != 1:
        return set()
    count = struct.unpack_from("<I", data, 0x48)[0]
    str_blob_off = STR_ENTRIES_OFFSET + count * STR_ENTRY_SIZE

    codepoints = set()
    for i in range(count):
        off = STR_ENTRIES_OFFSET + i * STR_ENTRY_SIZE
        if off + 4 > len(data):
            break
        str_offset = struct.unpack_from("<I", data, off)[0]
        start = str_blob_off + str_offset
        if start >= len(data):
            continue
        end = data.find(b"\x00", start)
        if end < 0:
            continue
        try:
            text = data[start:end].decode("utf-8")
        except UnicodeDecodeError:
            continue  # not expected on real data; skip rather than guess
        codepoints.update(ord(c) for c in text)
    return codepoints


def gather_cjk_font_charset(stringtables_dir):
    """Union of code points rendered with gangofchinese.ttf: printable ASCII
    plus every character in every translations_<lang>.str BODY file except
    arabic (own font) and header (ASCII keys, never rendered).
    Returns (codepoints_set, [source .str paths used for the union])."""
    codepoints = set(ASCII_PRINTABLE)
    sources = []
    if not os.path.isdir(stringtables_dir):
        return codepoints, sources
    for name in sorted(os.listdir(stringtables_dir)):
        lower = name.lower()
        if not (lower.startswith("translations_") and lower.endswith(".str")):
            continue
        if lower in STRINGTABLE_EXCLUDE:
            continue
        path = os.path.join(stringtables_dir, name)
        sources.append(path)
        codepoints |= parse_str_body_codepoints(path)
    for name in NATIVE_LANG_NAMES:
        codepoints.update(ord(c) for c in name)
    return codepoints, sources


def needs_font_subset(src_ttf, dst_ttf, charset_sources):
    if not os.path.isfile(dst_ttf):
        return True
    dst_mtime = os.path.getmtime(dst_ttf)
    if dst_mtime < os.path.getmtime(src_ttf):
        return True
    for p in charset_sources:
        if dst_mtime < os.path.getmtime(p):
            return True
    return False


def run_pyftsubset(src_ttf, dst_ttf, codepoints, charset_txt_path):
    """Write the used-character text file and invoke fontTools' subsetter via
    `python -m fontTools.subset` (not the pyftsubset console-script entry
    point, so this works regardless of whether pip put it on PATH)."""
    chars = "".join(chr(cp) for cp in sorted(codepoints))
    with open(charset_txt_path, "w", encoding="utf-8") as f:
        f.write(chars)

    tmp = dst_ttf + ".tmp.ttf"
    cmd = [
        sys.executable, "-m", "fontTools.subset", src_ttf,
        "--text-file=" + charset_txt_path,
        "--output-file=" + tmp,
        "--no-hinting",
        "--desubroutinize",
    ]
    proc = subprocess.run(cmd, capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError("pyftsubset failed ({}): {}".format(
            proc.returncode, proc.stderr.decode("utf-8", "replace")))
    os.replace(tmp, dst_ttf)


def count_glyphs(ttf_path):
    from fontTools.ttLib import TTFont
    font = TTFont(ttf_path, lazy=True)
    n = len(font.getGlyphOrder())
    font.close()
    return n


def subset_cjk_font(src_path, dst_path, codepoints, charset_sources, charset_txt_path,
                     stats, fonttools_available):
    """Subset gangofchinese.ttf to `codepoints` via pyftsubset. If fontTools
    isn't available (ensure_fonttools couldn't import/install it), gracefully
    degrades to a verbatim copy of the full font -- same non-fatal
    "missing tool -> skip, don't fail the stage" contract as the widget-art
    node/Pillow cases (see module docstring)."""
    stats["font_src_bytes"] = os.path.getsize(src_path)
    if not fonttools_available:
        if copy_if_different(src_path, dst_path):
            stats["font_action"] = "copied verbatim (fontTools unavailable)"
        else:
            stats["font_action"] = "skipped (verbatim, up to date)"
        stats["font_out_bytes"] = os.path.getsize(dst_path)
        stats["font_glyph_count"] = 0
        stats["font_codepoint_count"] = len(codepoints)
        return
    if needs_font_subset(src_path, dst_path, charset_sources):
        run_pyftsubset(src_path, dst_path, codepoints, charset_txt_path)
        stats["font_action"] = "subsetted"
    else:
        stats["font_action"] = "skipped (up to date)"
    stats["font_out_bytes"] = os.path.getsize(dst_path)
    stats["font_glyph_count"] = count_glyphs(dst_path)
    stats["font_codepoint_count"] = len(codepoints)


def copy_if_different(src_path, dst_path):
    if os.path.isfile(dst_path):
        s_src = os.stat(src_path)
        s_dst = os.stat(dst_path)
        if s_src.st_size == s_dst.st_size and s_dst.st_mtime >= s_src.st_mtime:
            return False
    shutil.copy2(src_path, dst_path)
    return True


def stage_tree_raw(src_root, dst_root):
    """--wii mode: recursive mirror. Transcodable Tex1 *.tex are re-encoded
    as pre-tiled GXT1 (bit-depth-preserving GX format -- see
    transcode_tex_file_wii); everything else (raw .wav.pcm audio, fonts,
    XML, non-Tex1 .tex) copies verbatim, EXCEPT the runtime TTF sources
    under fontstruetype/ (task #54): the Wii build no longer opens a .ttf
    at runtime at all (FontCacheObjectTTF is FRUIT_PLATFORM_WII-guarded to
    read the offline-baked FNT3 atlas, stage_wii_prebaked_fonts below,
    instead -- see src/engine/render/FontCacheObjectTTF.cpp). Shipping
    gangofchinese.ttf (~5MB) + arabic.ttf to the SD card would be dead
    weight; the .ttf files stay in the repo as BAKE-TIME-ONLY inputs to
    tools/wii/bake-fonts.py. Pure stdlib -- no Pillow/ffmpeg/fontTools
    dependency."""
    stats = {"other_copied": 0, "other_skipped": 0, "widget_tex_copied": 0,
             "tex_transcoded": 0, "tex_skipped": 0, "tex_verbatim": 0,
             "ttf_excluded": 0,
             "gx_counts": {}}
    for root, _dirs, files in os.walk(src_root):
        rel_dir = os.path.relpath(root, src_root)
        dst_dir = os.path.join(dst_root, rel_dir) if rel_dir != "." else dst_root
        rel_norm = rel_dir.replace("\\", "/")
        is_font_dir = rel_norm == FONT_RELPATH
        os.makedirs(dst_dir, exist_ok=True)
        for name in files:
            src_path = os.path.join(root, name)
            dst_path = os.path.join(dst_dir, name)
            if is_font_dir and name.lower().endswith(".ttf"):
                stats["ttf_excluded"] += 1
                continue
            if name.lower().endswith(".tex"):
                transcode_tex_file_wii(src_path, dst_path, stats)
            elif copy_if_different(src_path, dst_path):
                stats["other_copied"] += 1
            else:
                stats["other_skipped"] += 1
    return stats


def stage_tree(src_root, dst_root, is_web, is_ogg_audio, is_subset_font, font_codepoints,
               font_charset_sources, font_charset_txt_path, fonttools_available):
    stats = {
        "sfx_transcoded": 0,
        "sfx_skipped": 0,
        "other_copied": 0,
        "other_skipped": 0,
        "tex_transcoded": 0,
        "tex_copied_verbatim": 0,
        "tex_skipped": 0,
        "tex_src_bytes": 0,
        "tex_webp_bytes": 0,
        "widget_tex_copied": 0,
        "font_action": None,
        "font_src_bytes": 0,
        "font_out_bytes": 0,
        "font_glyph_count": 0,
        "font_codepoint_count": 0,
    }
    loops = {}
    sample_loops = {}

    for root, _dirs, files in os.walk(src_root):
        rel_dir = os.path.relpath(root, src_root)
        dst_dir = os.path.join(dst_root, rel_dir) if rel_dir != "." else dst_root
        os.makedirs(dst_dir, exist_ok=True)

        rel_norm = rel_dir.replace("\\", "/")
        is_sfx_dir = rel_norm == SFX_RELPATH or rel_norm.startswith(SFX_RELPATH + "/")
        is_font_dir = rel_norm == FONT_RELPATH

        for name in files:
            src_path = os.path.join(root, name)

            if is_ogg_audio and is_sfx_dir and name.lower().endswith(".wav.pcm"):
                short = sfx_name_from_filename(name)
                dst_ogg = os.path.join(dst_dir, short + ".ogg")
                transcode_sfx_file(src_path, dst_ogg, stats)

                # Collect loop metadata straight from the header.
                _k, rate, _bd, _c, loop_start = read_header(src_path)
                if loop_start != 0 and rate > 0:
                    loops[short] = float(loop_start) / float(rate)
                if loop_start != 0:
                    sample_loops[short] = int(loop_start)
            elif name.lower().endswith(".tex"):
                dst_tex = os.path.join(dst_dir, name)
                transcode_tex_file(src_path, dst_tex, stats)
            elif is_subset_font and is_font_dir and name.lower() == CJK_FONT_FILENAME:
                dst_font = os.path.join(dst_dir, name)
                subset_cjk_font(src_path, dst_font, font_codepoints,
                                 font_charset_sources, font_charset_txt_path, stats,
                                 fonttools_available)
            else:
                # Host build: *.wav.pcm (not staged above since is_ogg_audio
                # is False) falls through here and copies verbatim, same as
                # any other file -- the host SDL mixer plays raw PCM unchanged.
                dst_path = os.path.join(dst_dir, name)
                if copy_if_different(src_path, dst_path):
                    stats["other_copied"] += 1
                else:
                    stats["other_skipped"] += 1

    return stats, loops, sample_loops


def sweep_stale_wii_ttf(dst_root):
    """Remove any stale fontstruetype/*.ttf left in a Wii staging dir from a
    prior (pre-task-#54) run -- stage_tree_raw no longer copies them (see its
    docstring), but an existing incrementally-staged dir would otherwise keep
    the old verbatim copy around indefinitely (stage_tree_raw only copies/
    skips, never deletes). Wii-only, mirrors sweep_stale_pcm's pattern."""
    removed = 0
    dst_font_dir = os.path.join(dst_root, FONT_RELPATH)
    if not os.path.isdir(dst_font_dir):
        return removed
    for name in os.listdir(dst_font_dir):
        if name.lower().endswith(".ttf"):
            try:
                os.remove(os.path.join(dst_font_dir, name))
                removed += 1
            except OSError:
                pass
    return removed


def sweep_stale_pcm(dst_root):
    """Remove any stale sfx/*.wav.pcm left in staging by the retired resampler.
    Web builds --preload-file the whole staging Data dir, so a lingering
    .wav.pcm would re-bloat .data; other --ogg-audio targets (e.g. webOS)
    likewise ship .ogg instead of .wav.pcm. Only .ogg + non-pcm files should
    remain under sfx/. Plain host builds (no --ogg-audio) intentionally keep
    .wav.pcm (see stage_tree), so this sweep only runs when is_ogg_audio."""
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


def gen_loop_table_cpp(sample_loops, out_path):
    """Emit the generated C++ loop-lookup source at out_path from
    {name: loopStart_samples} (name = lowercased basename, no extension;
    only entries with loopStart > 0). Deterministic (sorted by name) and
    written only if content changed, so an unchanged loop set doesn't
    trigger a rebuild."""
    lines = [LOOP_TABLE_HEADER]
    for name in sorted(sample_loops.keys()):
        loop_start = sample_loops[name]
        if loop_start <= 0:
            continue
        lines.append("    {{ \"{}\", {}u }},\n".format(name, loop_start))
    lines.append(LOOP_TABLE_FOOTER)
    content = "".join(lines)

    if os.path.isfile(out_path):
        with open(out_path, "r") as f:
            if f.read() == content:
                return False
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    tmp = out_path + ".tmp"
    with open(tmp, "w") as f:
        f.write(content)
    os.replace(tmp, out_path)
    return True


def main():
    if len(sys.argv) < 3:
        print("Usage: stage-assets.py <repo_root> <out_staging_data_dir> "
              "[--web|--wii] [--subset-font] [--ogg-audio] "
              "[--gen-loop-table <path>]", file=sys.stderr)
        sys.exit(1)

    repo_root = sys.argv[1]
    dst_root = sys.argv[2]
    extra_args = sys.argv[3:]
    is_web = "--web" in extra_args
    is_wii = "--wii" in extra_args
    # --web implies --subset-font (unchanged web behaviour); webOS (or any
    # other non-web/non-wii target) can request just the font subset without
    # the Ogg/WebP-audio transcoding --web also brings.
    is_subset_font = is_web or "--subset-font" in extra_args
    # --web implies --ogg-audio (unchanged web behaviour); other targets
    # (e.g. native/SDL builds wanting Ogg to ship a generated C++ loop table
    # instead of raw .wav.pcm) can request just the Ogg transcode without the
    # rest of --web (font subset, JSON loop file is still emitted -- see
    # module docstring -- only under --web itself).
    is_ogg_audio = is_web or "--ogg-audio" in extra_args
    gen_loop_table_path = None
    if "--gen-loop-table" in extra_args:
        idx = extra_args.index("--gen-loop-table")
        if idx + 1 < len(extra_args):
            gen_loop_table_path = extra_args[idx + 1]
        else:
            print("ERROR: --gen-loop-table requires a path argument", file=sys.stderr)
            sys.exit(1)

    src_root = os.path.join(repo_root, "FruitNinjaBada", "Data")

    if not os.path.isdir(src_root):
        print("ERROR: source Data dir not found: {}".format(src_root), file=sys.stderr)
        sys.exit(1)

    os.makedirs(dst_root, exist_ok=True)

    if is_wii:
        # Pre-tiled GXT1 texture mirror + verbatim copy of everything else;
        # no ffmpeg/fontTools/node/Pillow dependency (pure stdlib). Widget
        # art is re-encoded to GXT1 from the .rgba sidecars. See the module
        # docstring's "WII MODE" section.
        print("[stage-assets] staging {} -> {} (wii, pre-tiled GX)".format(src_root, dst_root))
        stats = stage_tree_raw(src_root, dst_root)
        merge_widget_textures(repo_root, dst_root, stats, is_wii=True)
        gc = stats["gx_counts"]
        print("[stage-assets] textures: {} transcoded to GXT1, {} unchanged (skipped), "
              "{} verbatim (non-Tex1)".format(
                  stats["tex_transcoded"], stats["tex_skipped"], stats["tex_verbatim"]))
        print("[stage-assets] GX: {} RGBA8, {} RGB5A3, {} RGB565, {} verbatim".format(
            gc.get(gx_encoder.GX_TF_RGBA8, 0), gc.get(gx_encoder.GX_TF_RGB5A3, 0),
            gc.get(gx_encoder.GX_TF_RGB565, 0), stats["tex_verbatim"]))
        print("[stage-assets] other assets: {} copied, {} unchanged (skipped)".format(
            stats["other_copied"], stats["other_skipped"]))
        print("[stage-assets] widget textures: {} staged as GXT1 from {}".format(
            stats["widget_tex_copied"], WIDGET_TEX_RELDIR))
        print("[stage-assets] runtime TTF sources excluded (bake-time-only, task #54): "
              "{} file(s) under {}/".format(stats["ttf_excluded"], FONT_RELPATH))
        removed_ttf = sweep_stale_wii_ttf(dst_root)
        if removed_ttf:
            print("[stage-assets] removed {} stale staged .ttf under {}/ "
                  "(pre-task-#54 leftovers)".format(removed_ttf, FONT_RELPATH))

        stage_wii_prebaked_fonts(repo_root, dst_root, stats)
        if stats["prebaked_fonts_baked"]:
            print("[stage-assets] prebaked fonts: {} (lang,size) atlases baked, "
                  "{:.1f} MB, {} missing-glyph codepoints -> {}".format(
                      stats["prebaked_fonts_baked"],
                      stats.get("prebaked_fonts_bytes", 0) / (1024.0 * 1024.0),
                      stats.get("prebaked_fonts_missing", 0), WII_PREBAKED_FONTS_RELDIR))
        return

    # Self-provision the external tools (install-where-used). ffmpeg is
    # mandatory for --web/--ogg-audio (raises a clear error if missing/
    # uninstallable); Pillow is needed regardless (texture transcode runs for
    # host too). fontTools is best-effort (see ensure_fonttools) -- a missing
    # fontTools degrades the font step to a verbatim copy, it never aborts
    # the stage.
    _ensure_pillow()
    if is_ogg_audio:
        ensure_ffmpeg()
    fonttools_available = ensure_fonttools() if is_subset_font else False

    font_codepoints, font_charset_sources = set(), []
    font_charset_txt_path = os.path.join(os.path.dirname(dst_root), "gangofchinese-charset.txt")
    if is_subset_font:
        stringtables_dir = os.path.join(src_root, STRINGTABLES_RELPATH)
        font_codepoints, font_charset_sources = gather_cjk_font_charset(stringtables_dir)
        # Sibling of dst_root (not inside Data/) so it never gets --preload-file'd.

    mode = "web" if is_web else "host"
    if is_subset_font and not is_web:
        mode += "+subset-font"
    if is_ogg_audio and not is_web:
        mode += "+ogg-audio"
    print("[stage-assets] staging {} -> {} ({})".format(src_root, dst_root, mode))
    stats, loops, sample_loops = stage_tree(
        src_root, dst_root, is_web, is_ogg_audio, is_subset_font, font_codepoints,
        font_charset_sources, font_charset_txt_path, fonttools_available)

    merge_widget_textures(repo_root, dst_root, stats)

    if is_web:
        # Emit loop metadata JSON next to the .ogg files (web-JS-only consumer).
        loop_json_path = os.path.join(dst_root, SFX_RELPATH, LOOP_JSON_NAME)
        os.makedirs(os.path.dirname(loop_json_path), exist_ok=True)
        with open(loop_json_path, "w") as f:
            json.dump(loops, f, indent=0, sort_keys=True)

    if is_ogg_audio:
        removed = sweep_stale_pcm(dst_root)

        print("[stage-assets] sfx: {} transcoded, {} unchanged (skipped); {} loop points".format(
            stats["sfx_transcoded"], stats["sfx_skipped"], len(loops)))
        if removed:
            print("[stage-assets] swept {} stale .wav.pcm from staging sfx/".format(removed))

        if gen_loop_table_path:
            changed = gen_loop_table_cpp(sample_loops, gen_loop_table_path)
            print("[stage-assets] loop table: {} ({} entries) -> {}".format(
                "written" if changed else "unchanged (skipped)",
                len(sample_loops), gen_loop_table_path))
        else:
            print("[stage-assets] loop table: --gen-loop-table not passed, skipping C++ emit")

    src_mb = stats["tex_src_bytes"] / (1024.0 * 1024.0)
    webp_mb = stats["tex_webp_bytes"] / (1024.0 * 1024.0)
    ratio = (100.0 * webp_mb / src_mb) if src_mb > 0 else 0.0
    print("[stage-assets] textures: {} transcoded to WebP, {} verbatim, {} unchanged".format(
        stats["tex_transcoded"], stats["tex_copied_verbatim"], stats["tex_skipped"]))
    print("[stage-assets] texture bytes: {:.1f} MB source -> {:.1f} MB staged ({:.1f}% of original)".format(
        src_mb, webp_mb, ratio))
    print("[stage-assets] widget textures: {} merged from {}".format(
        stats["widget_tex_copied"], WIDGET_TEX_RELDIR))

    if is_subset_font and stats["font_action"]:
        font_src_mb = stats["font_src_bytes"] / (1024.0 * 1024.0)
        font_out_mb = stats["font_out_bytes"] / (1024.0 * 1024.0)
        print("[stage-assets] font {}: {} -- {} codepoints, {} glyphs, "
              "{:.2f} MB -> {:.2f} MB".format(
                  CJK_FONT_FILENAME, stats["font_action"],
                  stats["font_codepoint_count"], stats["font_glyph_count"],
                  font_src_mb, font_out_mb))

    print("[stage-assets] other assets: {} copied, {} unchanged (skipped)".format(
        stats["other_copied"], stats["other_skipped"]))


if __name__ == "__main__":
    main()
