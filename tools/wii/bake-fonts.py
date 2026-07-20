#!/usr/bin/env python3
"""tools/wii/bake-fonts.py -- Wii font baker (task #51).

Rasterizes the used glyph set (per `tmp/prebake/bake_plan.json`) with
FreeType and packs it into native IA8 GXTX atlas pages + a binary metrics
index, replacing runtime stb_truetype rasterization on Wii (stb clips CJK
glyphs and breaks Korean composition -- FreeType renders both correctly, see
`test_cjk_grid`). Output format is documented in
`tools/wii/prebaked-font-format.md`; read that first.

Uses freetype-py (wraps libfreetype -- the same rasterizer the engine's
FreeType-backed host/web/eventual-Wii-loader font path uses, so glyph shapes
match). This is a BAKE-TIME HOST dependency only (like Pillow/sharp for other
asset pipelines), not a runtime dependency -- `pip install freetype-py` on
the machine that runs asset staging.

Usage:
    bake-fonts.py <bake_plan.json dir> <fontstruetype dir> <output dir> [options]

    <bake_plan.json dir>   directory containing bake_plan.json + chars_<lang>.txt
                           (tmp/prebake/ during development)
    <fontstruetype dir>    FruitNinjaBada/Data/fontstruetype/ (gangofchinese.ttf, arabic.ttf)
    <output dir>           where to write <lang>/<size>.idx + <lang>/<size>_pN.gxtx

Options:
    --lang LANG        bake only this language (repeatable). Default: all in the plan.
    --size SIZE         bake only this canonical size (repeatable, int). Default: all.
    --selftest LANG SIZE   dump one (lang,size) atlas to a PNG next to the .idx for
                            eye-verification, then exit (no full bake). Requires Pillow.
    --report PATH       write the coverage-gap + footprint report as JSON to PATH.

Pure FreeType + stdlib; Pillow is optional (only for --selftest PNG dump).
"""

import argparse
import json
import os
import struct
import sys

try:
    import freetype
except ImportError:
    print("bake-fonts: freetype-py is required (pip install freetype-py) -- "
          "this is a bake-time host dependency, not a runtime one.", file=sys.stderr)
    sys.exit(1)

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import gx_encoder  # noqa: E402

CJK_FONT_FILENAME = "gangofchinese.ttf"
ARABIC_FONT_FILENAME = "arabic.ttf"
ARABIC_LANG_KEY = "arabic"

# Union of printable ASCII 0x20 (space) .. 0x7E (~) into EVERY baked
# (language, size) glyph set, on top of whatever the loc-string extraction
# found. Rationale (task #51 coverage fix): the game also renders hardcoded
# ASCII that never appears in any localized string -- numbers, dev/UI/version
# text, and format-string literals (e.g. "%i HITS", "+%i", "SLICE!") -- so a
# glyph set derived purely from chars_<lang>.txt misses ASCII letters/digits/
# punctuation that don't happen to occur in that language's translated
# strings. Without this, BakedFontWii reports a cache miss for those
# codepoints and falls back to stb_truetype at runtime, defeating the
# TTF-free goal. ~95 glyphs, cheap at every size.
ASCII_PRINTABLE_RANGE = range(0x20, 0x7F)

# Device supersample factor (task #52). Wii renders the 480x320 LOGICAL ortho
# filling the full 640x480 EFB, so a logical texel maps to ~1.333x (horizontal)
# .. 1.5x (vertical) screen pixels. Baking at the LOGICAL size then bilinear-
# upscaling to the EFB left Latin blurry and thin bars (chonpu/hyphen)
# under-resolved. We bake each canonical LOGICAL size S at S*BAKE_SS physical px
# so atlas texels ~= screen pixels (crisp), then the runtime loader divides the
# baked metrics back by BAKE_SS to keep the LOGICAL layout unchanged -- the exact
# scheme the host uses with kFontSupersample=3, just at 1.5 for the Wii device
# scale (1.5 = the LARGER device axis; vertical 1.5 is exact, horizontal 1.333 is
# mildly oversampled, both crisp). Written into the .idx header so the loader
# reads it rather than hard-coding it.
BAKE_SS = 1.5

