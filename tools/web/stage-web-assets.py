#!/usr/bin/env python3
"""
tools/web/stage-web-assets.py -- Build-phase asset staging for the web target.

Usage:
    python3 tools/web/stage-web-assets.py <repo_root>

Mirrors FruitNinjaBada/Data into build/web-staging/Data. Two web-only
size optimisations happen during the mirror (the real FruitNinjaBada/Data files
are never modified; the staged copy is a build/ artifact):

  1. AUDIO: every sfx/*.wav.pcm is TRANSCODED to Ogg/Vorbis (sfx/<name>.ogg)
     exactly ONCE at build time; the source .wav.pcm is NOT copied into the
     staging tree (that would re-bloat the .data payload we are shrinking).

  2. TEXTURES (// Port specific: web compressed textures (libwebp)): every
     *.tex whose header parses as a Tex1 with a known pixel format is decoded to
     RGBA8888 and re-encoded as WebP, then written back under the SAME relative
     path + SAME .tex filename (WebP bytes inside a .tex-named file). The engine
     loader (Mortar::TextureFileFormat) dispatches by CONTENT, not extension: a
     WebP reader registered at g_readers[0] detects the RIFF/WEBP magic and
     decodes; real Tex1 .tex fall through to the Tex1 reader. So NO path,
     preload, or platform-branch change is needed -- only the file bytes shrink.
     Textures that do NOT parse as Tex1 (Tex2/Tex3/DDS/PVRTC/unknown format) are
     copied verbatim so their own readers still handle them.

  3. FONT (fontstruetype/gangofchinese.ttf, ~5 MB): SUBSET down to only the
     Unicode code points that appear in the loaded stringtables, via fonttools'
     pyftsubset. gangofchinese.ttf is the SHARED default TTF face for every
     NON-Arabic language (PreloadFontsTTF @0x0011c1fc picks arabic.ttf only when
     languageFlag == 0x14 "arabic"; every other language in kLanguageSuffix --
     english_us/uk, french, spanish, german, italian, dutch, swedish, danish,
     norwegian, finnish, korean, japanese, (traditional) chinese, latin spanish,
     polish, portuguese (pt/br), russian, fake debug language -- shares this one
     face; see src/game/PreloadFontsTTF.cpp, src/engine/util/StringTable.cpp).
     The web build ships one bundle for every language (the user can switch
     language at runtime), so the used-glyph set is the UNION of every
     translations_<lang>.str BODY file except translations_arabic.str (own font)
     and translations_header.str (ASCII lookup keys, never rendered) -- plus
     printable ASCII (digits/latin appear inside every language's strings too:
     mode names, scores). arabic.ttf (99 KB) and every other fontstruetype file
     are copied verbatim -- only gangofchinese.ttf is subsetted.
     Glyph lookup is plain FreeType FT_Get_Char_Index per code point (see
     FontCacheObjectTTF.cpp) -- no HarfBuzz shaping -- so a code-point-based
     subset (no OpenType layout table retention) renders identically.

Everything else in Data is copied through unchanged.

Why: the web build no longer runs the SDL software mixer. It uses the Web Audio
API backend (src/engine/audio/SoundManagerWebAudio.cpp), which lets the browser
decode Ogg/Vorbis and mix on its own audio thread -- off the JS main thread (no
GC underrun / crackle on a slow webOS TV) -- and compressed assets shrink the
.data payload from ~72 MB (uncompressed PCM) to a few MB. Desktop keeps the
faithful SDL mixer + raw .wav.pcm untouched.

The staged copy is a BUILD ARTIFACT under build/ (gitignored) -- the real
FruitNinjaBada/Data/*.wav.pcm files (extracted, binary-faithful assets) are
NEVER modified. CMakeLists.txt's EMSCRIPTEN --preload-file points at the staged
directory instead of the real Data dir; see the fn_web_asset_staging custom
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
    build/web-staging/Data/sfx/sfx-loops.json
mapping { "<name>": <loopStart_seconds> } for every sfx with loopStart != 0
(loopStart_seconds = loopStart / sampleRate). Keys are the lowercased bare name
(no extension), matching the case-folded lookup the JS backend performs (mirrors
the desktop ResolvePathCI fallback). The C++/JS backend consumes this to set the
Web Audio loop point (source.loop / loopStart / loopEnd) for looping sfx (e.g.
bomb-fuse) and music (music-menu).

Idempotent and incremental: an already-transcoded .ogg is skipped unless its
source .wav.pcm is newer; other files use size+mtime copy-if-different. Pure
Python stdlib + an ffmpeg binary on PATH.

This script self-provisions its two external tools (ffmpeg for audio/texture
transcode, fontTools for the CJK font subset): when either is missing AND it is
running as root with apt-get available (i.e. inside the emscripten/emsdk
container), it apt-get installs the tool; otherwise it raises a clear error
naming the tool. The image's Python is PEP 668 externally-managed, so fontTools
is installed via the apt `fonttools` package (not pip). The staged
gangofchinese.ttf subset is skipped unless it is older than the source .ttf or
any translations_*.str body file that feeds its charset.
"""

