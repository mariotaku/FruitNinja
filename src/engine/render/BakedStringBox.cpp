#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontInterface.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/Renderer.h"
#include "render/Utf8StringIterator.h"
#include "math/Matrix44.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include "render/gl_funcs.h"
#if !defined(__bada__) && !defined(FN_GL_STUB)
#  include "debug/DebugFlags.h"
#endif
#include <cstring>
#include <cmath>
#include <vector>

// v1.6.1 WordWrap::IsEastAsianChar @ 0x002508ec
static bool IsEastAsianChar(uint32_t cp) {
    if (cp >= 0x1100 && cp <= 0x11FF) return true;
    if (cp >= 0x3000 && cp <= 0xD7AF) return true;
    if (cp >= 0xF900 && cp <  0xFB00) return true;
    if (cp >= 0xFF00 && cp <= 0xFFDC) return true;
    return false;
}
// TODO: v1.6.1 0x00250868 (WordWrap::IsNonBeginningChar) -- line-start punctuation guard not ported

// GCC 4.4.1 (C++03) forbids local types as template arguments (C++11 lifted
// this restriction). WordToken must be at file scope so std::vector<WordToken>
// compiles under the Sourcery 2010q1 cross-build.
namespace {
struct WordToken {
    const char* start;
    int         len;
    float       advance; // in world units
    bool        hardBreak; // true = this word is followed by a forced line break
    bool        cjk;       // true = single East-Asian codepoint token (v1.6.1 WordWrap::IsEastAsianChar @ 0x002508ec)
};
} // anonymous namespace