# Runtime DPI scale (task #52 shrink fix). FontCacheObjectTTF::SetCharSize
# (src/engine/render/FontCacheObjectTTF.cpp @ the SetCharSize body) rasterises
# every RUNTIME glyph at charHeight * (100/72) device px -- the 100 is the
# binary's Bada IFont cache-slot constant (v1.6.1 FontInterface ctor @0x002502e0)
# passed as FreeType's vert_res, /72 the standard dpi. EVERY consumer of
# GlyphAtlasEntry's world metrics is calibrated against that render resolution
# (see the long SetCharSize comment). The 1x baker rasterised at the LOGICAL px
# with NO 100/72 -> baked glyphs came out 72/100 = 0.72x smaller than the host
# FreeType reference (confirmed on-device). The baker is the OFFLINE equivalent
# of that runtime rasterizer, so it MUST apply the same DPI scale. This factor
# stays folded into the world size (the loader divides out only BAKE_SS, NOT the
# DPI) so baked world-size == host-FT world-size at the same requested size.
FONT_DPI_SCALE = 100.0 / 72.0

# Candidate page dims tried smallest-first (matches tmp/prebake/footprint.json's
# per-(lang,size) 'page' field, which was estimated with this same set). At
# BAKE_SS the packed glyphs are ~1.5x larger per axis, so a size that fit one
# small page at 1x may now spill to a bigger dim / extra page -- pack_glyphs
# already escalates dim then page-count, so this needs no change beyond the
# larger cells.
PAGE_DIM_CANDIDATES = [128, 256, 512]
SHELF_PAD = 2  # 2px transparent gutter between packed glyph cells. Must be >= the
               # loader's +1-texel UV overscan (BakedFontWii) so bilinear sampling at a
               # glyph edge stays inside the transparent gutter and never reaches the
               # neighbouring glyph (a 1px gutter + 1px overscan bled adjacent chars in).

# FNT2 (task #52): FNT1 + a supersample field. Header magic bumped because the
# glyph rects/metrics are now in SUPERSAMPLED px (BAKE_SS) instead of logical px
# -- an FNT1 loader reading FNT2 records unscaled would render 1.5x too big, so
# the loader MUST reject the old magic. Re-bake ALL atlases when bumping.
# Header layout unchanged in size (16 bytes): the former u32 reserved2 at offset
# 12 becomes u16 supersample (8.8 fixed-point: SS*256) + u16 reserved.
MAGIC = b"FNT2"
SS_FIXED_SHIFT = 8  # supersample stored as 8.8 fixed-point (value = round(SS*256))
HEADER_STRUCT = struct.Struct(">4sHBBIHH")  # magic, atlasDim, pageCount, reserved, glyphCount, supersample_8_8, reserved2
GLYPH_STRUCT = struct.Struct(">IBBHHHHhhH")  # cp, page, reserved, x, y, w, h, bearingX, bearingY, advance


class Glyph(object):
    __slots__ = ("cp", "w", "h", "pitch", "buf", "bearing_x", "bearing_y", "advance")

    def __init__(self, cp, w, h, pitch, buf, bearing_x, bearing_y, advance):
        self.cp = cp
        self.w = w
        self.h = h
        self.pitch = pitch
        self.buf = buf          # raw FT 8-bit coverage bitmap, `pitch`-stride rows
        self.bearing_x = bearing_x
        self.bearing_y = bearing_y
        self.advance = advance


def load_bake_plan(plan_dir):
    with open(os.path.join(plan_dir, "bake_plan.json"), "r", encoding="utf-8") as f:
        return json.load(f)


def load_full_charset(plan_dir, lang):
    """Parse chars_<lang>.txt (format documented in the file itself: line 1 is
    a comment header, subsequent lines are 'U+XXXX\\tCHAR')."""
    path = os.path.join(plan_dir, "chars_{}.txt".format(lang))
    cps = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            if not line.startswith("U+"):
                continue
            hexpart = line[2:].split("\t", 1)[0].strip()
            cps.append(int(hexpart, 16))
    return sorted(set(cps))


def glyph_set_for(plan, plan_dir, lang, size):
    entry = plan["plan"][lang][str(size)]
    if entry["mode"] == "full":
        cps = set(load_full_charset(plan_dir, lang))
    else:
        cps = set(entry["codepoints"])
    cps.update(ASCII_PRINTABLE_RANGE)
    return sorted(cps)


