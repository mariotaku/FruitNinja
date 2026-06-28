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
#include <cstring>
#include <cmath>
#include <vector>

// GCC 4.4.1 (C++03) forbids local types as template arguments (C++11 lifted
// this restriction). WordToken must be at file scope so std::vector<WordToken>
// compiles under the Sourcery 2010q1 cross-build.
namespace {
struct WordToken {
    const char* start;
    int         len;
    float       advance; // in world units
    bool        hardBreak; // true = this word is followed by a forced line break
};
} // anonymous namespace

namespace Mortar {

BakedStringBox::BakedStringBox(FontCacheObjectTTF* font,
                               float fontSize,
                               float width,
                               float height,
                               int align,
                               int maxLines,
                               float lineSpacing,
                               int param8)
    : m_Font(font)
    , m_FontSize(fontSize)
    , m_BoxWidth(width)
    , m_BoxHeight(height)
    , m_Align(align)
    , m_MaxLines(maxLines)
    , m_LineSpacing(lineSpacing)
    , m_HorizLineSpacing(-1.0f)
    , m_Param8(param8)
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

void BakedStringBox::SetColour(const Colour& colour, int /*setBase*/) {
    m_Colour = colour;
    m_Dirty = true;
}

void BakedStringBox::SetHorizontalLineSpacing(float spacing) {
    m_HorizLineSpacing = spacing;
    m_Dirty = true;
}

void BakedStringBox::SetTranslation(const Vec3& pos, int flag) {
    Vec3 p = pos;
    if (flag) {
        // ASM-spec v1.6.1 BakedStringBox::SetTranslation @0x00246238: flag!=0 pre-shifts
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
    // Binary RebuildMeshes @ 0x00246944: shrink fontSize in 1-pixel steps (floor 6.0px)
    // until wrapped text fits within m_MaxLines lines AND no line exceeds m_BoxWidth.
    // m_BaseFontSize stays at the original size; m_FontSize is the shrunk size.
    while (m_FontSize > 6.0f) {
        Layout();
        bool fits = ((int)m_Lines.size() <= m_MaxLines);
        if (fits) {
            for (size_t i = 0; i < m_Lines.size(); ++i) {
                if (m_Lines[i].width > m_BoxWidth) {
                    fits = false;
                    break;
                }
            }
        }
        if (fits) {
            return;
        }
        m_FontSize -= 1.0f;
        m_Dirty = true;
    }
    Layout();
}

float BakedStringBox::TotalHeight() const {
    if (m_Lines.empty()) return 0.0f;
    float total = 0.0f;
    for (size_t i = 0; i < m_Lines.size(); ++i) {
        total += m_Lines[i].height;
        if (i + 1 < m_Lines.size()) {
            total += m_LineSpacing;
        }
    }
    return total;
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
                words.push_back(tok);
                p++;
                continue;
            }
            const char* ws = p;
            while (*p && *p != ' ' && *p != '\n') p++;
            WordToken tok;
            tok.start     = ws;
            tok.len       = (int)(p - ws);
            tok.advance   = MeasureWord(m_Font, ws, tok.len, requestedSize);
            tok.hardBreak = false;
            words.push_back(tok);
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
            if (lineEnd > lineStart) needed += spAdv;
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
            for (size_t wj = lineStart; wj < lineEnd; wj++) {
                if (words[wj].hardBreak) continue;
                if (!firstWordInk) penX += spAdv;
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
        for (size_t wj = lineStart; wj < lineEnd; wj++) {
            if (words[wj].hardBreak) continue; // skip '\n' sentinels
            if (!firstWordOnLine) curX += spAdv;
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

                    if (g->bearingY > lineMaxBearingY)
                        lineMaxBearingY = g->bearingY;
                    float bottom = g->bearingY - g->height;
                    if (bottom < lineMinBottom)
                        lineMinBottom = bottom;
                }

                curX += g->advanceX + 1.0f;
            }
        }