import array
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

# --- Texture transcoding (web only): Tex1 .tex -> WebP-in-.tex ---------------
# WEBP_QUALITY is the lossy quality 0..100 (higher = better/larger). If UI/text
# looks soft or shows compression artifacts, flip WEBP_LOSSLESS to True for
# pixel-exact (larger) output.
WEBP_QUALITY = "90"
WEBP_LOSSLESS = False
WEBP_COMPRESSION_LEVEL = "6"

TEX_HEADER_SIZE = 12  # Tex1 header: byte0 wLog2, byte1 hLog2, byte2 fmt, [3..11] pad

# Tex1 format byte -> bits-per-pixel, restricted to the formats this script can
# unpack to RGBA8888. Must match Mortar::TextureFileFormat::ReadTex1Format +
# Texture.cpp's GL upload (RGB888/RGBA8888/RGBA5551/RGBA4444/RGB565). Any other
# format byte (0x02..0x0e PVRTC/etc.) -> texture copied verbatim.
TEX_KNOWN_FORMATS = {
    0x00: 24,  # RGB888
    0x01: 32,  # RGBA8888
    0x0f: 16,  # RGBA5551
    0x10: 16,  # RGBA4444
    0x11: 16,  # RGB565
}

# Channel-expansion lookup tables (bit replication, matching the GL/ES upload
# expansion used on desktop): 5-bit -> 8-bit, 6-bit -> 8-bit, 4-bit -> 8-bit.
_EXP5 = tuple(((v << 3) | (v >> 2)) for v in range(32))
_EXP6 = tuple(((v << 2) | (v >> 4)) for v in range(64))
_EXP4 = tuple(((v << 4) | v) for v in range(16))


# --- tool self-provisioning (install-where-used) -----------------------------
# ffmpeg (audio + texture transcode) and fontTools (CJK font subset) are the two
# external tools this script needs. Install them here, at the point of use,
# rather than in build.sh -- so a direct `python3 stage-web-assets.py` (IDE, bare
# `cmake --build`) provisions the same tools CI does. Idempotent: skip if already
# present. Auto-install only when we can (root + apt-get, i.e. inside the
# emscripten/emsdk container); otherwise raise a clear, actionable error.
def _can_apt_install():
    """True if we can apt-get install: running as root with apt-get on PATH."""
    if shutil.which("apt-get") is None:
        return False
    geteuid = getattr(os, "geteuid", None)
    return geteuid is not None and geteuid() == 0