namespace Mortar {

// ASM-spec v1.6.1 Mortar::BakedStringBox ctor @0x002465fc: 7 args; width/height int in binary.
BakedStringBox::BakedStringBox(FontCacheObjectTTF* font,
                               float fontSize,
                               int width,
                               int height,
                               int align,
                               int maxLines,
                               int param8)
    : m_Font(font)
    , m_FontSize(fontSize)
    , m_BoxWidth((float)width)
    , m_BoxHeight((float)height)
    , m_Align(align)
    , m_MaxLines(maxLines)
    , m_Param8(param8)
    , m_AlignMode(-1)
    , m_Colour(255, 255, 255, 255)
    , m_Pos(0.0f, 0.0f, 0.0f)
    , m_ShadowOffset(0.0f, 0.0f, 0.0f)
    , m_ShadowScale(0.0f)
    , m_ShadowCol(255, 255, 255, 255)
    , m_ShadowFlag(false)
    , m_GradTop(255, 255, 255, 255)
    , m_GradBottom(255, 255, 255, 255)
    , m_GradCol2(255, 255, 255, 255)
    , m_GradCol3(255, 255, 255, 255)
    , m_GradMode(0)
    , m_MetallicFlag(false)
    , m_StrokeWidth(0.0f)
    , m_StrokeCount(0)
    , m_StrokeCol0(0, 0, 0, 255)
    , m_StrokeCol1(0, 0, 0, 255)
    , m_StrokeCol2(0, 0, 0, 255)
    , m_ClipX0(0.0f)
    , m_ClipY0(0.0f)
    , m_ClipW(0.0f)
    , m_ClipH(0.0f)
    , m_HasClip(false)
    , m_Dirty(true)
    , m_BaseFontSize(fontSize)
{
    m_Text[0] = '\0';
}

BakedStringBox::~BakedStringBox() {
}

void BakedStringBox::SetText(const char* text) {
    if (!text) text = "";
    strncpy(m_Text, text, sizeof(m_Text) - 1);
    m_Text[sizeof(m_Text) - 1] = '\0';
    m_Dirty = true;
}

// ASM-spec v1.6.1 Mortar::BakedStringBox::SetColour @0x002454e0: (Colour, bool eager).
// TODO: v1.6.1 BakedStringBox::SetColour @0x002454e0 — binary writes m_FillTop + m_ColourMode=1
//   + m_MetallicFlag=0 and on eager(bool!=0) calls FancyBakedString::ApplyGradient per line;
//   port writes m_Colour only.
void BakedStringBox::SetColour(Colour colour, bool /*eager*/) {
    m_Colour = colour;
    m_Dirty = true;
}

// ASM-spec v1.6.1 Mortar::BakedStringBox::SetHorizontalLineSpacing @0x0024565c:
// body = m_AlignMode = param; m_DirtyMesh = true.
void BakedStringBox::SetHorizontalLineSpacing(int spacing) {
    m_AlignMode = spacing;
    m_Dirty = true;
}

// ASM-spec v1.6.1 Mortar::BakedStringBox::SetTranslation @0x00246238: (_Vector3<float>, bool preShift).
void BakedStringBox::SetTranslation(const Vec3& pos, bool preShift) {
    Vec3 p = pos;
    if (preShift) {
        // ASM-spec v1.6.1 BakedStringBox::SetTranslation @0x00246238: preShift!=0 pre-shifts
        // -(boxW/2) in X, +(boxH/2) in Y, using SIGNED INT /2 (truncates: 75/2=37, not 37.5).
        // m_BoxWidth/m_BoxHeight are float in the port; cast to int first to match truncation.
        p.x -= (float)((int)m_BoxWidth  / 2);
        p.y += (float)((int)m_BoxHeight / 2);
    }
    // ASM-spec v1.6.1 BakedStringBox::SetTranslation @0x00246238: writes position
    // fields only; does NOT set m_Dirty. m_Pos is a draw-time translate anchor
    // consumed in Draw() as Vec3 anchor = m_Pos; it is never read by Layout().
    // The previous port code set m_Dirty=true on position change, causing a full
    // re-layout + GL atlas upload every frame when a caller (e.g. MainScreen::Draw)
    // updates position each frame via SetTranslation (performance fix).
    m_Pos = p;
}

void BakedStringBox::FitIntoVerticalBounds() {
    if (!m_Font) return;
    // ASM-spec v1.6.1 BakedStringBox::FitIntoVerticalBounds @ 0x00246fbc:
    // Shrinks m_FontSize in 1.0-px steps until total ink height < m_BoxHeight (HEIGHT predicate,
    // NOT line-count). Binary loop:
    //   Layout(); N = numLines; if (N == 0) return;
    //   step = per-line pitch (RebuildAlignments @ 0x00245c78, stored as line.height)
    //   totalInkHeight = maxBearingY(line0) + (N-1)*step + (-minBottom(lineN-1))
    //   if (totalInkHeight < m_BoxHeight) return;  // fits
    //   nextSize = m_FontSize - 1.0f;
    //   if (nextSize < 6.0f) return;               // floor: stop without applying the too-small size
    //   SetFontSize(nextSize);  // writes BOTH m_FontSize AND m_BaseFontSize, marks dirty
    for (;;) {
        Layout();
        int N = (int)m_Lines.size();
        if (N == 0) return;

        float step = m_Lines[0].height;
        float totalInkHeight = m_Lines[0].maxBearingY
                             + (float)(N - 1) * step
                             + (-m_Lines[N - 1].minBottom);
        if (totalInkHeight < m_BoxHeight) return;

        float nextSize = m_FontSize - 1.0f;
        if (nextSize < 6.0f) return;
        SetFontSize(nextSize);
    }
}

float BakedStringBox::TotalHeight() const {
    // Binary FitIntoVerticalBounds @ 0x00246fbc: totalInkHeight = maxBearingY(line0)
    // + (N-1)*step + (-minBottom(lineN-1)). step is already the full baseline pitch
    // (= (int)(fontSize + m_Param8)); no separate inter-line spacing term.
    int N = (int)m_Lines.size();
    if (N == 0) return 0.0f;
    const float step = m_Lines[0].height;
    return m_Lines[0].maxBearingY
         + (float)(N - 1) * step
         + (-m_Lines[N - 1].minBottom);
}

// Measure world-unit advance of a word (ASCII chars, length len) at requestedSize.
// Binary advance per glyph: advanceX (world units) + tracking(m_Base[0x28], =0) + 1.0.
// The +1.0 inter-glyph gap is the binary's constant (ApplyFormatting_LeftJustify @0x00247874).
// WRAP measure: pure pen-advance per glyph. The binary's FitStringToWidth
// (@ 0x00248734) accumulates advance + (alignArg*fontScale) + 1.0; for this
// left-justified plate the tracking term cancels the +1.0 inter-glyph gap in
// the WRAP, so the effective wrap width is the bare advance sum. (The +1.0 gap
// IS kept in the RENDER advance below, which spaces the drawn glyphs.) Without
// this, "SLICE FRUIT" measured 80 > boxW 75 and over-wrapped to 3 lines.
static float MeasureWord(FontCacheObjectTTF* font, const char* ptr, int len,
                         float requestedSize) {
    float adv = 0.0f;
    const char* p   = ptr;
    const char* end = ptr + len;
    while (p < end) {
        uint32_t cp = Mortar::utf8::decode_next_unicode_character(&p);
        if (cp == 0) break;
        const GlyphAtlasEntry* g = font->GetGlyph(cp, requestedSize);
        if (g) adv += g->advanceX;
    }
    return adv;
}

static float SpaceAdvance(FontCacheObjectTTF* font, float requestedSize) {
    const GlyphAtlasEntry* sp = font->GetGlyph((uint32_t)' ', requestedSize);
    return sp ? sp->advanceX : 0.0f;
}

// Greedy word-wrap layout at m_FontSize into m_Lines.
// All glyph coordinates and advances are in world units (FT metric / 64 * invFontScale).
// Binary: ApplyFormatting_LeftJustify @ 0x00247874; RebuildAlignments @ 0x00245c78.
// Fix (a): GetGlyph now uses FT_Set_Char_Size (DPI=100) and returns world-unit metrics.
// Fix (b): wrap comparison and geometry are both in world units.
// Fix (c): line pitch = binary step; centre-origin = binary RebuildAlignments formula.
// Fix (d): align&3: 3->centre-H, 2->right, 0/1->left.
void BakedStringBox::Layout() {
    m_Lines.clear();
    m_Dirty = false;

    if (!m_Font || m_Text[0] == '\0') {
        return;
    }

    const float requestedSize = m_FontSize;
    if (requestedSize < 1.0f) {
        return;
    }

    const float wrapLimit  = m_BoxWidth;
    const uint32_t packed  = m_Colour.PlatformColour();

    // Pre-render every codepoint in the string so atlas UVs are populated.
    {
        Mortar::Utf8StringIterator it(m_Text);
        while (!it.IsEmpty()) {
            if (it.m_CurrentCodepoint != (uint32_t)'\n')
                m_Font->GetGlyph(it.m_CurrentCodepoint, requestedSize);
            it++;
        }
    }
    FontInterface* atlas = m_Font->GetAtlas();
    if (atlas) atlas->BuildPendingTextures();

    // Tokenise into logical lines split by '\n', then words within each line.
    // Binary SetText splits on '\n' into separate lines before word-wrapping.
    // FitIntoVerticalBounds @0x00246fbc then shrinks until all fit within box.
    std::vector<WordToken> words;
    {
        const char* p = m_Text;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            if (*p == '\n') {
                // Hard line break: emit a zero-width sentinel so the greedy
                // loop below sees a forced end-of-line at this position.
                WordToken tok;
                tok.start     = p;
                tok.len       = 0;
                tok.advance   = 0.0f;
                tok.hardBreak = true;
                tok.cjk       = false;
                words.push_back(tok);
                p++;
                continue;
            }
            const char* ws = p;
            const char* lookahead = p;
            uint32_t firstCp = Mortar::utf8::decode_next_unicode_character(&lookahead);
            if (IsEastAsianChar(firstCp)) {
                // Each East-Asian codepoint becomes its own token so the greedy
                // wrapper can break between any two consecutive CJK chars.
                // v1.6.1 WordWrap::IsEastAsianChar @ 0x002508ec
                WordToken tok;
                tok.start     = ws;
                tok.len       = (int)(lookahead - ws);
                tok.advance   = MeasureWord(m_Font, ws, tok.len, requestedSize);
                tok.hardBreak = false;
                tok.cjk       = true;
                words.push_back(tok);
                p = lookahead;
            } else {
                while (*p && *p != ' ' && *p != '\n') {
                    const char* next = p;
                    uint32_t cp = Mortar::utf8::decode_next_unicode_character(&next);
                    if (IsEastAsianChar(cp)) break;
                    p = next;
                }
                WordToken tok;
                tok.start     = ws;
                tok.len       = (int)(p - ws);
                tok.advance   = MeasureWord(m_Font, ws, tok.len, requestedSize);
                tok.hardBreak = false;
                tok.cjk       = false;
                words.push_back(tok);
            }
        }
    }
    if (words.empty()) return;

    const float spAdv = SpaceAdvance(m_Font, requestedSize);