def font_path_for(font_dir, lang):
    if lang == ARABIC_LANG_KEY:
        return os.path.join(font_dir, ARABIC_FONT_FILENAME)
    return os.path.join(font_dir, CJK_FONT_FILENAME)


def physical_size(size):
    """Physical px the LOGICAL canonical `size` is rasterized at =
    round(size * FONT_DPI_SCALE * BAKE_SS):
      - FONT_DPI_SCALE (100/72): reproduce the runtime SetCharSize DPI scale so
        baked world-size == host-FT world-size (task #52 shrink fix). This factor
        STAYS in the world size -- the loader does NOT divide it out.
      - BAKE_SS (1.5): device supersample for crispness; the loader DIVIDES the
        recorded metrics by this to recover LOGICAL layout while the atlas rect
        stays supersampled.
    Rounded to whole px because FreeType set_pixel_sizes takes integer px; the
    loader divides by the ideal (unrounded) BAKE_SS so a <1px rounding delta in
    the atlas density is harmless."""
    return int(round(size * FONT_DPI_SCALE * BAKE_SS))


def rasterize_glyphs(face_path, size, codepoints, missing_out):
    """Rasterize each codepoint at physical_size(`size`) px -- i.e. the LOGICAL
    canonical `size` scaled up by BAKE_SS (task #52 device supersample). The
    recorded metrics (advance, bearingX/Y, w, h) are thus in SUPERSAMPLED px; the
    runtime loader divides them by BAKE_SS (read from the .idx header) to recover
    LOGICAL-px layout, exactly like the host divides by kFontSupersample.
    Codepoints with no glyph in the face (FT_Get_Char_Index == 0) are recorded
    into `missing_out` and skipped -- bake what exists, never silently drop the
    whole language (see task spec)."""
    face = freetype.Face(face_path)
    face.set_pixel_sizes(0, physical_size(size))

    glyphs = []
    for cp in codepoints:
        if face.get_char_index(cp) == 0:
            missing_out.append(cp)
            continue
        face.load_char(cp, freetype.FT_LOAD_RENDER)
        g = face.glyph
        bmp = g.bitmap
        w, h, pitch = bmp.width, bmp.rows, bmp.pitch
        buf = bytes(bmp.buffer) if (w > 0 and h > 0) else b""
        glyphs.append(Glyph(
            cp=cp, w=w, h=h, pitch=pitch, buf=buf,
            bearing_x=g.bitmap_left, bearing_y=g.bitmap_top,
            advance=g.advance.x >> 6,
        ))
    return glyphs


class Page(object):
    def __init__(self, dim):
        self.dim = dim
        # [I][A] per texel, 2 bytes -- same source layout as FontInterface.cpp's
        # Wii LA8 atlas (I=255 always, A=coverage). CRITICAL: initialise the
        # LUMINANCE byte to 255 for EVERY texel, including the transparent
        # gutter -- NOT all-zero. The luminance must be uniformly white so that
        # bilinear sampling at a glyph edge (which blends the ink texel with the
        # adjacent gutter texel) keeps I=255 and only fades A. If the gutter I
        # were 0, edge pixels would fade toward BLACK (I: 255->0) as alpha
        # drops, producing a dark colour fringe on every glyph edge (worst on
        # rotated text / the white-shadow CREDITS heading). Only alpha carries
        # the glyph shape; the blit below writes A=coverage over this white base.
        self.pixels = bytearray(b"\xff\x00" * (dim * dim))
        self.cursor_x = 0
        self.cursor_y = 0
        self.row_h = 0

    def try_pack(self, w, h):
        """Attempt to place a w x h cell (with SHELF_PAD margin). Returns
        (x, y) on success, None if it doesn't fit this page at all (even on
        a fresh row) -- caller must grow the page or spill to a new one."""
        if self.cursor_x + w + SHELF_PAD > self.dim:
            self.cursor_x = 0
            self.cursor_y += self.row_h + SHELF_PAD
            self.row_h = 0
        if self.cursor_y + h > self.dim:
            return None
        if w + SHELF_PAD > self.dim:
            return None
        x, y = self.cursor_x, self.cursor_y
        self.cursor_x += w + SHELF_PAD
        if h > self.row_h:
            self.row_h = h
        return x, y

    def blit(self, x, y, glyph):
        if glyph.w <= 0 or glyph.h <= 0:
            return
        dim = self.dim
        for row in range(glyph.h):
            srow = row * glyph.pitch
            drow = (y + row) * dim + x
            for col in range(glyph.w):
                a = glyph.buf[srow + col]
                di = (drow + col) * 2
                self.pixels[di] = 255       # I
                self.pixels[di + 1] = a     # A