def _apt_install(pkg):
    print("[stage-web-assets] installing {} via apt-get".format(pkg))
    if subprocess.run(["apt-get", "update", "-qq"]).returncode != 0:
        raise RuntimeError("apt-get update failed while installing {}".format(pkg))
    if subprocess.run(["apt-get", "install", "-y", "-qq", pkg]).returncode != 0:
        raise RuntimeError("apt-get install {} failed".format(pkg))


def ensure_ffmpeg():
    """ffmpeg (Debian package bundles libvorbis + libwebp) must be on PATH."""
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
    """The fontTools module (apt `fonttools`) must be importable. The image's
    Python is PEP 668 externally-managed, so install via apt, not pip."""
    try:
        import fontTools  # noqa: F401
        return
    except ImportError:
        pass
    if _can_apt_install():
        _apt_install("fonttools")
        import importlib
        importlib.invalidate_caches()
        try:
            import fontTools  # noqa: F401
            return
        except ImportError:
            pass
    raise RuntimeError(
        "fontTools not found and cannot auto-install (needs root + apt-get, i.e. "
        "inside the emscripten/emsdk container). Install it: "
        "apt-get install -y fonttools  (apt, not pip -- the image's Python is "
        "PEP 668 externally-managed)")


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


def unpack_tex1_to_rgba(fmt, w, h, body):
    """Decode a Tex1 pixel body (one of the 5 known formats) to a flat RGBA8888
    bytearray (w*h*4 bytes, row-major, R,G,B,A). Bulk slice assignment is used so
    the per-pixel work stays at C speed for the common formats."""
    n = w * h
    if fmt == 0x01:  # RGBA8888 -- straight copy
        return bytearray(body[:n * 4])

    out = bytearray(n * 4)
    if fmt == 0x00:  # RGB888 (R,G,B bytes) -> RGBA, A=255
        out[0::4] = body[0:n * 3:3]
        out[1::4] = body[1:n * 3:3]
        out[2::4] = body[2:n * 3:3]
        out[3::4] = b"\xff" * n
        return out

    # 16-bit formats: read the body as native-order uint16, force little-endian.
    u16 = array.array("H")
    u16.frombytes(bytes(body[:n * 2]))
    if sys.byteorder != "little":
        u16.byteswap()

    if fmt == 0x0f:  # RGBA5551: R[15..11] G[10..6] B[5..1] A[0]
        out[0::4] = bytes(_EXP5[(p >> 11) & 0x1f] for p in u16)
        out[1::4] = bytes(_EXP5[(p >> 6) & 0x1f] for p in u16)
        out[2::4] = bytes(_EXP5[(p >> 1) & 0x1f] for p in u16)
        out[3::4] = bytes(0xff if (p & 0x1) else 0x00 for p in u16)
    elif fmt == 0x10:  # RGBA4444: R[15..12] G[11..8] B[7..4] A[3..0]
        out[0::4] = bytes(_EXP4[(p >> 12) & 0xf] for p in u16)
        out[1::4] = bytes(_EXP4[(p >> 8) & 0xf] for p in u16)
        out[2::4] = bytes(_EXP4[(p >> 4) & 0xf] for p in u16)
        out[3::4] = bytes(_EXP4[p & 0xf] for p in u16)
    elif fmt == 0x11:  # RGB565: R[15..11] G[10..5] B[4..0], A=255
        out[0::4] = bytes(_EXP5[(p >> 11) & 0x1f] for p in u16)
        out[1::4] = bytes(_EXP6[(p >> 5) & 0x3f] for p in u16)
        out[2::4] = bytes(_EXP5[p & 0x1f] for p in u16)
        out[3::4] = b"\xff" * n
    return out