    // Greedy line-fill loop. '\n' sentinels force a line break immediately.
    size_t wi = 0;
    while (wi < words.size()) {
        // Skip leading hard-break sentinels (bare '\n' at start of a "line").
        if (words[wi].hardBreak) {
            wi++;
            continue;
        }
        size_t lineStart = wi;

        // Measure how many words fit in world units vs wrapLimit,
        // stopping early on a hard line-break sentinel.
        float lineWidth = 0.0f;
        size_t lineEnd  = lineStart;
        while (lineEnd < words.size()) {
            if (words[lineEnd].hardBreak) {
                // Hard break: consume the sentinel and end line here.
                lineEnd++;
                break;
            }
            float needed = words[lineEnd].advance;
            // v1.6.1 WordWrap::CanBreakLineAt @ 0x002509cc: no inter-word space between adjacent CJK tokens
            if (lineEnd > lineStart && !words[lineEnd].cjk && !words[lineEnd - 1].cjk) {
                needed += spAdv;
            }
            if (lineWidth + needed > wrapLimit && lineEnd > lineStart) {
                break;
            }
            lineWidth += needed;
            lineEnd++;
        }
        if (lineEnd == lineStart) {
            // Force at least one word to avoid infinite loop on overlong words.
            lineWidth = words[lineStart].advance;
            lineEnd   = lineStart + 1;
        }

        // ASM-verified: 2026-06-21T00:00Z v1.6.1 BakedStringBox::RebuildAlignments @0x00245c78 (re-analyst):
        // per-line align uses ink extent (GetBounds.right-left), NOT advance sum; X truncated to int.
        //
        // Pre-pass: compute ink extent for this line. Pen starts at 0 (lineOffsetX not yet known).
        // inkLeft  = penX_atFirstGlyph + firstGlyph.bearingX
        // inkRight = penX_atLastGlyph  + lastGlyph.bearingX + lastGlyph.width
        float lineInkWidth = 0.0f;
        {
            float penX = 0.0f;
            bool firstInkGlyph = true;
            float inkLeft  = 0.0f;
            float inkRight = 0.0f;
            bool firstWordInk = true;
            size_t prevInkWj = lineStart; // last non-hardBreak word processed
            for (size_t wj = lineStart; wj < lineEnd; wj++) {
                if (words[wj].hardBreak) continue;
                if (!firstWordInk) {
                    // v1.6.1 WordWrap::CanBreakLineAt @ 0x002509cc: no space between adjacent CJK tokens
                    if (!words[wj].cjk && !words[prevInkWj].cjk) penX += spAdv;
                }
                prevInkWj = wj;
                firstWordInk = false;
                const char* wp    = words[wj].start;
                const char* wpEnd = wp + words[wj].len;
                while (wp < wpEnd) {
                    uint32_t cp = Mortar::utf8::decode_next_unicode_character(&wp);
                    if (cp == 0) break;
                    const GlyphAtlasEntry* g = m_Font->GetGlyph(cp, requestedSize);
                    if (!g) continue;
                    if (g->width > 0.0f) {
                        float gLeft  = penX + g->bearingX;
                        float gRight = gLeft + g->width;
                        if (firstInkGlyph) {
                            inkLeft  = gLeft;
                            inkRight = gRight;
                            firstInkGlyph = false;
                        } else {
                            if (gRight > inkRight) inkRight = gRight;
                        }
                    }
                    penX += g->advanceX + 1.0f;
                }
            }
            lineInkWidth = firstInkGlyph ? lineWidth : (inkRight - inkLeft);
        }

        // Fix (d): horizontal alignment.
        // Binary RebuildAlignments @0x00245c78 low-2-bits: 3=centre-H, 2=right, 0/1=left.
        // Uses ink extent (lineInkWidth), NOT advance sum. Result truncated to int.
        float lineOffsetX = 0.0f;
        {
            const int horizAlign = m_Align & 0x3;
            if (horizAlign == 3) {
                lineOffsetX = wrapLimit * 0.5f - lineInkWidth * 0.5f;
            } else if (horizAlign == 2) {
                lineOffsetX = wrapLimit - lineInkWidth;
            }
            // 0 or 1: left-justified, lineOffsetX = 0.
            lineOffsetX = (float)(int)lineOffsetX;
        }

        BakedStringBoxLine line;
        float curX = lineOffsetX;
        // Track per-line glyph bounds for RebuildAlignments maxAscent/minDescent.
        float lineMaxBearingY = 0.0f;   // max bearingY across glyphs (above baseline)
        float lineMinBottom   = 0.0f;   // min (bearingY - height) across glyphs (below baseline, negative)

        bool firstWordOnLine = true;
        size_t prevLineWj = lineStart; // last non-hardBreak word drawn on this line
        for (size_t wj = lineStart; wj < lineEnd; wj++) {
            if (words[wj].hardBreak) continue; // skip '\n' sentinels
            if (!firstWordOnLine) {
                // v1.6.1 WordWrap::CanBreakLineAt @ 0x002509cc: no space between adjacent CJK tokens
                if (!words[wj].cjk && !words[prevLineWj].cjk) curX += spAdv;
            }
            prevLineWj = wj;
            firstWordOnLine = false;
            const char* wp    = words[wj].start;
            const char* wpEnd = wp + words[wj].len;
            while (wp < wpEnd) {
                uint32_t cp = Mortar::utf8::decode_next_unicode_character(&wp);
                if (cp == 0) break;
                const GlyphAtlasEntry* g = m_Font->GetGlyph(cp, requestedSize);
                if (!g) continue;

                // Advance per binary ApplyFormatting_LeftJustify @ 0x00247874:
                // penX += advance (world units) + tracking(=0) + 1.0.

                if (g->width > 0.0f && g->height > 0.0f) {
                    // Glyph quad: bearingX/Y are world-unit metrics.
                    float x0 = curX + g->bearingX;
                    float y1 = g->bearingY;           // top above baseline
                    float x1 = x0 + g->width;
                    float y0 = y1 - g->height;        // bottom below baseline

                    QUADCUSTOMVERTEX v[6];
                    v[0] = { x0, y0, 0.f, 0,0,1, packed, g->u0, g->v1 };
                    v[1] = { x0, y1, 0.f, 0,0,1, packed, g->u0, g->v0 };
                    v[2] = { x1, y0, 0.f, 0,0,1, packed, g->u1, g->v1 };
                    v[3] = { x1, y1, 0.f, 0,0,1, packed, g->u1, g->v0 };
                    v[4] = v[3];
                    v[5] = v[3];
                    for (int k = 0; k < 6; k++) line.verts.push_back(v[k]);
                    // Record which atlas page this glyph belongs to for per-page batching in Draw.
                    line.glyphPageTexIDs.push_back((uint32_t)g->pageTextureID);

                    if (g->bearingY > lineMaxBearingY)
                        lineMaxBearingY = g->bearingY;
                    float bottom = g->bearingY - g->height;
                    if (bottom < lineMinBottom)
                        lineMinBottom = bottom;
                }

                curX += g->advanceX + 1.0f;
            }
        }

        // ASM-spec v1.6.1 BakedStringBox::RebuildAlignments @ 0x00245c78:
        // step = (int)(m_CurrentFontSize + (m_Param8 - (m_BaseFontSize - m_CurrentFontSize)*0.5))
        // Binary truncates (C-cast to int), not rounds. m_BaseFontSize tracks the initial
        // or last-SetFontSize size; after FitIntoVerticalBounds+SetFontSize both are equal
        // so the shrink term is 0 and step = (int)(fontSize + m_Param8).
        float diffShrink = m_BaseFontSize - requestedSize;
        float step = (float)(int)(requestedSize + ((float)m_Param8 - diffShrink * 0.5f));

        line.height       = step;
        line.width        = lineWidth;
        line.maxBearingY  = lineMaxBearingY;
        line.minBottom    = lineMinBottom;
        m_Lines.push_back(line);
        wi = lineEnd;
    }