def _try_pack_at(glyphs, order, dim, max_pages):
    """Shelf-pack `glyphs` into pages of `dim`x`dim`, spilling to a new same-dim
    page when a glyph doesn't fit the current one. Returns (pages, placements)
    on success, or None if packing would need more than `max_pages` pages (the
    caller uses this to prefer fewer/bigger pages over many small ones)."""
    pages = [Page(dim)]
    placements = [None] * len(glyphs)
    for i in order:
        g = glyphs[i]
        if g.w <= 0 or g.h <= 0:
            placements[i] = (0, 0, 0)  # ink-less glyph (space): page 0, x=y=0, w=h=0
            continue
        if g.w + SHELF_PAD > dim or g.h > dim:
            return None  # doesn't fit this dim at all, even on an empty page
        pos = pages[-1].try_pack(g.w, g.h)
        if pos is None:
            if len(pages) >= max_pages:
                return None
            pages.append(Page(dim))
            pos = pages[-1].try_pack(g.w, g.h)
            if pos is None:
                return None
        x, y = pos
        pages[-1].blit(x, y, g)
        placements[i] = (len(pages) - 1, x, y)
    return pages, placements


def pack_glyphs(glyphs):
    """Shelf-pack `glyphs` (already sorted by codepoint) into atlas page(s).
    Prefers the smallest PAGE_DIM_CANDIDATES dim that fits the whole set in a
    SINGLE page; if none do, escalates to the largest candidate dim and
    allows multi-page spill there (matches the real-world shape in
    tmp/prebake/footprint.json: small UI sizes fit one small page, large CJK
    'full' bakes at small point sizes spill to 2-3 pages of the BIGGEST dim,
    never many pages of a small dim). Returns
    (pages: [Page], placements: [(page_idx, x, y)] parallel to `glyphs`)."""
    # Sort largest-area-first for a tighter shelf pack (packing order does
    # not affect codepoint sort order in the output -- that's applied later).
    order = sorted(range(len(glyphs)), key=lambda i: -(glyphs[i].w * glyphs[i].h))

    for dim in PAGE_DIM_CANDIDATES:
        result = _try_pack_at(glyphs, order, dim, max_pages=1)
        if result is not None:
            return result

    # Nothing fit in a single page at any candidate dim -- use the largest
    # dim and allow it to spill across as many pages as needed.
    dim = PAGE_DIM_CANDIDATES[-1]
    result = _try_pack_at(glyphs, order, dim, max_pages=len(glyphs) + 1)
    if result is not None:
        return result

    raise RuntimeError("pack_glyphs: glyph set does not fit even the largest "
                        "page dim {} -- increase PAGE_DIM_CANDIDATES".format(dim))


def build_idx_bytes(glyphs, placements, page_count, atlas_dim):
    records = []
    for g, pl in zip(glyphs, placements):
        page_idx, x, y = pl
        records.append((g.cp, page_idx, 0, x, y, g.w, g.h,
                         g.bearing_x, g.bearing_y, g.advance))
    records.sort(key=lambda r: r[0])  # sorted by codepoint for binary search

    ss_fixed = int(round(BAKE_SS * (1 << SS_FIXED_SHIFT)))  # 8.8 fixed-point

    out = bytearray()
    out += HEADER_STRUCT.pack(MAGIC, atlas_dim, page_count, 0, len(records),
                              ss_fixed, 0)
    for r in records:
        out += GLYPH_STRUCT.pack(*r)
    return bytes(out)