def parse_tex1(raw):
    """If raw is a Tex1 with a known transcodable format and passes the reader's
    size check, return (fmt, w, h, body); else None (copy verbatim)."""
    if len(raw) < 0xd:
        return None
    w_log2 = raw[0]
    h_log2 = raw[1]
    fmt = raw[2]
    if fmt not in TEX_KNOWN_FORMATS:
        return None
    bpp = TEX_KNOWN_FORMATS[fmt]
    row_bytes = (((bpp << w_log2) + 7) >> 3)
    pix_bytes = row_bytes << h_log2
    expected = TEX_HEADER_SIZE + pix_bytes
    if len(raw) < expected:
        return None
    w = 1 << w_log2
    h = 1 << h_log2
    body = raw[TEX_HEADER_SIZE:TEX_HEADER_SIZE + pix_bytes]
    return (fmt, w, h, body)


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
    """Pipe flat RGBA8888 to ffmpeg's libwebp encoder, writing the WebP to a
    seekable temp FILE (NOT stdout). The WebP muxer must seek back to patch the
    RIFF/VP8 chunk sizes after writing the bitstream; a non-seekable stdout pipe
    leaves those sizes wrong and produces a corrupt bitstream (dwebp
    BITSTREAM_ERROR) on larger textures -- the fruit atlas decoded to white.
    Then atomically rename the temp file to dst_tex (same .tex name, WebP bytes)."""
    tmp = dst_tex + ".tmp.webp"
    cmd = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo", "-pix_fmt", "rgba", "-s", "{}x{}".format(w, h),
        "-i", "pipe:0",
        "-c:v", "libwebp",
    ]
    if WEBP_LOSSLESS:
        cmd += ["-lossless", "1"]
    else:
        cmd += ["-quality", WEBP_QUALITY]
    cmd += ["-compression_level", WEBP_COMPRESSION_LEVEL, "-f", "webp", tmp]

    proc = subprocess.run(cmd, input=bytes(rgba_bytes))
    if proc.returncode != 0 or not os.path.exists(tmp) or os.path.getsize(tmp) == 0:
        raise RuntimeError("ffmpeg failed ({}) encoding webp {}".format(proc.returncode, dst_tex))
    os.replace(tmp, dst_tex)


def transcode_tex_file(src_path, dst_tex, stats):
    src_size = os.path.getsize(src_path)
    stats["tex_src_bytes"] += src_size

    if not needs_tex_transcode(src_path, dst_tex):
        stats["tex_skipped"] += 1
        stats["tex_webp_bytes"] += os.path.getsize(dst_tex)
        return

    with open(src_path, "rb") as f:
        raw = f.read()

    parsed = parse_tex1(raw)
    if parsed is None:
        # Not a transcodable Tex1 (Tex2/Tex3/DDS/PVRTC/unknown) -- copy verbatim.
        copy_if_different(src_path, dst_tex)
        stats["tex_copied_verbatim"] += 1
        stats["tex_webp_bytes"] += os.path.getsize(dst_tex)
        return

    fmt, w, h, body = parsed
    rgba = unpack_tex1_to_rgba(fmt, w, h, body)
    encode_webp(rgba, w, h, dst_tex)
    stats["tex_transcoded"] += 1
    stats["tex_webp_bytes"] += os.path.getsize(dst_tex)


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


def subset_cjk_font(src_path, dst_path, codepoints, charset_sources, charset_txt_path, stats):
    stats["font_src_bytes"] = os.path.getsize(src_path)
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