    // Re-bake gradient on every mesh rebuild (mirrors binary ApplyEffects inside FullInternalRebuild).
    if (m_GradMode >= 2 && !m_Lines.empty()) {
        BakeGradient();
    }
}

// BakeGradient — bake-time gradient: mirrors binary ApplyEffects path inside FullInternalRebuild.
// ASM-spec v1.6.1 BakedStringBox::SetGradient @0x0024566c: per-glyph bake via
// FancyBakedString::ApplyGradient @0x0024accc / Transform_LinearGradient_TopBottom @0x00247a48.
// ASM-spec v1.6.1 FancyBakedString::ApplyMetallicGradient @0x0024abf4: c0->c3 base +
// 2 horizontal-band splits (0.51/0.49) via Transform_GradientSplit @0x0024954c.
void BakedStringBox::BakeGradient() {
    if (m_Lines.empty()) return;

    // Replicate the bbox computation the binary does in Transform_LinearGradient_TopBottom:
    // yTop/yBot span the whole rendered block (same Y extents Draw used to use at render-time).
    // The binary's "rectTop" / "rectBottom" are the block-level Y bounds before per-line offset.
    float minDescent = 0.0f;
    for (size_t li = 0; li < m_Lines.size(); ++li) {
        if (m_Lines[li].minBottom < minDescent)
            minDescent = m_Lines[li].minBottom;
    }
    const float step   = m_Lines[0].height;
    const int   nLines = (int)m_Lines.size();

    // Vertical anchor formula: mirrors Draw's baselineY computation (centre-V path for bake,
    // but we need the same Y as Draw will use for actual rendering so colours match geometry).
    // We replicate the full baselineY logic from Draw so per-line localBaseY is consistent.
    // TODO: v1.6.1 0x00245c78 (RebuildAlignments) -- BakeGradient should use single-line else-branch
    //   at nLines==1: -boxH*0.5 - (fontSize+4.0)*0.5 instead of the multi-line formula below.
    float baselineY = 0.0f;
    const int vertAlign = m_Align & 0xc;
    if (vertAlign == 0xc) {
        baselineY = (-(step * 0.5f) - m_BoxHeight * 0.5f - step * 0.5f
                     + (step * (float)nLines) * 0.5f) - minDescent;
    } else if ((m_Align & 0x8) == 0) {
        const BakedStringBoxLine& l0 = m_Lines[0];
        float ascentSpan = l0.maxBearingY - l0.minBottom;
        float descent    = -l0.minBottom;
        baselineY = -(ascentSpan * 0.5f) - step * 0.5f - descent;
    } else {
        baselineY = m_BoxHeight;
    }

    // Block Y range: top of first-line ascent to bottom of last-line descent.
    float gradYTop = baselineY + m_Lines[0].maxBearingY;
    float lastBaseline = baselineY - (float)(nLines - 1) * step;
    float gradYBot = lastBaseline + minDescent;
    float gradYRange = gradYTop - gradYBot;
    // Binary clamp: if (range < 1.0) range = 1.0  (Transform_LinearGradient_TopBottom @0x00247a48)
    if (gradYRange < 1.0f) gradYRange = 1.0f;

    // c0=m_GradTop, c3=m_GradCol3 for metallic base lerp; c0=m_GradTop, c1=m_GradBottom for 2-stop.
    // Binary metallic param order: c0=top, c1=m_GradBottom, c2=m_GradCol2, c3=m_GradCol3.
    const Colour& colTop    = m_GradTop;
    const Colour& colBot2   = m_GradBottom;   // metallic c1 / 2-stop bottom
    const Colour& colBand2  = m_GradCol2;     // metallic c2
    const Colour& colBot4   = m_GradCol3;     // metallic c3 (full bottom)

    for (size_t li = 0; li < m_Lines.size(); ++li) {
        BakedStringBoxLine& line = m_Lines[li];
        const int nVerts = (int)line.verts.size();
        float localBaseY = baselineY - (float)li * step;

        for (int vi = 0; vi < nVerts; ++vi) {
            // Layout Y: same expression Draw uses to compute the pre-transform Y.
            float layoutY = line.verts[vi].y + localBaseY;

            unsigned char r, g, b, a;

            if (m_GradMode == 4) {
                // Metallic: step 1 — full c0->c3 base lerp (Transform_LinearGradient_TopBottom).
                // vy >= yTop or vy < yBot -> solid top colour (edge clamp, binary behaviour).
                float t;
                if (layoutY >= gradYTop || layoutY < gradYBot) {
                    t = 0.0f;
                } else {
                    t = (gradYTop - layoutY) / gradYRange;
                }
                // Per-channel: top*(1-t) + bot*t, normalised /255 then *(int)255, truncated.
                // t=0 at top (layoutY>=gradYTop) -> top colour; t=1 at bottom -> bottom colour.
                float fr = (colTop.r / 255.0f) * (1.0f - t) + (colBot4.r / 255.0f) * t;
                float fg = (colTop.g / 255.0f) * (1.0f - t) + (colBot4.g / 255.0f) * t;
                float fb = (colTop.b / 255.0f) * (1.0f - t) + (colBot4.b / 255.0f) * t;
                float fa = (colTop.a / 255.0f) * (1.0f - t) + (colBot4.a / 255.0f) * t;
                r = (unsigned char)(int)(fr * 255.0f);
                g = (unsigned char)(int)(fg * 255.0f);
                b = (unsigned char)(int)(fb * 255.0f);
                a = (unsigned char)(int)(fa * 255.0f);

                // Metallic: step 2 — ApplyGradientSplit(0.51, c1=m_GradBottom).
                // ASM-verified v1.6.1 Transform_GradientSplit @0x0024954c: plane d = -(sum*frac)
                // with sum=(rectTop+rectBottom); per-vertex test paints the side vy + d > eps,
                // i.e. vy > -d = frac*(gradYTop+gradYBot) (greater-Y / upper side).
                float plane1 = 0.51f * (gradYTop + gradYBot);
                if (layoutY > plane1) {
                    r = colBot2.r;
                    g = colBot2.g;
                    b = colBot2.b;
                    a = colBot2.a;
                }

                // Metallic: step 3 — ApplyGradientSplit(0.49, c2=m_GradCol2).
                // 0.49 plane sits below the 0.51 plane, so c2 overpaints c1 above it,
                // leaving a thin c1 strip between the two planes.
                float plane2 = 0.49f * (gradYTop + gradYBot);
                if (layoutY > plane2) {
                    r = colBand2.r;
                    g = colBand2.g;
                    b = colBand2.b;
                    a = colBand2.a;
                }
            } else {
                // 2-stop gradient (m_GradMode == 2): Transform_LinearGradient_TopBottom.
                float t;
                if (layoutY >= gradYTop || layoutY < gradYBot) {
                    t = 0.0f;
                } else {
                    t = (gradYTop - layoutY) / gradYRange;
                }
                // top*(1-t) + bot*t: t=0 at top -> top colour, t=1 at bottom -> bottom colour.
                float fr = (colTop.r / 255.0f) * (1.0f - t) + (colBot2.r / 255.0f) * t;
                float fg = (colTop.g / 255.0f) * (1.0f - t) + (colBot2.g / 255.0f) * t;
                float fb = (colTop.b / 255.0f) * (1.0f - t) + (colBot2.b / 255.0f) * t;
                float fa = (colTop.a / 255.0f) * (1.0f - t) + (colBot2.a / 255.0f) * t;
                r = (unsigned char)(int)(fr * 255.0f);
                g = (unsigned char)(int)(fg * 255.0f);
                b = (unsigned char)(int)(fb * 255.0f);
                a = (unsigned char)(int)(fa * 255.0f);
            }

            line.verts[vi].colour = Colour(r, g, b, a).PlatformColour();
        }
    }
}