def bake_one(plan, plan_dir, font_dir, out_dir, lang, size, report):
    codepoints = glyph_set_for(plan, plan_dir, lang, size)
    face_path = font_path_for(font_dir, lang)

    missing = []
    glyphs = rasterize_glyphs(face_path, size, codepoints, missing)
    glyphs.sort(key=lambda g: g.cp)

    pages, placements = pack_glyphs(glyphs)

    lang_dir = os.path.join(out_dir, lang)
    os.makedirs(lang_dir, exist_ok=True)

    for pi, page in enumerate(pages):
        blob = gx_encoder.encode_gxtx(bytes(page.pixels), page.dim, page.dim, gx_encoder.GX_TF_IA8)
        with open(os.path.join(lang_dir, "{}_p{}.gxtx".format(size, pi)), "wb") as f:
            f.write(blob)

    idx_bytes = build_idx_bytes(glyphs, placements, len(pages), pages[0].dim)
    with open(os.path.join(lang_dir, "{}.idx".format(size)), "wb") as f:
        f.write(idx_bytes)

    total_bytes = len(idx_bytes) + sum(
        12 + gx_encoder.tiled_size(p.dim, p.dim, gx_encoder.GX_TF_IA8) for p in pages)

    entry = {
        "requested": len(codepoints),
        "baked": len(glyphs),
        "missing": len(missing),
        "missing_codepoints": missing,
        "pages": len(pages),
        "page_dim": pages[0].dim,
        "bytes": total_bytes,
    }
    report.setdefault(lang, {})[str(size)] = entry

    print("[bake-fonts] {}/{}: {} glyphs baked ({} missing), {} page(s) @ {}x{}, "
          "{} bytes".format(lang, size, len(glyphs), len(missing), len(pages),
                             pages[0].dim, pages[0].dim, total_bytes))
    if missing:
        print("[bake-fonts]   MISSING in {}: {}".format(
            os.path.basename(face_path),
            ", ".join("U+{:04X}".format(cp) for cp in missing[:20]) +
            (" ..." if len(missing) > 20 else "")))

    return pages, glyphs, placements


def selftest_dump(plan, plan_dir, font_dir, out_dir, lang, size):
    try:
        from PIL import Image
    except ImportError:
        print("bake-fonts --selftest: Pillow is required for the PNG dump "
              "(pip install pillow)", file=sys.stderr)
        return 1

    report = {}
    pages, glyphs, placements = bake_one(plan, plan_dir, font_dir, out_dir, lang, size, report)

    lang_dir = os.path.join(out_dir, lang)
    for pi, page in enumerate(pages):
        img = Image.new("L", (page.dim, page.dim))
        # Visualize the ALPHA (coverage) channel -- byte 1 of each [I][A] texel.
        alpha = bytes(page.pixels[i] for i in range(1, len(page.pixels), 2))
        img.putdata(list(alpha))
        png_path = os.path.join(lang_dir, "{}_p{}_selftest.png".format(size, pi))
        img.save(png_path)
        print("[bake-fonts] --selftest wrote {}".format(png_path))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("plan_dir", help="directory with bake_plan.json + chars_<lang>.txt")
    ap.add_argument("font_dir", help="FruitNinjaBada/Data/fontstruetype/")
    ap.add_argument("out_dir", help="output directory for <lang>/<size>.idx + .gxtx")
    ap.add_argument("--lang", action="append", default=None, help="bake only this language (repeatable)")
    ap.add_argument("--size", action="append", type=int, default=None, help="bake only this size (repeatable)")
    ap.add_argument("--selftest", nargs=2, metavar=("LANG", "SIZE"),
                     help="dump one (lang,size) atlas to a PNG and exit")
    ap.add_argument("--report", metavar="PATH", help="write coverage/footprint JSON report to PATH")
    args = ap.parse_args()

    plan = load_bake_plan(args.plan_dir)

    if args.selftest:
        lang, size = args.selftest[0], int(args.selftest[1])
        return selftest_dump(plan, args.plan_dir, args.font_dir, args.out_dir, lang, size)

    langs = args.lang if args.lang else sorted(plan["plan"].keys())
    sizes = args.size if args.size else plan["canonical_sizes"]

    os.makedirs(args.out_dir, exist_ok=True)
    report = {}
    for lang in langs:
        for size in sizes:
            bake_one(plan, args.plan_dir, args.font_dir, args.out_dir, lang, size, report)

    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=1, sort_keys=True)
        print("[bake-fonts] report written to {}".format(args.report))

    total_bytes = sum(e["bytes"] for lang_r in report.values() for e in lang_r.values())
    total_missing = sum(e["missing"] for lang_r in report.values() for e in lang_r.values())
    print("[bake-fonts] done: {} languages, {:.1f} MB total, {} missing-glyph codepoints".format(
        len(langs), total_bytes / (1024.0 * 1024.0), total_missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