def stage_tree(src_root, dst_root, font_codepoints, font_charset_sources, font_charset_txt_path):
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
        "font_action": None,
        "font_src_bytes": 0,
        "font_out_bytes": 0,
        "font_glyph_count": 0,
        "font_codepoint_count": 0,
    }
    loops = {}

    for root, _dirs, files in os.walk(src_root):
        rel_dir = os.path.relpath(root, src_root)
        dst_dir = os.path.join(dst_root, rel_dir) if rel_dir != "." else dst_root
        os.makedirs(dst_dir, exist_ok=True)

        rel_norm = rel_dir.replace("\\", "/")
        is_sfx_dir = rel_norm == SFX_RELPATH or rel_norm.startswith(SFX_RELPATH + "/")
        is_font_dir = rel_norm == FONT_RELPATH

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
            elif name.lower().endswith(".tex"):
                dst_tex = os.path.join(dst_dir, name)
                transcode_tex_file(src_path, dst_tex, stats)
            elif is_font_dir and name.lower() == CJK_FONT_FILENAME:
                dst_font = os.path.join(dst_dir, name)
                subset_cjk_font(src_path, dst_font, font_codepoints,
                                 font_charset_sources, font_charset_txt_path, stats)
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
        print("Usage: stage-web-assets.py <repo_root>", file=sys.stderr)
        sys.exit(1)

    # Self-provision the external tools (install-where-used). Raises a clear
    # error if missing and not auto-installable.
    ensure_ffmpeg()
    ensure_fonttools()

    repo_root = sys.argv[1]
    src_root = os.path.join(repo_root, "FruitNinjaBada", "Data")
    dst_root = os.path.join(repo_root, "build", "web-staging", "Data")

    if not os.path.isdir(src_root):
        print("ERROR: source Data dir not found: {}".format(src_root), file=sys.stderr)
        sys.exit(1)

    os.makedirs(dst_root, exist_ok=True)

    stringtables_dir = os.path.join(src_root, STRINGTABLES_RELPATH)
    font_codepoints, font_charset_sources = gather_cjk_font_charset(stringtables_dir)
    # Sibling of dst_root (not inside Data/) so it never gets --preload-file'd.
    font_charset_txt_path = os.path.join(os.path.dirname(dst_root), "gangofchinese-charset.txt")

    print("[stage-web-assets] staging {} -> {} (sfx -> Ogg/Vorbis via ffmpeg)".format(
        src_root, dst_root))
    stats, loops = stage_tree(src_root, dst_root, font_codepoints, font_charset_sources,
                               font_charset_txt_path)

    # Emit loop metadata JSON next to the .ogg files.
    loop_json_path = os.path.join(dst_root, SFX_RELPATH, LOOP_JSON_NAME)
    os.makedirs(os.path.dirname(loop_json_path), exist_ok=True)
    with open(loop_json_path, "w") as f:
        json.dump(loops, f, indent=0, sort_keys=True)

    removed = sweep_stale_pcm(dst_root)

    print("[stage-web-assets] sfx: {} transcoded, {} unchanged (skipped); {} loop points".format(
        stats["sfx_transcoded"], stats["sfx_skipped"], len(loops)))

    src_mb = stats["tex_src_bytes"] / (1024.0 * 1024.0)
    webp_mb = stats["tex_webp_bytes"] / (1024.0 * 1024.0)
    ratio = (100.0 * webp_mb / src_mb) if src_mb > 0 else 0.0
    print("[stage-web-assets] textures: {} transcoded to WebP, {} verbatim, {} unchanged".format(
        stats["tex_transcoded"], stats["tex_copied_verbatim"], stats["tex_skipped"]))
    print("[stage-web-assets] texture bytes: {:.1f} MB source -> {:.1f} MB staged ({:.1f}% of original)".format(
        src_mb, webp_mb, ratio))

    if stats["font_action"]:
        font_src_mb = stats["font_src_bytes"] / (1024.0 * 1024.0)
        font_out_mb = stats["font_out_bytes"] / (1024.0 * 1024.0)
        print("[stage-web-assets] font {}: {} -- {} codepoints, {} glyphs, "
              "{:.2f} MB -> {:.2f} MB".format(
                  CJK_FONT_FILENAME, stats["font_action"],
                  stats["font_codepoint_count"], stats["font_glyph_count"],
                  font_src_mb, font_out_mb))

    print("[stage-web-assets] other assets: {} copied, {} unchanged (skipped)".format(
        stats["other_copied"], stats["other_skipped"]))
    if removed:
        print("[stage-web-assets] swept {} stale .wav.pcm from staging sfx/".format(removed))


if __name__ == "__main__":
    main()