// SetGradient  binary @ 0x0024566c
// ASM-spec v1.6.1 BakedStringBox::SetGradient @0x0024566c: per-glyph bake via
// FancyBakedString::ApplyGradient @0x0024accc / Transform_LinearGradient_TopBottom @0x00247a48.
void BakedStringBox::SetGradient(Colour top, Colour bottom, bool perGlyph) {
    if (m_GradTop.r != top.r || m_GradTop.g != top.g || m_GradTop.b != top.b || m_GradTop.a != top.a ||
        m_GradBottom.r != bottom.r || m_GradBottom.g != bottom.g || m_GradBottom.b != bottom.b || m_GradBottom.a != bottom.a ||
        m_GradMode != 2 || m_MetallicFlag != false) {
        m_GradMode = 2;
        m_GradTop = top;
        m_GradBottom = bottom;
        m_MetallicFlag = false;
        if (!perGlyph) {
            m_Dirty = true;
        } else if (!m_Lines.empty()) {
            BakeGradient();
        }
    }
}

// SetMetallicGradient  binary @ 0x002458e0
// ASM-spec v1.6.1 FancyBakedString::ApplyMetallicGradient @0x0024abf4: c0->c3 base +
// 2 horizontal-band splits (0.51/0.49) via Transform_GradientSplit @0x0024954c.
// Colour mapping: port top=c0, bottom=c1, c2=c2, c3=c3 (binary order: c0=top, c1, c2, c3=bottom).
void BakedStringBox::SetMetallicGradient(Colour top, Colour bottom, Colour c2, Colour c3, bool flag) {
    if (m_GradTop.r    != top.r    || m_GradTop.g    != top.g    || m_GradTop.b    != top.b    || m_GradTop.a    != top.a    ||
        m_GradBottom.r != bottom.r || m_GradBottom.g != bottom.g || m_GradBottom.b != bottom.b || m_GradBottom.a != bottom.a ||
        m_GradCol2.r   != c2.r     || m_GradCol2.g   != c2.g     || m_GradCol2.b   != c2.b     || m_GradCol2.a   != c2.a     ||
        m_GradCol3.r   != c3.r     || m_GradCol3.g   != c3.g     || m_GradCol3.b   != c3.b     || m_GradCol3.a   != c3.a     ||
        m_GradMode != 4 || m_MetallicFlag != true) {
        m_GradMode      = 4;
        m_MetallicFlag  = true;
        m_GradTop       = top;
        m_GradBottom    = bottom;
        m_GradCol2      = c2;
        m_GradCol3      = c3;
        if (!m_Lines.empty()) {
            BakeGradient();
        } else {
            m_Dirty = true;
        }
    }
    (void)flag;
}

// SetShadow  binary @ 0x002462c0
// ASM-spec v1.6.1 Mortar::BakedStringBox::SetShadow @0x002462c0: (float, Colour, _Vector3<float>, int).
// Note: SetColour/SetTranslation use bool; SetShadow uses int.
void BakedStringBox::SetShadow(float scale, Colour col, Vec3 offset, int flag) {
    if (m_ShadowScale != scale ||
        m_ShadowCol.r != col.r || m_ShadowCol.g != col.g || m_ShadowCol.b != col.b || m_ShadowCol.a != col.a ||
        m_ShadowOffset.x != offset.x || m_ShadowOffset.y != offset.y || m_ShadowOffset.z != offset.z ||
        m_ShadowFlag != (bool)flag) {
        m_Dirty = true;
        m_ShadowScale = scale;
        m_ShadowCol = col;
        m_ShadowFlag = (bool)flag;
        m_ShadowOffset = offset;
    }
}

// ASM-verified: 2026-06-13T04:05Z v1.6.1 binary @ 0x00245314 (asm-inspector)
void BakedStringBox::SetStroke(float width, const Colour& c0) {
    if (m_StrokeCount != 1 || m_StrokeWidth != width ||
        m_StrokeCol0.r != c0.r || m_StrokeCol0.g != c0.g ||
        m_StrokeCol0.b != c0.b || m_StrokeCol0.a != c0.a) {
        m_StrokeWidth = width;
        m_Dirty = true;
        m_StrokeCount = 1;
        m_StrokeCol0 = c0;
    }
}

// ASM-verified: 2026-06-13T04:05Z v1.6.1 binary @ 0x0024536c (asm-inspector)
void BakedStringBox::SetStroke(float width, const Colour& c0, const Colour& c1) {
    if (m_StrokeCount != 2 || m_StrokeWidth != width ||
        m_StrokeCol0.r != c0.r || m_StrokeCol0.g != c0.g ||
        m_StrokeCol0.b != c0.b || m_StrokeCol0.a != c0.a ||
        m_StrokeCol1.r != c1.r || m_StrokeCol1.g != c1.g ||
        m_StrokeCol1.b != c1.b || m_StrokeCol1.a != c1.a) {
        m_Dirty = true;
        m_StrokeWidth = width;
        m_StrokeCount = 2;
        m_StrokeCol0 = c0;
        m_StrokeCol1 = c1;
    }
}