        // Fix (c): binary step = round(currentFontSize + (param8 - (baseFontSize - currentFontSize)*0.5))
        // This is the line pitch used by RebuildAlignments @ 0x00245c78.
        // m_BaseFontSize is the original size; m_FontSize is the (possibly shrunk) current size.
        float diffShrink = m_BaseFontSize - requestedSize;
        float step = floorf(requestedSize + (m_Param8 - diffShrink * 0.5f) + 0.5f);

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
// ASM-verified: 2026-06-13T03:20Z v1.6.1 binary @ 0x002462c0 (asm-inspector)
void BakedStringBox::SetShadow(float scale, Colour col, Vec3 offset, bool flag) {
    if (m_ShadowScale != scale ||
        m_ShadowCol.r != col.r || m_ShadowCol.g != col.g || m_ShadowCol.b != col.b || m_ShadowCol.a != col.a ||
        m_ShadowOffset.x != offset.x || m_ShadowOffset.y != offset.y || m_ShadowOffset.z != offset.z ||
        m_ShadowFlag != flag) {
        m_Dirty = true;
        m_ShadowScale = scale;
        m_ShadowCol = col;
        m_ShadowFlag = flag;
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

void BakedStringBox::Draw(float rotationDegrees, Vec2 scale, int center) {
    if (!m_Font) return;
    if (m_Dirty) Layout();
    if (m_Lines.empty()) return;

    FontInterface* atlas = m_Font->GetAtlas();
    if (!atlas) return;
    atlas->BuildPendingTextures();

    // Build rotation coefficients.
    const float theta = rotationDegrees * (3.14159265f / 180.0f);
    const float sinT  = sinf(theta);
    const float cosT  = cosf(theta);

    // Binary RebuildAlignments @ 0x00245c78: step stored per-line in line.height
    // (all lines share the same step since requestedSize is fixed within one Layout() call).
    // Collect minDescent (min bottom across all lines, <=0) for the centre formula.
    float minDescent = 0.0f;
    for (size_t li = 0; li < m_Lines.size(); ++li) {
        if (m_Lines[li].minBottom < minDescent)
            minDescent = m_Lines[li].minBottom;
    }

    const float step   = m_Lines[0].height;
    const int   nLines = (int)m_Lines.size();

    // World-space anchor.
    Vec3 anchor = m_Pos;

    // Vertical alignment from binary RebuildAlignments @ 0x00245c78.
    // Three branches on (m_Align & 0xc):
    //   0xc (centre-V): existing formula -- unchanged.
    //   0x0..0x3 (top-anchored, (m_Align&0x8)==0): line0 baseline = -(ascentSpan/2) - step/2 - descent.
    //   0x8..0xb (bottom-anchored, (m_Align&0x8)!=0): baseline = boxH.
    // NOTE: sign of descent term has not been ASM-pinned visually; flip if version box lands wrong side.
    float baselineY = 0.0f;
    const int vertAlign = m_Align & 0xc;
    if (vertAlign == 0xc) {
        if (nLines == 1) {
            // ASM-spec v1.6.1 BakedStringBox::RebuildAlignments @0x00245c78 (nLines==1, center-V):
            // binary places the ink CENTRE at -boxH/2 using actual ink extents:
            //   baselineY = -boxH/2 - inkCenter,  inkCenter = (maxBearingY + minBottom)/2.
            float inkCenter = (m_Lines[0].maxBearingY + m_Lines[0].minBottom) * 0.5f;
            baselineY = -m_BoxHeight * 0.5f - inkCenter;
        } else {
            // step == maxLineH per binary; minDescent == minBound (<=0).
            baselineY = (-(step * 0.5f) - m_BoxHeight * 0.5f - step * 0.5f
                         + (step * (float)nLines) * 0.5f) - minDescent;
        }
    } else if ((m_Align & 0x8) == 0) {
        // Top-anchored: binary RebuildAlignments @0x00245c78.
        // ascentSpan = maxBearingY - minBottom (total glyph cap-to-descent span, >0).
        // descent    = -minBottom (descent magnitude, >=0, since minBottom<=0).
        const BakedStringBoxLine& l0 = m_Lines[0];
        float ascentSpan = l0.maxBearingY - l0.minBottom;
        float descent    = -l0.minBottom;
        baselineY = -(ascentSpan * 0.5f) - step * 0.5f - descent;
    } else {
        // Bottom-anchored: binary RebuildAlignments @0x00245c78.
        baselineY = m_BoxHeight;
    }

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
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas->GetTextureID());
        glEnable(GL_TEXTURE_2D);
        TexEnvModulate();  // must precede DrawTriStrip (it does not set tex-env)

        renderer->DrawTriStrip(&wv[0], nVerts);
    }

#if !defined(__bada__) && !defined(FN_GL_STUB)
    if (m_HasClip) {
        glDisable(GL_SCISSOR_TEST);
    }
#endif

    world.Pop();
}

// SetWorldspaceClipping  binary @ 0x0015ab58 (AddLine call site @0x0015aaf0)
// ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0: args (-240, -46, 400, 108).
// Args: x0/y0 = top-left corner in worldspace; w/h = width/height (not far corner).
void BakedStringBox::SetWorldspaceClipping(float x0, float y0, float w, float h) {
    m_ClipX0 = x0;
    m_ClipY0 = y0;
    m_ClipW  = w;
    m_ClipH  = h;
    m_HasClip = true;
}

// Update  binary @ 0x0015ab80 (AddLine call site @0x0015aaf0)
// ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0: called after SetWorldspaceClipping.
void BakedStringBox::Update() {
    if (m_Dirty) Layout();
}

// ReshapeBounds  binary @ 0x00245ab8 (v1.6.1 BakedStringBox::ReshapeBounds)
// Writes m_MaxLines=p3, m_BoxWidth=w, m_BoxHeight=h, m_Param8=p4, m_Dirty=true unconditionally.
// ASM-spec v1.6.1 BSButton::Init @0x0015ea40: ReshapeBounds(54,20,1,0) -> m_MaxLines=1.
void BakedStringBox::ReshapeBounds(float width, float height, int maxLines, int param8) {
    m_MaxLines  = maxLines;
    m_BoxWidth  = width;
    m_BoxHeight = height;
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
