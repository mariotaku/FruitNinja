#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontInterface.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/Renderer.h"
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
    if (p != m_Pos) { m_Pos = p; m_Dirty = true; }
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
    for (int c = 0; c < len; c++) {
        uint32_t cp = (uint32_t)(unsigned char)ptr[c];
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
        const char* p = m_Text;
        while (*p) {
            if (*p != '\n')
                m_Font->GetGlyph((uint32_t)(unsigned char)*p, requestedSize);
            p++;
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
                const char* wp = words[wj].start;
                for (int c = 0; c < words[wj].len; c++) {
                    uint32_t cp = (uint32_t)(unsigned char)wp[c];
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
            const char* wp = words[wj].start;
            for (int c = 0; c < words[wj].len; c++) {
                uint32_t cp = (uint32_t)(unsigned char)wp[c];
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
}

// SetGradient  binary @ 0x0024566c
// ASM-verified: 2026-06-13T03:20Z binary @ 0x0024566c (asm-inspector)
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
        }
    }
}

// SetMetallicGradient  binary @ 0x002458e0
// Stores 4 fill colours + sets m_GradMode=4 + m_MetallicFlag=1 + dirty.
// Port renders 2-stop (top/bottom) in Draw; full 4-stop metallic path pending.
// TODO: 4-stop metallic render path (binary SetMetallicGradient @0x002458e0)
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
        m_Dirty         = true;
    }
    (void)flag;
}

// SetShadow  binary @ 0x002462c0
// ASM-verified: 2026-06-13T03:20Z binary @ 0x002462c0 (asm-inspector)
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

// ASM-verified: 2026-06-13T04:05Z binary @ 0x00245314 (asm-inspector)
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

// ASM-verified: 2026-06-13T04:05Z binary @ 0x0024536c (asm-inspector)
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

// ASM-verified: 2026-06-13T04:05Z binary @ 0x002453f0 (asm-inspector)
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
        // step == maxLineH per binary; minDescent == minBound (<=0).
        baselineY = (-(step * 0.5f) - m_BoxHeight * 0.5f - step * 0.5f
                     + (step * (float)nLines) * 0.5f) - minDescent;
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

    // Gradient: compute total text block Y range (local, pre-rotation) for lerp.
    // Top of block = baselineY + line0.maxBearingY; bottom = last-line baseline + minDescent.
    // m_GradMode >= 2: apply 2-stop top/bottom gradient (m_GradTop / m_GradBottom).
    const bool doGrad = (m_GradMode >= 2);
    float gradYTop    = 0.0f;
    float gradYBot    = 0.0f;
    float gradYRange  = 0.0f;
    if (doGrad) {
        gradYTop = baselineY + m_Lines[0].maxBearingY;
        float lastBaseline = baselineY - (float)(nLines - 1) * step;
        gradYBot = lastBaseline + minDescent; // minDescent <= 0 so this is below baseline
        gradYRange = gradYTop - gradYBot;
        if (gradYRange < 0.001f) gradYRange = 0.001f;
    }

    // Apply worldspace scissor clip if set.
    // DIFFERS: original = CPU ClipAgainstPlanes geometry clip (v1.6.1 BakedStringBox::ClipToRectangle
    //   @0x00246358 / RebuildClipping @0x002464d0), using glScissor because GLES2 has no
    //   fixed-function user clip planes and per-glyph CPU mesh clipping isn't ported.
    // Mapping: ortho SetupOrtho(top=160,bottom=-160,left=-240,right=240) gives:
    //   NDC_x = wx * 2/480;  pixel_x = (NDC_x+1)/2 * vpW + vpX = (wx+240)/480 * vpW + vpX
    //   NDC_y = wy * 2/320;  pixel_y_gl = (NDC_y+1)/2 * vpH + vpY = (wy+160)/320 * vpH + vpY
    //   glScissor uses GL bottom-left origin; clip rect: left=m_ClipX0, right=m_ClipX0+m_ClipW,
    //   top=m_ClipY0, bottom=m_ClipY0-m_ClipH (decreasing Y = downward in worldspace).
#if !defined(__bada__)
    // Host/SDL+GLES2 only: glGetIntegerv/GL_VIEWPORT are not in the asm-verify
    // cross-build's GL shim, and this whole block is the DIFFERS substitute
    // (the binary clips on the CPU via ClipAgainstPlanes, never glScissor).
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

            if (doGrad) {
                // Lerp t=0 at top (gradYTop), t=1 at bottom (gradYBot).
                // Use pre-transform layout Y: line.verts[i].y + localBaseY.
                float layoutY = line.verts[i].y + localBaseY;
                float t = (gradYTop - layoutY) / gradYRange;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                uint8_t r = (uint8_t)(m_GradTop.r + (int)((m_GradBottom.r - m_GradTop.r) * t));
                uint8_t g = (uint8_t)(m_GradTop.g + (int)((m_GradBottom.g - m_GradTop.g) * t));
                uint8_t b = (uint8_t)(m_GradTop.b + (int)((m_GradBottom.b - m_GradTop.b) * t));
                uint8_t a = (uint8_t)(m_GradTop.a + (int)((m_GradBottom.a - m_GradTop.a) * t));
                wv[i].colour = Colour(r, g, b, a).PlatformColour();
            }
        }

        // Wire degenerate connector between glyphs in the tri-strip.
        for (int gi = 1; gi * 6 < nVerts; gi++) {
            wv[gi * 6 - 1] = wv[gi * 6];
        }

        // Port specific: glyph atlas is RGBA (white + coverage-alpha) so GL_MODULATE
        // yields vertex-coloured text on both desktop FFP and emscripten WebGL (which
        // lacks GL_COMBINE). Binary used Bada IFont with an RGBA atlas.
        Matrix44 mvp = MatrixManager::GetInstance().GetMVP();
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(mvp.ptr());
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas->GetTextureID());
        glEnable(GL_TEXTURE_2D);
        TexEnvModulate();
        glDisable(GL_LIGHTING);
        glColor4ub(255, 255, 255, 255);

        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        const int stride = sizeof(QUADCUSTOMVERTEX);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, stride, &wv[0].x);
        glClientActiveTexture(GL_TEXTURE0);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, stride, &wv[0].u);
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, &wv[0].colour);
        glDisableClientState(GL_NORMAL_ARRAY);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, nVerts);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

        glBindTexture(GL_TEXTURE_2D, 0);
    }

#if !defined(__bada__)
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