// ASM-verified: 2026-06-13T04:05Z v1.6.1 binary @ 0x002453f0 (asm-inspector)
void BakedStringBox::SetStroke(float width, const Colour& c0, const Colour& c1, const Colour& c2) {
    if (m_StrokeCount != 3 || m_StrokeWidth != width ||
        m_StrokeCol0.r != c0.r || m_StrokeCol0.g != c0.g ||
        m_StrokeCol0.b != c0.b || m_StrokeCol0.a != c0.a ||
        m_StrokeCol1.r != c1.r || m_StrokeCol1.g != c1.g ||
        m_StrokeCol1.b != c1.b || m_StrokeCol1.a != c1.a ||
        m_StrokeCol2.r != c2.r || m_StrokeCol2.g != c2.g ||
        m_StrokeCol2.b != c2.b || m_StrokeCol2.a != c2.a) {
        m_Dirty = true;
        m_StrokeWidth = width;
        m_StrokeCount = 3;
        m_StrokeCol0 = c0;
        m_StrokeCol1 = c1;
        m_StrokeCol2 = c2;
    }
}

// ASM-verified: 2026-06-29 v1.6.1 RebuildAlignments @0x00245c78 (asm-inspector + runtime HLE):
// single-line center-V takes the *else* sub-branch @0x00245e74 (FontInterface::GetInstance()[0]=0x48!=0
// at runtime; the metric-based if-branch @0x00245d74 is dead code). baselineY = -boxH*0.5 - (fontSize+4.0)*0.5
// Pure stateless vertical baseline computation. See BakedStringBox.h ComputeBaselineY doc for formulas.
// lineIdx==0 gives the line-0 anchor used by Draw(); the render loop subtracts li*step per line.
float BakedStringBox::ComputeBaselineY(int align, int nLines, int lineIdx,
                                        float maxBearingY, float minBottom,
                                        float boxH, float step, float maxSpan,
                                        float fontSize)
{
    const int vertAlign = align & 0xc;
    if (vertAlign == 0xc) {
        if (nLines == 1) {
            // ASM-verified: 2026-06-29 v1.6.1 RebuildAlignments @0x00245c78 (asm-inspector + runtime HLE):
            // single-line center-V else-branch @0x00245e74 -- metric-independent.
            // VFP const 4.0 = 0x40800000.
            return -boxH * 0.5f - (fontSize + 4.0f) * 0.5f;
        } else {
            // ASM-verified: 2026-06-29T00:00Z v1.6.1 Mortar::BakedStringBox::RebuildAlignments @0x00245c78 (asm-inspector)
            // multi-line center-V per line i: (lineH*nLines)*0.5 - lineH*0.5 - boxH*0.5 - maxSpan*0.5 - i*lineH
            // binary descent term (pBVar11) evaluates to 0 -- no minDescent subtracted here.
            return (step * (float)nLines) * 0.5f - step * 0.5f - boxH * 0.5f
                   - maxSpan * 0.5f - (float)lineIdx * step;
        }
    } else if ((align & 0x8) == 0) {
        // Top-anchored: binary RebuildAlignments @0x00245c78.
        // ascentSpan = maxBearingY - minBottom (total glyph cap-to-descent span, >0).
        // descent    = -minBottom (descent magnitude, >=0, since minBottom<=0).
        float ascentSpan = maxBearingY - minBottom;
        float descent    = -minBottom;
        return -(ascentSpan * 0.5f) - step * 0.5f - descent;
    } else {
        // Bottom-anchored: binary RebuildAlignments @0x00245c78.
        return boxH;
    }
}

// ASM-spec v1.6.1 Mortar::BakedStringBox::Draw @0x00246e20: (Vec2 scale, float rotation, bool center).
void BakedStringBox::Draw(Vec2 scale, float rotation, bool center) {
    if (!m_Font) return;
    if (m_Dirty) Layout();
    if (m_Lines.empty()) return;

    FontInterface* atlas = m_Font->GetAtlas();
    if (!atlas) return;
    atlas->BuildPendingTextures();

    // Build rotation coefficients.
    const float theta = rotation * (3.14159265f / 180.0f);
    const float sinT  = sinf(theta);
    const float cosT  = cosf(theta);

    // Binary RebuildAlignments @ 0x00245c78: step stored per-line in line.height.
    // Compute maxSpan = max(maxBearingY - minBottom) across all lines (binary iVar7,
    // the max glyph ink-span used in the multi-line center-V baseline formula).
    float maxSpan = 0.0f;
    for (size_t li = 0; li < m_Lines.size(); ++li) {
        float span = m_Lines[li].maxBearingY - m_Lines[li].minBottom;
        if (span > maxSpan) maxSpan = span;
    }

    const float step   = m_Lines[0].height;
    const int   nLines = (int)m_Lines.size();

    // World-space anchor.
    Vec3 anchor = m_Pos;

    // Vertical alignment via ComputeBaselineY() (binary RebuildAlignments @ 0x00245c78).
    // lineIdx=0: the Draw loop below applies the per-line offset via baselineY - li*step.
    // m_FontSize = m_CurrentFontSize @+0xc4 (possibly shrunk by FitIntoVerticalBounds).
    float baselineY = ComputeBaselineY(m_Align, nLines, 0,
                                       m_Lines[0].maxBearingY, m_Lines[0].minBottom,
                                       m_BoxHeight, step, maxSpan, m_FontSize);

    // ASM-spec v1.6.1 BakedStringBox::Draw @0x00246e20: center recenters only the scale-shrink
    // delta (0 at scale=1); per-line centering is in Layout/RebuildAlignments.
    //   anchor.x += boxW*0.5 - boxW*scale.x*0.5
    //   anchor.y -= boxH*0.5 - boxH*scale.y*0.5
    // At scale=(1,1) both correction terms are 0 -> anchor == m_Pos.
    if (center) {
        anchor.x += m_BoxWidth  * 0.5f - m_BoxWidth  * scale.x * 0.5f;
        anchor.y -= m_BoxHeight * 0.5f - m_BoxHeight * scale.y * 0.5f;
    }

    // Use identity world matrix so vertex transforms are in world space.
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    world.Push();
    world.m_Current.Identity();
    world.m_Version++;
    MatrixManager::GetInstance().UploadModelViewOnly();

    Renderer* renderer = Renderer::GetInstance();

    // Apply worldspace scissor clip if set.
    // DIFFERS: original = CPU ClipAgainstPlanes geometry clip (v1.6.1 BakedStringBox::ClipToRectangle
    //   @0x00246358 / RebuildClipping @0x002464d0), using glScissor because GLES2 has no
    //   fixed-function user clip planes and per-glyph CPU mesh clipping isn't ported.
    // Mapping: ortho SetupOrtho(top=160,bottom=-160,left=-240,right=240) gives:
    //   NDC_x = wx * 2/480;  pixel_x = (NDC_x+1)/2 * vpW + vpX = (wx+240)/480 * vpW + vpX
    //   NDC_y = wy * 2/320;  pixel_y_gl = (NDC_y+1)/2 * vpH + vpY = (wy+160)/320 * vpH + vpY
    //   glScissor uses GL bottom-left origin; clip rect: left=m_ClipX0, right=m_ClipX0+m_ClipW,
    //   top=m_ClipY0, bottom=m_ClipY0-m_ClipH (decreasing Y = downward in worldspace).
#if !defined(__bada__) && !defined(FN_GL_STUB)
    // Host/SDL+GLES2 only: glGetIntegerv/GL_VIEWPORT are not in the asm-verify
    // cross-build's GL shim (or the unit-test GL stub), and this whole block is
    // the DIFFERS substitute (binary clips on the CPU via ClipAgainstPlanes, never glScissor).
    if (m_HasClip) {
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        const GLint vpX = vp[0], vpY = vp[1];
        const GLsizei vpW = (GLsizei)vp[2], vpH = (GLsizei)vp[3];
        // Worldspace ortho: right-left=480, top-bottom=320 (fixed game constants).
        const float orthoW = 480.0f;
        const float orthoH = 320.0f;
        const float clipL = m_ClipX0;
        const float clipR = m_ClipX0 + m_ClipW;
        const float clipTop_ws  = m_ClipY0;
        const float clipBot_ws  = m_ClipY0 - m_ClipH;
        GLint sx = (GLint)((clipL + orthoW * 0.5f) / orthoW * (float)vpW) + vpX;
        GLint sy = (GLint)((clipBot_ws + orthoH * 0.5f) / orthoH * (float)vpH) + vpY;
        GLint sw = (GLint)((clipR - clipL) / orthoW * (float)vpW);
        GLint sh = (GLint)((clipTop_ws - clipBot_ws) / orthoH * (float)vpH);
        if (sw < 0) sw = 0;
        if (sh < 0) sh = 0;
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx, sy, sw, sh);
    }
