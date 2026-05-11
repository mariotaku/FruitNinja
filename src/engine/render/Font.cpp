// Analysed: 2026-04-29T00:00
#include "render/Font.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/gl_funcs.h"
#include "asset/File.h"
#include "asset/TextureManager.h"
#include "math/Vec3.h"
#include "math/Matrix44.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <new>

#ifdef _MSC_VER
#  define FN_STRNICMP _strnicmp
#else
#  define FN_STRNICMP strncasecmp
#endif

namespace Mortar {

// ---------------------------------------------------------------------------
// Font ctor / dtor
// ---------------------------------------------------------------------------

Font::Font()
    : m_Glyphs(nullptr)
    , m_GlyphCount(0)
    , m_Pages(nullptr)
    , m_PageCount(0)
    , m_Kernings(nullptr)
    , m_KerningCount(0)
    , _pad_0x418(0)
    , m_ScaleW(256)
    , m_ScaleH(256)
    , m_LineHeight(1.0f)
    , m_BaseNorm(0.0f)
    , m_PageVerts(nullptr)
{
    memset(m_GlyphLookup, 0, sizeof(m_GlyphLookup));
}

Font::~Font() {
    delete[] m_Glyphs;
    m_Glyphs = nullptr;

    if (m_Pages) {
        for (int i = 0; i < m_PageCount; i++) {
            m_Pages[i].~Page();
        }
        // Pages were allocated as raw bytes; free via operator delete[]
        operator delete[](reinterpret_cast<void*>(m_Pages));
        m_Pages = nullptr;
    }

    delete[] m_Kernings;
    m_Kernings = nullptr;

    delete[] m_PageVerts;
    m_PageVerts = nullptr;
}

// ---------------------------------------------------------------------------
// Helper: parse an integer value from a key=N token in the .fnt line
// ---------------------------------------------------------------------------

static bool ParseFntInt(const char* p, const char* key, int* out) {
    const char* k = strstr(p, key);
    if (!k) return false;
    k += strlen(key);
    if (*k != '=') return false;
    k++;
    *out = (int)strtol(k, nullptr, 10);
    return true;
}

static bool ParseFntString(const char* p, const char* key, char** outAlloc) {
    const char* k = strstr(p, key);
    if (!k) return false;
    k += strlen(key);
    if (*k != '=') return false;
    k++;
    bool quoted = (*k == '"');
    if (quoted) k++;
    const char* end = k;
    while (*end && (quoted ? *end != '"' : (*end != ' ' && *end != '\t' && *end != '\r' && *end != '\n'))) {
        end++;
    }
    size_t len = (size_t)(end - k);
    char* buf = new char[len + 1];
    memcpy(buf, k, len);
    buf[len] = '\0';
    *outAlloc = buf;
    return true;
}

// ---------------------------------------------------------------------------
// Font::Load -- ASM-verified: 2026-05-08T00:00 binary @ 0x00199e9c (asm-inspector)
//
// Slurps the entire .fnt file via Mortar::File (IFile-backed), walks
// byte-by-byte comparing tags. Matches the binary's IFile-based slurp
// (binary uses a stack-allocated File; port matches that pattern).
// ---------------------------------------------------------------------------

int Font::Load(const char* path) {
    // Binary @ 0x00199e9c: open via Mortar::File (IFile -> FileSystem_Direct).
    // The path is forwarded straight through; FileSystem_Direct's prefix
    // logic (data_dir prepend or strict) is owned by the FileSystem layer.
    Mortar::File f(path, 0, 0);
    if (!f.Open()) {
        fprintf(stderr, "Font::Load: failed to open '%s'\n", path);
        return 0;
    }
    if (!f.Load(nullptr, 0)) {
        fprintf(stderr, "Font::Load: failed to slurp '%s'\n", path);
        return 0;
    }
    const unsigned long fsize = f.Size();
    if (fsize == 0) return 0;

    // Copy into a NUL-terminated heap buffer (parser walks `*p`).
    char* buf = new char[(size_t)fsize + 1];
    memcpy(buf, f.Data(), (size_t)fsize);
    buf[fsize] = '\0';

    // Extract base directory from path for fallback texture loads
    const char* lastSlash = nullptr;
    for (const char* s = path; *s; s++) {
        if (*s == '/' || *s == '\\') lastSlash = s;
    }
    char baseDir[512] = "";
    if (lastSlash) {
        size_t dirLen = (size_t)(lastSlash - path) + 1;
        if (dirLen < sizeof(baseDir)) {
            memcpy(baseDir, path, dirLen);
            baseDir[dirLen] = '\0';
        }
    }

    // Temp storage for raw parse values before we know counts
    int lineHeight = 0, base = 0, scaleW = 256, scaleH = 256;
    int pageCountParsed = 0;
    int glyphCountParsed = 0;
    int kerningCountParsed = 0;

    // --- First pass: gather common/counts ---
    const char* p = buf;
    while (*p) {
        if (strncmp(p, "common ", 7) == 0) {
            ParseFntInt(p, "lineHeight", &lineHeight);
            ParseFntInt(p, "base",       &base);
            ParseFntInt(p, "scaleW",     &scaleW);
            ParseFntInt(p, "scaleH",     &scaleH);
            ParseFntInt(p, "pages",      &pageCountParsed);
        } else if (strncmp(p, "chars ", 6) == 0) {
            ParseFntInt(p, "count", &glyphCountParsed);
        } else if (strncmp(p, "kernings ", 9) == 0) {
            ParseFntInt(p, "count", &kerningCountParsed);
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    m_ScaleW     = scaleW;
    m_ScaleH     = scaleH;
    m_LineHeight = (float)lineHeight;
    m_BaseNorm   = (float)base;  // will be divided at the end
    m_GlyphCount = glyphCountParsed;
    m_PageCount  = pageCountParsed;
    m_KerningCount = kerningCountParsed;

    // Allocate arrays matching binary alloc pattern
    if (m_GlyphCount > 0) {
        m_Glyphs = new CharTemplate[m_GlyphCount]();
    }
    if (m_PageCount > 0) {
        // Binary: operator_new((N+1)*8) — allocates raw bytes, then in-place-constructs each Page
        void* pagesRaw = operator new[]((size_t)(m_PageCount + 1) * sizeof(Page));
        m_Pages = reinterpret_cast<Page*>(pagesRaw);
        for (int i = 0; i < m_PageCount; i++) {
            new (&m_Pages[i]) Page();
        }
    }
    if (m_KerningCount > 0) {
        m_Kernings = new Kerning[m_KerningCount]();
    }

    const float invLH = (lineHeight > 0) ? (1.0f / (float)lineHeight) : 1.0f;
    const float invW  = (scaleW > 0)     ? (1.0f / (float)scaleW)     : 1.0f;
    const float invH  = (scaleH > 0)     ? (1.0f / (float)scaleH)     : 1.0f;

    int glyphIdx   = 0;
    int kerningIdx = 0;

    // --- Second pass: fill arrays ---
    p = buf;
    while (*p) {
        if (strncmp(p, "page ", 5) == 0) {
            int pageId = -1;
            ParseFntInt(p, "id", &pageId);
            if (pageId >= 0 && pageId < m_PageCount) {
                char* fname = nullptr;
                if (ParseFntString(p, "file", &fname)) {
                    m_Pages[pageId].filename = fname;
                }
            }
        } else if (strncmp(p, "char ", 5) == 0 && glyphIdx < m_GlyphCount) {
            int rawId   = 0, rawX = 0, rawY = 0, rawW = 0, rawH = 0;
            int rawXoff = 0, rawYoff = 0, rawXadv = 0, rawPage = 0;
            ParseFntInt(p, "id",       &rawId);
            ParseFntInt(p, "x",        &rawX);
            ParseFntInt(p, "y",        &rawY);
            ParseFntInt(p, "width",    &rawW);
            ParseFntInt(p, "height",   &rawH);
            ParseFntInt(p, "xoffset",  &rawXoff);
            ParseFntInt(p, "yoffset",  &rawYoff);
            ParseFntInt(p, "xadvance", &rawXadv);
            ParseFntInt(p, "page",     &rawPage);

            CharTemplate* g = &m_Glyphs[glyphIdx++];
            g->id   = (uint16_t)rawId;
            g->_pad = 0;
            // Normalize in-place (matches binary ARM at 0x0019a128)
            g->u0   = (float)rawX    * invW;
            g->v0   = (float)rawY    * invH;
            g->w    = (float)rawW    * invLH;
            g->h    = (float)rawH    * invLH;
            g->xoff = (float)rawXoff * invLH;
            g->yoff = (float)rawYoff * invLH;
            g->xadv = (float)rawXadv * invLH;
            g->page = (uint8_t)rawPage;
            g->_pad2[0] = g->_pad2[1] = g->_pad2[2] = 0;

            if (rawId >= 0 && rawId < 256) {
                m_GlyphLookup[rawId] = g;
            }
        } else if (strncmp(p, "kerning ", 8) == 0 && kerningIdx < m_KerningCount) {
            int first = 0, second = 0;
            float amount = 0.0f;
            int iamt = 0;
            ParseFntInt(p, "first",  &first);
            ParseFntInt(p, "second", &second);
            ParseFntInt(p, "amount", &iamt);
            amount = (float)iamt;
            m_Kernings[kerningIdx].first  = (uint32_t)first;
            m_Kernings[kerningIdx].second = (uint32_t)second;
            m_Kernings[kerningIdx].amount = amount;
            kerningIdx++;
        }

        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    delete[] buf;

    // Final fix-up: normalize base
    if (m_LineHeight > 0.0f) {
        m_BaseNorm /= m_LineHeight;
    }

    // Load page textures.
    // TextureManager::Load routes through FileSystem_Direct::TranslateFileName which
    // prepends data_dir; pass a logical (relative) path, not an absolute one.
    // Port specific: derive logical subdir by stripping data_dir prefix from baseDir.
    const char* dataDir = TextureManager::GetDataDir();
    const char* logicalSubDir = "";
    char logicalSubDirBuf[512] = "";
    if (dataDir && dataDir[0] && baseDir[0]) {
        size_t ddLen = strlen(dataDir);
        // baseDir may end with '/' or '\\'; dataDir typically does not
        if (strncmp(baseDir, dataDir, ddLen) == 0) {
            const char* rel = baseDir + ddLen;
            while (*rel == '/' || *rel == '\\') rel++;
            // rel now points to e.g. "fonts/" or "fonts"
            strncpy(logicalSubDirBuf, rel, sizeof(logicalSubDirBuf) - 1);
            logicalSubDirBuf[sizeof(logicalSubDirBuf) - 1] = '\0';
            logicalSubDir = logicalSubDirBuf;
        }
    }

    for (int i = 0; i < m_PageCount; i++) {
        if (!m_Pages[i].filename) continue;

        // Port specific: swap extension to .tex (original atlas is .tga, not present)
        const char* origName = m_Pages[i].filename;
        char texName[512];
        strncpy(texName, origName, sizeof(texName) - 1);
        texName[sizeof(texName) - 1] = '\0';
        char* dot = strrchr(texName, '.');
        if (dot) strcpy(dot, ".tex");

        // Build logical path: subdir + texName (FileSystem_Direct prepends data_dir)
        char logicalPath[512];
        if (logicalSubDir[0]) {
            size_t sdLen = strlen(logicalSubDir);
            bool needSlash = sdLen > 0 &&
                logicalSubDir[sdLen - 1] != '/' &&
                logicalSubDir[sdLen - 1] != '\\';
            snprintf(logicalPath, sizeof(logicalPath), "%s%s%s",
                     logicalSubDir, needSlash ? "/" : "", texName);
        } else {
            snprintf(logicalPath, sizeof(logicalPath), "%s", texName);
        }
        m_Pages[i].texture = TextureManager::GetInstance().Load(logicalPath);
    }

    // Pre-allocate per-page vertex scratch: PAGE_VERT_CAPACITY (0x600)
    // verts per page in one flat heap allocation.
    delete[] m_PageVerts;
    m_PageVerts = (m_PageCount > 0)
        ? new QUADCUSTOMVERTEX[(size_t)m_PageCount * PAGE_VERT_CAPACITY]()
        : nullptr;

    return 1;
}

Mortar::SmartPtr<Font> Font::Create(const char* path) {
    Font* font = new Font();
    if (!font->Load(path)) {
        delete font;
        return Mortar::SmartPtr<Font>();
    }
    return Mortar::SmartPtr<Font>(font);
}

// ---------------------------------------------------------------------------
// Accessor helpers
// ---------------------------------------------------------------------------

Font::CharTemplate* Font::GetCharTemplate(uint32_t cp) const {
    if (cp < 256) return m_GlyphLookup[cp];
    // For codepoints >= 256, linear search (rare in this game)
    for (int i = 0; i < m_GlyphCount; i++) {
        if (m_Glyphs[i].id == (uint16_t)cp) return &m_Glyphs[i];
    }
    return nullptr;
}

Font::Page* Font::GetPage(int idx) const {
    if (idx >= 0 && idx < m_PageCount) return &m_Pages[idx];
    return nullptr;
}

// ---------------------------------------------------------------------------
// GetLineLength — used for horizontal alignment per-line offset
// Returns the line width in lineHeight-normalized units up to a wrap point.
// outSlack is unused (binary computes it but callers ignore it).
// ---------------------------------------------------------------------------

float Font::GetLineLength(Mortar::Utf8StringIterator iter, float wrapWidth, float* outSlack) const {
    float runX = 0.0f;
    while (!iter.IsEmpty()) {
        uint32_t cp = iter.m_CurrentCodepoint;
        if (cp == '\n') break;

        // Skip <font color=...> and </font tags
        if (cp == '<') {
            Mortar::Utf8StringIterator tmp = iter + 1;
            if (!tmp.IsEmpty()) {
                uint32_t next = tmp.m_CurrentCodepoint;
                if (next == '/') {
                    // </font ...> — skip to '>'
                    iter++;
                    while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
                    if (!iter.IsEmpty()) iter++; // consume '>'
                    continue;
                } else if (next == 'f' || next == 'F') {
                    // <font color=...> — skip to '>'
                    while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
                    if (!iter.IsEmpty()) iter++;
                    continue;
                }
            }
        }

        CharTemplate* g = GetCharTemplate(cp);
        if (!g) { iter++; continue; }

        // Word-wrap check at space
        if (cp == ' ' && wrapWidth > 0.0f) {
            // Measure the next word
            float wordW = 0.0f;
            Mortar::Utf8StringIterator wi = iter + 1;
            while (!wi.IsEmpty() && wi.m_CurrentCodepoint != ' ' && wi.m_CurrentCodepoint != '\n') {
                CharTemplate* wg = GetCharTemplate(wi.m_CurrentCodepoint);
                if (wg) wordW += wg->xadv;
                wi++;
            }
            if (runX + wordW > wrapWidth) break;
        }

        runX += g->xadv;
        iter++;
    }

    if (outSlack) *outSlack = wrapWidth - runX;
    return runX;
}

// ---------------------------------------------------------------------------
// MeasureWidth (char* overload — compat for existing callers)
// ---------------------------------------------------------------------------

float Font::MeasureWidth(float /*scale*/, const char* text) const {
    Mortar::Utf8StringIterator iter(text);
    return MeasureWidth(0.0f, iter);
}

float Font::MeasureWidth(float /*scale*/, Mortar::Utf8StringIterator iter) const {
    float width = 0.0f;
    float maxW  = 0.0f;
    while (!iter.IsEmpty()) {
        uint32_t cp = iter.m_CurrentCodepoint;
        if (cp == '\n') {
            if (width > maxW) maxW = width;
            width = 0.0f;
            iter++;
            continue;
        }
        // Skip color tags
        if (cp == '<') {
            while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
            if (!iter.IsEmpty()) iter++;
            continue;
        }
        CharTemplate* g = GetCharTemplate(cp);
        if (g) width += g->xadv;
        iter++;
    }
    if (width > maxW) maxW = width;
    return maxW;
}

// ---------------------------------------------------------------------------
// MeasureString (binary @ 0x001988a8)
// Single-line only: stops at newline or end of string.
// Returns total advance in lineHeight-normalized units.
// ---------------------------------------------------------------------------

// ASM-verified: 2026-05-09 binary @ 0x001988a8 (asm-inspector)
// Binary's MeasureString is a thunk that calls GetLineLength(iter, 0.0f, NULL).
// Forward through to match exactly.
float Font::MeasureString(const Mortar::Utf8StringIterator& iterIn) const {
    return GetLineLength(iterIn, 0.0f, nullptr);
}

float Font::MeasureString(const char* str) const {
    Mortar::Utf8StringIterator iter(str);
    return MeasureString(iter);
}

// ---------------------------------------------------------------------------
// DrawString (matches Font_DrawString 0x00198e44)
// ---------------------------------------------------------------------------

// ASM-verified: 2026-05-09 binary @ 0x00198e44 (asm-inspector)
void Font::DrawString(float scale, float yLineFactor, float rotZ,
                      Mortar::Utf8StringIterator iter, const Vec3& pos, const Colour& colour,
                      Vec2 maxWH, int alignment, float z,
                      MortarRectangleDec* clipRect)
{
    // DIFFERS: clipRect path not implemented -- no shipping caller uses it
    (void)clipRect;
    (void)z;

    if (iter.IsEmpty()) return;

    // Binary modifies maxWH in-place on a stack copy:
    //   maxWH.x /= scale
    //   maxWH.y /= (yLineFactor * scale)
    // yLineFactor is a vertical line-pitch divisor (NOT a wrap-width limit
    // as earlier port comments claimed). The binary's wrapper @ 0x00199aa0
    // hardcodes yLineFactor = 1.0; ScoreControl's "BEST:" label passes 0.9,
    // its digit call passes 0.0 directly to the full overload.
    //
    // Binary preamble @ 0x00198eb0..0x00198eee only runs when maxWH != null
    // (re-analyst 2026-05-10). Port models this by gating on maxWH being
    // non-zero -- prevents NaN when yLineFactor=0 with maxWH=(0,0).
    if (maxWH.x != 0.0f || maxWH.y != 0.0f) {
        maxWH.x /= scale;
        maxWH.y /= (yLineFactor * scale);
    }

    // Word-wrap threshold in lineHeight-normalized units (maxWH.x after /= scale).
    // Wrap is active whenever maxWH.x > 0; the binary's caller signals "no wrap"
    // by passing maxWH = (0, 0). The 0x10 alignment bit is reserved for other
    // semantics (still being RE'd) -- using maxWH.x as the wrap gate matches
    // the description-text path's binary call shape.
    const float wrapLimit = maxWH.x;

    // Per-page glyph vertex counts.
    int* perPageCount = new int[m_PageCount]();

    // m_PageVerts is a flat heap array of size m_PageCount * PAGE_VERT_CAPACITY,
    // populated by Font::Load. Lazy-allocate here for unit-test paths that
    // never called Load (defaults to nullptr in that case).
    if (!m_PageVerts && m_PageCount > 0) {
        m_PageVerts = new QUADCUSTOMVERTEX[(size_t)m_PageCount * PAGE_VERT_CAPACITY]();
    }
    for (int pg = 0; pg < m_PageCount; pg++) {
        perPageCount[pg] = 0;
    }

    // Initial colour
    uint32_t curColour = colour.PlatformColour();
    uint32_t origColour = curColour;

    // Horizontal alignment: compute offset for first line.
    // ASM-verified: 2026-05-10T00:00Z binary @ 0x00198e44 (re-analyst hand-
    // disassembly @ 0x00198ef0..0x00198f80). Definitive alignment decode:
    //   alignment & 3 == 0 (LEFT):       lineOffset = 0
    //   alignment & 3 == 1 (CENTER):     lineOffset = 0  -- INERT for X
    //                                    (the binary loads 0.0f from the
    //                                    constant pool unconditionally for
    //                                    this branch; bit 4 is tested but
    //                                    both sides produce 0)
    //   alignment & 3 == 2 (RIGHT):
    //     measureCap = (alignment & 0x10) ? wrapLimit : 0.0f
    //     lineOffset = wrapLimit - GetLineLength(iter, measureCap)
    //   alignment & 3 == 3 (RIGHT|CENTER, "centre-within-box"):
    //     same as RIGHT, then lineOffset *= 0.5
    // Implications when callers pass maxWH=(0,0) (most callers):
    //   - LEFT  (0x00): text starts at pos.x
    //   - CENTER(0x01): text starts at pos.x   (NOT centred -- caller must
    //                                           pre-offset its anchor)
    //   - RIGHT (0x02): right edge at pos.x    (lineOffset = -lineWidth)
    //   - 0x03         : centred on pos.x      (lineOffset = -lineWidth/2)
    // Earlier port unconditionally applied -lineWidth*0.5 for CENTER, which
    // moved score digits leftward over the watermelon icon. The binary's
    // CENTER mode is intentionally inert; ScoreControl's 0x0d achieves
    // LEFT-anchor layout via this inert path, not via a real centring op.
    const int horizAlign = alignment & 0x3;
    float lineOffset = 0.0f;
    if (horizAlign >= 2) {
        const float measureCap = (alignment & 0x10) ? wrapLimit : 0.0f;
        float len = GetLineLength(iter, measureCap, nullptr);
        lineOffset = wrapLimit - len;
        if (horizAlign == 3) lineOffset *= 0.5f;
    }
    float cursorX = 0.0f;
    float cursorY = 0.0f;  // starts at 0; vertical alignment applied via TranslateLocal

    // Spacing from maxWH (binary reads local_64[0] which is a spacing scalar;
    // in shipping callers maxWH is always (0,0) so spacing = 0)
    const float spacing = 0.0f;

    // --- Glyph emit loop ---
    while (!iter.IsEmpty()) {
        uint32_t cp = iter.m_CurrentCodepoint;

        if (cp == '\n') {
            iter++;
            // Per-line recompute (binary @ 0x00198fc0..0x0019906a).
            // Same alignment decode as the first-line block above.
            lineOffset = 0.0f;
            if (horizAlign >= 2) {
                const float measureCap = (alignment & 0x10) ? wrapLimit : 0.0f;
                float _len = GetLineLength(iter, measureCap, nullptr);
                lineOffset = wrapLimit - _len;
                if (horizAlign == 3) lineOffset *= 0.5f;
            }
            cursorX = 0.0f;
            cursorY -= 1.0f;  // one lineHeight unit down
            continue;
        }

        // Word-wrap at space
        if (cp == ' ' && wrapLimit > 0.0f) {
            Mortar::Utf8StringIterator wi = iter + 1;
            float wordW = 0.0f;
            while (!wi.IsEmpty() && wi.m_CurrentCodepoint != ' ' && wi.m_CurrentCodepoint != '\n') {
                CharTemplate* wg = GetCharTemplate(wi.m_CurrentCodepoint);
                if (wg) wordW += wg->xadv;
                wi++;
            }
            if (cursorX + wordW > wrapLimit) {
                iter++;
                lineOffset = 0.0f;
                if (horizAlign >= 2) {
                    const float measureCap = (alignment & 0x10) ? wrapLimit : 0.0f;
                    float _len = GetLineLength(iter, measureCap, nullptr);
                    lineOffset = wrapLimit - _len;
                    if (horizAlign == 3) lineOffset *= 0.5f;
                }
                cursorX = 0.0f;
                cursorY -= 1.0f;
                continue;
            }
        }

        // Color tags: <font color=RRGGBB[AA]> and </font>
        if (cp == '<') {
            Mortar::Utf8StringIterator tagIter = iter + 1;
            if (!tagIter.IsEmpty() && (tagIter.m_CurrentCodepoint == '/' ||
                tagIter.m_CurrentCodepoint == 'f' || tagIter.m_CurrentCodepoint == 'F')) {

                if (tagIter.m_CurrentCodepoint == '/') {
                    // </font -- reset colour, advance to '>'
                    curColour = origColour;
                    iter++;
                    while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
                    if (!iter.IsEmpty()) iter++; // consume '>'
                    continue;
                } else {
                    // <font color=RRGGBB[AA]>
                    // Skip to 'color=' or 'colour=' tag
                    // Advance past '<font' to find 'color='
                    Mortar::Utf8StringIterator scan = iter + 1;
                    // Skip "font"
                    while (!scan.IsEmpty() && scan.m_CurrentCodepoint != '>' &&
                           scan.m_CurrentCodepoint != '=') scan++;
                    // Now scan should be at '='
                    if (!scan.IsEmpty() && scan.m_CurrentCodepoint == '=') {
                        scan++; // skip '='
                        // Read up to 8 hex digits
                        char hexbuf[9];
                        int hexlen = 0;
                        while (!scan.IsEmpty() && hexlen < 8) {
                            uint32_t hc = scan.m_CurrentCodepoint;
                            if ((hc >= '0' && hc <= '9') || (hc >= 'a' && hc <= 'f') ||
                                (hc >= 'A' && hc <= 'F')) {
                                hexbuf[hexlen++] = (char)hc;
                                scan++;
                            } else {
                                break;
                            }
                        }
                        hexbuf[hexlen] = '\0';
                        if (hexlen >= 6) {
                            uint32_t rgb = (uint32_t)strtoul(hexbuf, nullptr, 16);
                            uint8_t cr, cg, cb, ca;
                            if (hexlen >= 8) {
                                // RRGGBBAA
                                cr = (uint8_t)((rgb >> 24) & 0xFF);
                                cg = (uint8_t)((rgb >> 16) & 0xFF);
                                cb = (uint8_t)((rgb >>  8) & 0xFF);
                                ca = (uint8_t)(rgb & 0xFF);
                            } else {
                                // RRGGBB — preserve original alpha
                                cr = (uint8_t)((rgb >> 16) & 0xFF);
                                cg = (uint8_t)((rgb >>  8) & 0xFF);
                                cb = (uint8_t)(rgb & 0xFF);
                                ca = colour.a;
                            }
                            Colour tagCol(cr, cg, cb, ca);
                            curColour = tagCol.PlatformColour();
                        }
                    }
                    // Advance iter to past '>'
                    while (!iter.IsEmpty() && iter.m_CurrentCodepoint != '>') iter++;
                    if (!iter.IsEmpty()) iter++;
                    continue;
                }
            }
        }

        CharTemplate* g = GetCharTemplate(cp);
        if (!g) {
            iter++;
            continue;
        }

        int pageIdx = (int)g->page;
        if (pageIdx >= 0 && pageIdx < m_PageCount) {
            // Vertex geometry in lineHeight-normalized space (ARM-confirmed at 0x0019919e)
            // cx = cursor_x + xoff + w*0.5
            // cy = cursor_y - yoff - h*0.5
            const float cx = cursorX + lineOffset + g->xoff + g->w * 0.5f;
            const float cy = cursorY - g->yoff - g->h * 0.5f;
            const float hw = g->w * 0.5f;
            const float hh = g->h * 0.5f;

            // UVs already normalized in CharTemplate
            const float u0 = g->u0;
            const float v0 = g->v0;
            // u1/v1 computed from w/h * (lineHeight / scaleW/H) = raw_w/scaleW
            const float lhDivW = (m_ScaleW > 0) ? m_LineHeight / (float)m_ScaleW : 1.0f;
            const float lhDivH = (m_ScaleH > 0) ? m_LineHeight / (float)m_ScaleH : 1.0f;
            const float u1 = u0 + g->w * lhDivW;
            const float v1 = v0 + g->h * lhDivH;

            // ASM-verified: 2026-05-09 binary @ 0x00199576..0x00199836
            // (asm-inspector). Per-glyph emit is 6 verts in GL_TRIANGLE_STRIP
            // order: LB, LT, RB, RT + 2 degenerate copies of RT.
            //   strip(LB,LT,RB,RT) = tris (LB,LT,RB) + (LT,RB,RT) = one quad
            // Inter-glyph connector: when starting a new glyph at base > 0,
            // the binary OVERWRITES the previous glyph's last slot
            // verts[base-1] with the current LB before emitting the new
            // 6 verts. This collapses the strip transition
            //   (RT_prev, RT_prev, LB_cur)  -> RT_prev=LB_cur degen
            //   (RT_prev=LB_cur, LB_cur, LT_cur) -> degen (first two equal)
            // Without the overwrite, the second tri is NON-degenerate and
            // draws a thin connector triangle between glyphs. (Binary
            // writes verts[base-1] @ 0x001995ee..0x00199648.)
            // V-axis pairing matches binary: cy+hh (screen-top) -> v0,
            // cy-hh (screen-bottom) -> v1.
            const float kZ = 0.0f;  // DAT_00199a94 = 0.0f
            QUADCUSTOMVERTEX v[6];
            v[0] = { cx - hw, cy - hh, kZ, 0,0,1, curColour, u0, v1 };  // LB
            v[1] = { cx - hw, cy + hh, kZ, 0,0,1, curColour, u0, v0 };  // LT
            v[2] = { cx + hw, cy - hh, kZ, 0,0,1, curColour, u1, v1 };  // RB
            v[3] = { cx + hw, cy + hh, kZ, 0,0,1, curColour, u1, v0 };  // RT
            v[4] = v[3];                                                // degenerate
            v[5] = v[3];                                                // degenerate

            const int base = perPageCount[pageIdx] * 6;
            if (base + 6 <= PAGE_VERT_CAPACITY) {
                QUADCUSTOMVERTEX* dst =
                    &m_PageVerts[(size_t)pageIdx * PAGE_VERT_CAPACITY + base];
                // Inter-glyph connector overwrite: prev glyph's last slot
                // becomes this glyph's LB so the strip transition is fully
                // degenerate. Binary @ 0x001995ee-0x00199648.
                if (base > 0) {
                    dst[-1] = v[0];
                }
                for (int vi = 0; vi < 6; vi++) {
                    dst[vi] = v[vi];
                }
                perPageCount[pageIdx]++;
            }
        }

        // Advance cursor: xadv + kerning + spacing
        // spacing * (cp == 0x20 ? 3.0 : 1.0) matches binary (spacing=0 in all shipping callers)
        float spacingMul = (cp == 0x20) ? 3.0f : 1.0f;
        cursorX += g->xadv + GetKerning((uint32_t)cp, (uint32_t)0) + spacing * spacingMul;
        iter++;
    }

    // --- Bake text transform into vertex coords (binary-faithful, matches
    // 0x00199216..0x00199254 architecture) ---
    //
    // ASM-verified: 2026-05-11 binary @ 0x00101c58, 0x00101964 (asm-inspector
    // verdict (c) -- "Helper bypasses the matrix stack entirely. World coords
    // are computed from scalars"). The binary's Font_DrawString writes
    // screen-space scalar math directly into the vertex buffer; it never
    // reads or writes m_Current during per-glyph submission. Per-glyph corner
    // emit is `Vec2 ctor` + `Vec2 operator+` + direct vstr.32 into the batch
    // vertex slots.
    //
    // Port replicates this: build the combined text transform matrix once,
    // apply it as a 2D affine to every batched vertex's (x, y), then flush
    // with an identity world matrix. The port is now insensitive to the
    // caller's dirty m_Current, matching the binary's behaviour without the
    // previous Push+Identity workaround.
    //
    // The transform is M = T_world(pos) * RotZ(rotZ) * T_local(0, alignY) * S(scale)
    // -- build via the existing MatrixStack helpers, then snapshot the 2x3
    // affine submatrix (m[0], m[4], m[12]; m[1], m[5], m[13]) into locals.
    Matrix44 textM;
    textM.Identity();
    textM.ApplyScale(scale, scale, 1.0f);
    if (rotZ != 0.0f) {
        textM.RotZ44(sinf(rotZ), cosf(rotZ));
    }

    // Vertical alignment: LocalTranslate(0, alignY, 0) -- per binary @
    // 0x00199920..0x00199964. The Y-shift depends on the final cursorY
    // (== -(numLines - 1)) so it's known only after the glyph loop above.
    if (alignment & 0xC) {
        float s19 = cursorY - yLineFactor;
        float s14 = -maxWH.y - s19;
        float factor = (alignment & 0x4) ? 0.5f : 1.0f;
        float translateY = s14 * factor;
        textM.LocalTranslate44(0.0f, translateY, 0.0f);
    }
    textM.GlobalTranslate44(pos);

    // 2x3 affine columns (Matrix44 is column-major).
    const float a00 = textM.m[0],  a01 = textM.m[4],  ax = textM.m[12];
    const float a10 = textM.m[1],  a11 = textM.m[5],  ay = textM.m[13];

    // Apply the affine to every emitted vertex's (x, y). Z stays 0 since
    // glyphs are flat and the binary never writes Z.
    for (int pg = 0; pg < m_PageCount; pg++) {
        if (perPageCount[pg] == 0) continue;
        QUADCUSTOMVERTEX* page_verts =
            &m_PageVerts[(size_t)pg * PAGE_VERT_CAPACITY];
        const int n = perPageCount[pg] * 6;
        for (int i = 0; i < n; i++) {
            const float lx = page_verts[i].x;
            const float ly = page_verts[i].y;
            page_verts[i].x = a00 * lx + a01 * ly + ax;
            page_verts[i].y = a10 * lx + a11 * ly + ay;
        }
    }

    // --- Per-page flush with identity world matrix ---
    // Verts are in world space; the matrix stack must NOT re-transform them.
    // Push/Pop bracket to preserve caller's m_Current.
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    world.Push();
    world.m_Current.Identity();
    world.m_Version++;
    MatrixManager::GetInstance().UploadModelViewOnly();

    Renderer* renderer = Renderer::GetInstance();
    if (renderer) {
        for (int pg = 0; pg < m_PageCount; pg++) {
            if (perPageCount[pg] == 0) continue;
            Page* page = GetPage(pg);
            if (!page || !page->texture.IsValid()) continue;
            page->texture->Set();
            renderer->DrawTriStrip(
                &m_PageVerts[(size_t)pg * PAGE_VERT_CAPACITY],
                perPageCount[pg] * 6);
            page->texture->UnSet();
        }
    }

    world.Pop();

    delete[] perPageCount;
}

// ---------------------------------------------------------------------------
// DrawString — thin char* wrapper (matches 0x00199aa0)
// Packs x/y/z into Vec3, calls the full overload with maxWidth=1.0.
// ---------------------------------------------------------------------------

// Binary-shape wrapper. Matches Font::DrawString @ 0x00199aa0 byte-for-byte:
//   - 7 individual float args mapped to s0..s6 by hard-float ABI.
//   - by-reference iter / colour copied into local stack copies (binary
//     does this for both — Utf8StringIterator copy ctor + Colour copy ctor).
//   - hardcodes yLineFactor = 1.0 when forwarding to Font_DrawString.
void Font::DrawString(Mortar::Utf8StringIterator& iter,
                      const Colour& colour, int alignment,
                      float posX, float posY, float posZ,
                      float scale, float maxWHx, float maxWHy, float rotZ,
                      MortarRectangleDec* clip)
{
    if (iter.IsEmpty()) return;
    Vec3 pos(posX, posY, posZ);
    Vec2 maxWH(maxWHx, maxWHy);
    // Binary @ 0x00199b1c: vmov.f32 s1, 0x3f800000 -- yLineFactor pinned to 1.0.
    DrawString(scale, /*yLineFactor=*/1.0f, rotZ,
               iter, pos, colour, maxWH, alignment, /*z=*/0.0f, clip);
}

// Port-side convenience wrapper. Forwards to the binary-shape overload
// above (which hardcodes yLineFactor = 1.0 like binary @ 0x00199b1c).
// The 2nd arg `yLineFactor` is preserved in the signature for backward
// source-compat with existing port callers (almost all pass 1.0), but
// is IGNORED -- to pass a non-1.0 value (e.g. ScoreControl's BEST label
// passing 0.9), call the full Font_DrawString directly.
void Font::DrawString(float scale, float /*yLineFactor (ignored)*/, float z,
                      const char* text, const Vec3& pos,
                      const Colour& colour, int alignment)
{
    if (!text || !*text) return;
    Mortar::Utf8StringIterator iter(text);
    // Forward through the binary-shape wrapper so the call shape lines up
    // with binary's wrapper @ 0x00199aa0 (yLineFactor pinned to 1.0).
    DrawString(iter, colour, alignment,
               pos.x, pos.y, pos.z, scale,
               /*maxWHx=*/0.0f, /*maxWHy=*/0.0f, /*rotZ=*/0.0f, nullptr);
    (void)z;  // binary's wrapper ignores its z slot too
}

// ---------------------------------------------------------------------------
// Stubs (binary signatures present; bodies not yet RE'd)
// ---------------------------------------------------------------------------

// STUB: Font::DrawString(Utf8StringIterator,Vec3,Colour,float,Vec2,int,float,MortarRectangleDec*,float) -- binary @ 0x???? (TODO RE)
void Font::DrawString(Utf8StringIterator, Vec3, Colour, float, Vec2, int, float, MortarRectangleDec*, float) {}

// STUB: Font::DrawString(Utf8StringIterator,float,float,float,Colour,float,float,float,int,MortarRectangleDec*,float) -- binary @ 0x???? (TODO RE)
void Font::DrawString(Utf8StringIterator, float, float, float, Colour, float, float, float, int, MortarRectangleDec*, float) {}

// STUB: Font::FindAdvanceOfNextWord(Utf8StringIterator,float,float,float,float) -- binary @ 0x???? (TODO RE)
float Font::FindAdvanceOfNextWord(Utf8StringIterator, float, float, float, float) { return 0.0f; }

// STUB: Font::GetCharTemplate(long,int) -- binary @ 0x???? (TODO RE)
Font::CharTemplate* Font::GetCharTemplate(long, int) { return nullptr; }

// ASM-spec: 2026-05-11 binary @ 0x001988f0 (re-analyst).
//   maxWidth <= 0: walks string counting '\n', returns lineH * (n+1).
//   maxWidth >  0: word-wrap path; advances per word, line-breaks when
//                  next word would exceed maxWidth.
// Used by FruitFactControl's auto-shrink loop to pick a font scale that
// keeps the fact body within a 96-px box.
float Font::GetStringHeight(Utf8StringIterator iter, float lineH, float maxWidth) {
    if (maxWidth <= 0.0f) {
        int newlines = 0;
        while (!iter.IsEmpty()) {
            if (iter.m_CurrentCodepoint == '\n') ++newlines;
            iter++;
        }
        return lineH * (float)(newlines + 1);
    }

    int lines = 1;
    float curWidth = 0.0f;
    while (!iter.IsEmpty()) {
        uint32_t cp = iter.m_CurrentCodepoint;
        if (cp == '\n') {
            ++lines;
            curWidth = 0.0f;
            iter++;
            continue;
        }
        float wordW = 0.0f;
        Mortar::Utf8StringIterator wi = iter;
        while (!wi.IsEmpty() && wi.m_CurrentCodepoint != ' ' && wi.m_CurrentCodepoint != '\n') {
            CharTemplate* g = GetCharTemplate(wi.m_CurrentCodepoint);
            if (g) wordW += g->xadv;
            wi++;
        }
        if (curWidth > 0.0f && curWidth + wordW > maxWidth) {
            ++lines;
            curWidth = 0.0f;
        }
        curWidth += wordW;
        iter = wi;
        if (!iter.IsEmpty() && iter.m_CurrentCodepoint == ' ') {
            CharTemplate* g = GetCharTemplate(' ');
            if (g) curWidth += g->xadv;
            iter++;
        }
    }
    return lineH * (float)lines;
}

// STUB: Font::MeasureString(Utf8StringIterator) -- binary @ 0x???? (TODO RE)
float Font::MeasureString(Utf8StringIterator) { return 0.0f; }

} // namespace Mortar