#endif

    // Render lines. Line 0 baseline at baselineY (relative to box centre / anchor.y),
    // each subsequent line step lower (decreasing Y).

    // --- Shadow pass (drawn first, behind all other passes) ---
    // ASM-spec v1.6.1 FancyBakedString::Draw @0x0024b8e4: shadow drawn first at drawPos + m_Translation (m_ShadowOffset).
    // DIFFERS: original blurs the shadow glyphs (FetchGlyph blur_radius ~= ceil(shadowScale*invFontScale));
    //   port draws a SOLID offset drop-shadow (no pixel blur) because the port FontCache has no glyph-blur path.
    // TODO: v1.6.1 BakedStringTTF::BuildGlyphs @0x00248b28 -- port glyph-blur for soft shadow/glow edges.
    {
        const bool doShadow = (!m_ShadowFlag && m_ShadowScale > 0.0f) ||
                              (m_ShadowFlag  && m_ShadowScale >= 0.0f);
        if (doShadow) {
            const uint32_t shadowPacked = m_ShadowCol.PlatformColour();
            const float sdx = m_ShadowOffset.x;
            const float sdy = m_ShadowOffset.y;
            for (size_t li = 0; li < m_Lines.size(); li++) {
                const BakedStringBoxLine& sline = m_Lines[li];
                if (sline.verts.empty()) continue;
                const float localBaseY = baselineY - (float)li * step;
                const int nVerts = (int)sline.verts.size();
                std::vector<QUADCUSTOMVERTEX> wv(sline.verts);
                for (int i = 0; i < nVerts; i++) {
                    const float lx = wv[i].x * scale.x;
                    const float ly = (wv[i].y + localBaseY) * scale.y;
                    wv[i].x = cosT * lx - sinT * ly + anchor.x + sdx;
                    wv[i].y = sinT * lx + cosT * ly + anchor.y + sdy;
                    wv[i].colour = shadowPacked;
                }
                for (int gi = 1; gi * 6 < nVerts; gi++) {
                    wv[gi * 6 - 1] = wv[gi * 6];
                }
                // Per-page batch: draw consecutive same-page glyph runs.
                {
                    const int numGlyphs = (int)sline.glyphPageTexIDs.size();
                    int gIdx = 0;
                    while (gIdx < numGlyphs) {
                        uint32_t curTex = sline.glyphPageTexIDs[gIdx];
                        int runStart = gIdx;
                        while (gIdx < numGlyphs && sline.glyphPageTexIDs[gIdx] == curTex) gIdx++;
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, (GLuint)curTex);
                        glEnable(GL_TEXTURE_2D);
                        TexEnvModulate();
                        renderer->DrawTriStrip(&wv[runStart * 6], (gIdx - runStart) * 6);
                    }
                }
            }
        }
    }

    // --- Stroke (glow) pass (drawn after shadow, before foreground) ---
    // ASM-spec v1.6.1 FancyBakedString::Draw @0x0024b8e4: m_pGlow (stroke) drawn at drawPos.
    // DIFFERS: original = single expanded-glyph (blur) pass; port = 8-direction outline copies.
    // TODO: blur path (see above). Multi-colour stroke (m_StrokeCount>=2/3, ApplyStrokeGradient)
    //   + the m_Field68 inner-stroke layer are not ported yet.
    if (m_StrokeWidth > 0.0f && m_StrokeCount >= 1) {
        const uint32_t strokePacked = m_StrokeCol0.PlatformColour();
        const float sw = m_StrokeWidth;
        const float sd = sw * 0.707f;
        const float offX[8] = {  sw, -sw, 0.0f, 0.0f,  sd, -sd,  sd, -sd };
        const float offY[8] = { 0.0f, 0.0f, sw,  -sw,   sd,  sd, -sd, -sd };
        for (int dir = 0; dir < 8; dir++) {
            const float ox = offX[dir];
            const float oy = offY[dir];
            for (size_t li = 0; li < m_Lines.size(); li++) {
                const BakedStringBoxLine& sline = m_Lines[li];
                if (sline.verts.empty()) continue;
                const float localBaseY = baselineY - (float)li * step;
                const int nVerts = (int)sline.verts.size();
                std::vector<QUADCUSTOMVERTEX> wv(sline.verts);
                for (int i = 0; i < nVerts; i++) {
                    const float lx = wv[i].x * scale.x;
                    const float ly = (wv[i].y + localBaseY) * scale.y;
                    wv[i].x = cosT * lx - sinT * ly + anchor.x + ox;
                    wv[i].y = sinT * lx + cosT * ly + anchor.y + oy;
                    wv[i].colour = strokePacked;
                }
                for (int gi = 1; gi * 6 < nVerts; gi++) {
                    wv[gi * 6 - 1] = wv[gi * 6];
                }
                // Per-page batch for stroke pass.
                {
                    const int numGlyphs = (int)sline.glyphPageTexIDs.size();
                    int gIdx = 0;
                    while (gIdx < numGlyphs) {
                        uint32_t curTex = sline.glyphPageTexIDs[gIdx];
                        int runStart = gIdx;
                        while (gIdx < numGlyphs && sline.glyphPageTexIDs[gIdx] == curTex) gIdx++;
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, (GLuint)curTex);
                        glEnable(GL_TEXTURE_2D);
                        TexEnvModulate();
                        renderer->DrawTriStrip(&wv[runStart * 6], (gIdx - runStart) * 6);
                    }
                }
            }
        }
    }

    // Port specific: accumulate ink bounds across all lines for DebugText_Overlay.
#if !defined(__bada__) && !defined(FN_GL_STUB)
    float dbgInkX0 = 0.0f, dbgInkY0 = 0.0f, dbgInkX1 = 0.0f, dbgInkY1 = 0.0f;
    bool dbgHasInk = false;
#endif

    for (size_t li = 0; li < m_Lines.size(); li++) {
        const BakedStringBoxLine& line = m_Lines[li];
        if (line.verts.empty()) {
            continue;
        }

        // Local Y baseline for this line. Lines go downward (decreasing Y in world).
        float localBaseY = baselineY - (float)li * step;

        const int nVerts = (int)line.verts.size();

        std::vector<QUADCUSTOMVERTEX> wv(line.verts);
        for (int i = 0; i < nVerts; i++) {
            float lx = wv[i].x * scale.x;
            float ly = (wv[i].y + localBaseY) * scale.y;
            wv[i].x = cosT * lx - sinT * ly + anchor.x;
            wv[i].y = sinT * lx + cosT * ly + anchor.y;
        }

        // Wire degenerate connector between glyphs in the tri-strip.
        for (int gi = 1; gi * 6 < nVerts; gi++) {
            wv[gi * 6 - 1] = wv[gi * 6];
        }

        // Port specific: glyph atlas is RGBA (white + coverage-alpha) so GL_MODULATE
        // yields vertex-coloured text on both desktop FFP and emscripten WebGL (which
        // lacks GL_COMBINE). Binary used Bada IFont with an RGBA atlas.
        // Per-page batch: bind each atlas page and draw its consecutive glyph run.
        {
            const int numGlyphs = (int)line.glyphPageTexIDs.size();
            int gIdx = 0;
            while (gIdx < numGlyphs) {
                uint32_t curTex = line.glyphPageTexIDs[gIdx];
                int runStart = gIdx;
                while (gIdx < numGlyphs && line.glyphPageTexIDs[gIdx] == curTex) gIdx++;
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, (GLuint)curTex);
                glEnable(GL_TEXTURE_2D);
                TexEnvModulate();  // must precede DrawTriStrip (it does not set tex-env)
                renderer->DrawTriStrip(&wv[runStart * 6], (gIdx - runStart) * 6);
            }
        }

        // Accumulate ink bounds from transformed verts for the debug overlay.
#if !defined(__bada__) && !defined(FN_GL_STUB)
        if (FN::g_DebugHitboxes && !FN::g_SuppressTextOverlay) {
            for (int i = 0; i < nVerts; i++) {
                if (!dbgHasInk) {
                    dbgInkX0 = dbgInkX1 = wv[i].x;
                    dbgInkY0 = dbgInkY1 = wv[i].y;
                    dbgHasInk = true;
                } else {
                    if (wv[i].x < dbgInkX0) dbgInkX0 = wv[i].x;
                    if (wv[i].x > dbgInkX1) dbgInkX1 = wv[i].x;
                    if (wv[i].y < dbgInkY0) dbgInkY0 = wv[i].y;
                    if (wv[i].y > dbgInkY1) dbgInkY1 = wv[i].y;
                }
            }
        }
#endif
    }

    // Port specific: draw anchor + box + ink-bounds debug overlay.
#if !defined(__bada__) && !defined(FN_GL_STUB)
    if (FN::g_DebugHitboxes && !FN::g_SuppressTextOverlay && dbgHasInk) {
        // Box bounds: m_BoxWidth/m_BoxHeight are declared box dimensions in world units.
        // Approximate box as centred on anchor (exact origin depends on SetTranslation
        // flag and align mode; centering is a reasonable approximation for the overlay).
        const bool hasBox = (m_BoxWidth > 0.0f);
        const float bx0 = anchor.x - m_BoxWidth  * 0.5f;
        const float bx1 = anchor.x + m_BoxWidth  * 0.5f;
        const float by0 = anchor.y - m_BoxHeight * 0.5f;
        const float by1 = anchor.y + m_BoxHeight * 0.5f;
        FN::DebugText_Overlay(anchor.x, anchor.y,
                              hasBox,
                              bx0, by0, bx1, by1,
                              dbgInkX0, dbgInkY0, dbgInkX1, dbgInkY1);
        // Unbind texture after debug overlay to restore clean GL state.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
#endif

#if !defined(__bada__) && !defined(FN_GL_STUB)
    if (m_HasClip) {
        glDisable(GL_SCISSOR_TEST);
    }
#endif

    world.Pop();
}

// SetWorldspaceClipping  binary @ 0x0015ab58 (AddLine call site @0x0015aaf0)
// ASM-spec v1.6.1 Mortar::BakedStringBox::SetWorldspaceClipping @0x00114554: (int x0, int y0, int w, int h).
// ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0: args (-240, -46, 400, 108).
// Args: x0/y0 = top-left corner in worldspace; w/h = width/height (not far corner).
void BakedStringBox::SetWorldspaceClipping(int x0, int y0, int w, int h) {
    m_ClipX0 = (float)x0;
    m_ClipY0 = (float)y0;
    m_ClipW  = (float)w;
    m_ClipH  = (float)h;
    m_HasClip = true;
}

// Update  binary @ 0x0015ab80 (AddLine call site @0x0015aaf0)
// ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0: called after SetWorldspaceClipping.
void BakedStringBox::Update() {
    if (m_Dirty) Layout();
}

// ReshapeBounds  binary @ 0x00245ab8 (v1.6.1 BakedStringBox::ReshapeBounds)
// Writes m_MaxLines=p3, m_BoxWidth=w, m_BoxHeight=h, m_Param8=p4, m_Dirty=true unconditionally.
// ASM-spec v1.6.1 Mortar::BakedStringBox::ReshapeBounds @0x00245ab8: (int, int, int, int).
// ASM-spec v1.6.1 BSButton::Init @0x0015ea40: ReshapeBounds(54,20,1,0) -> m_MaxLines=1.
void BakedStringBox::ReshapeBounds(int width, int height, int maxLines, int param8) {
    m_MaxLines  = maxLines;
    m_BoxWidth  = (float)width;
    m_BoxHeight = (float)height;
    m_Param8    = param8;
    m_Dirty     = true;
}

// SetFontSize  binary call site v1.6.1 PauseScreen::Update @0x001a5ebc
// Updates m_FontSize and m_BaseFontSize, marks dirty.
// TODO: v1.6.1 BakedStringBox::SetFontSize -- confirm exact binary field writes.
void BakedStringBox::SetFontSize(float size) {
    if (m_FontSize != size || m_BaseFontSize != size) {
        m_FontSize     = size;
        m_BaseFontSize = size;
        m_Dirty        = true;
    }
}

} // namespace Mortar
