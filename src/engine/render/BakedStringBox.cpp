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
#include <cstdlib>
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

// Return type for MeasureWrap() measure-only pass.
// Binary: FitStrings @0x00246800 / FitStringToWidth @0x00248734 / GetFinalPointSize @0x002468fc.
struct MeasureResult {
    int  lineCount;
    bool overflow;  // true when a line had to force an unbreakable token wider than wrapLimit
};
} // anonymous namespace

namespace Mortar {

// ASM-spec v1.6.1 Mortar::BakedStringBox ctor @0x002465fc: 7 args; width/height int in binary.
BakedStringBox::BakedStringBox(FontCacheObjectTTF* font,
                               float fontSize,
                               int width,
                               int height,
                               ALIGNMENT_TYPE align,
                               int maxLines,
                               int lineSpacing)
    : m_Dirty(true)                           // +0x00
    , m_Visible(false)                        // +0x01
    // m_Lines default-constructed            // +0x04
    , m_Field10(0)                            // +0x10
    , m_Field14(0)                            // +0x14
    , m_ShadowOffset(0.0f, 0.0f, 0.0f)       // +0x18
    , m_BoxWidth(width)                       // +0x24
    , m_BoxHeight(height)                     // +0x28
    , m_MaxLines(maxLines)                    // +0x2c
    , m_Align(align)                          // +0x30
    , m_Text(0)                               // +0x34 (null; SetText allocates)
    , m_Pos(0.0f, 0.0f, 0.0f)                // +0x38
    , m_AlignMode(-1)                         // +0x44
    , m_LineSpacing(lineSpacing)              // +0x48
    , m_BaseFontSize(fontSize)                // +0x4c
    , m_Font(font)                            // +0x50
    , m_StrokeWidth(0.0f)                     // +0x54
    , m_StrokeCount(0)                        // +0x58
    , m_StrokeCol0(0, 0, 0, 255)             // +0x5c
    , m_StrokeCol1(0, 0, 0, 255)             // +0x60
    , m_StrokeCol2(0, 0, 0, 255)             // +0x64
    , m_StrokeLayerWidth(0.0f)                // +0x68
    , m_StrokeLayerColour(255, 255, 255, 255) // +0x6c
    , m_ShadowScale(0.0f)                     // +0x70
    , m_ShadowCol(255, 255, 255, 255)         // +0x74
    , m_ShadowFlag(0)                         // +0x78
    , m_GradTop(255, 255, 255, 255)           // +0x7c
    , m_GradBottom(255, 255, 255, 255)        // +0x80
    , m_GradCol2(255, 255, 255, 255)          // +0x84
    , m_GradCol3(255, 255, 255, 255)          // +0x88
    , m_GradMode(0)                           // +0x8c
    , m_MetallicFlag(0)                       // +0x90
    , m_ClipX0(0)                             // +0x94
    , m_ClipY0(0)                             // +0x98
    , m_ClipW(0)                              // +0x9c
    , m_ClipH(0)                              // +0xa0
    , m_HasClip(false)                        // +0xa4
    , m_FieldA5(false)                        // +0xa5
    , m_ExtraWidth(0.0f)                      // +0xa8
    , m_FieldAc(0)                            // +0xac
    , m_Extra1Colour(0, 0, 0, 0)              // +0xb0
    , m_Extra2Colour(0, 0, 0, 0)              // +0xb4
    // m_WrappedLines default-constructed     // +0xb8
    , m_FontSize(fontSize)                    // +0xc4
{
}

BakedStringBox::~BakedStringBox() {
    if (m_Text) {
        free(m_Text);
        m_Text = 0;
    }
}

void BakedStringBox::SetText(const char* text) {
    if (!text) text = "";
    if (m_Text) {
        free(m_Text);
        m_Text = 0;
    }
    m_Text = (char*)malloc(strlen(text) + 1);
    if (m_Text) strcpy(m_Text, text);
    m_Dirty = true;
}

// ASM-spec v1.6.1 Mortar::BakedStringBox::SetColour @0x002454e0: (Colour, bool eager).
// Binary change-detects on m_GradTop(+0x7c); on change writes m_GradTop=colour,
// m_GradMode(+0x8c)=1, m_MetallicFlag(+0x90)=0; if eager!=0 iterates m_Lines calling
// FancyBakedString::ApplyGradient per line, else m_Dirty=true.
// Port: sets m_Dirty in both paths (ApplyGradient per-line is cosmetically divergent).
void BakedStringBox::SetColour(Colour colour, bool eager) {
    if (m_GradTop.r != colour.r || m_GradTop.g != colour.g ||
        m_GradTop.b != colour.b || m_GradTop.a != colour.a) {
        m_GradTop = colour;
        m_GradMode = 1;
        m_MetallicFlag = 0;
        if (eager) {
            // binary: iterate m_Lines calling FancyBakedString::ApplyGradient per line
            // port: BakedStringBoxLine has no such method; cosmetically divergent
            m_Dirty = true;
        } else {
            m_Dirty = true;
        }
    }
}

// ASM-spec v1.6.1 Mortar::BakedStringBox::SetHorizontalLineSpacing @0x0024565c:
// body = m_AlignMode = param; m_DirtyMesh = true.
void BakedStringBox::SetHorizontalLineSpacing(int spacing) {
    m_AlignMode = spacing;
    m_Dirty = true;
}

// ASM-spec v1.6.1 Mortar::BakedStringBox::SetTranslation @0x00246238: (_Vector3<float>, bool preShift).
void BakedStringBox::SetTranslation(Vec3 pos, bool preShift) {
    Vec3 p = pos;
    if (preShift) {
        // ASM-spec v1.6.1 BakedStringBox::SetTranslation @0x00246238: preShift!=0 pre-shifts
        // -(boxW/2) in X, +(boxH/2) in Y, using SIGNED INT /2 (truncates: 75/2=37, not 37.5).
        // m_BoxWidth/m_BoxHeight are int; integer division truncates correctly.
        p.x -= (float)(m_BoxWidth  / 2);
        p.y += (float)(m_BoxHeight / 2);
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
        if (totalInkHeight < (float)m_BoxHeight) return;

        float nextSize = m_FontSize - 1.0f;
        if (nextSize < 6.0f) return;
        SetFontSize(nextSize);
    }
}

float BakedStringBox::TotalHeight() const {
    // Binary FitIntoVerticalBounds @ 0x00246fbc: totalInkHeight = maxBearingY(line0)
    // + (N-1)*step + (-minBottom(lineN-1)). step is already the full baseline pitch
    // (= (int)(fontSize + m_LineSpacing)); no separate inter-line spacing term.
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

// Measure-only word-wrap: tokenise `text` at `size` and greedy-wrap against `wrapLimit`.
// Returns lineCount and overflow (set when any line forced an unbreakable token whose
// advance > wrapLimit -- mirrors FitStringToWidth @0x00248734 / FitStrings @0x00246800 param_7=1).
// No vertex data is produced. Side effect: GetGlyph may add atlas entries at `size`.
// Binary: FitStrings @0x00246800, FitStringToWidth @0x00248734, GetFinalPointSize @0x002468fc.
static MeasureResult MeasureWrap(FontCacheObjectTTF* font, const char* text,
                                 float size, float wrapLimit)
{
    MeasureResult result = {0, false};

    std::vector<WordToken> words;
    {
        const char* p = text;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            if (*p == '\n') {
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
            const char* ws       = p;
            const char* lookahead = p;
            uint32_t firstCp = Mortar::utf8::decode_next_unicode_character(&lookahead);
            if (IsEastAsianChar(firstCp)) {
                WordToken tok;
                tok.start     = ws;
                tok.len       = (int)(lookahead - ws);
                tok.advance   = MeasureWord(font, ws, tok.len, size);
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
                tok.advance   = MeasureWord(font, ws, tok.len, size);
                tok.hardBreak = false;
                tok.cjk       = false;
                words.push_back(tok);
            }
        }
    }
    if (words.empty()) return result;

    const float spAdv = SpaceAdvance(font, size);

    size_t wi = 0;
    while (wi < words.size()) {
        if (words[wi].hardBreak) { wi++; continue; }
        size_t lineStart = wi;
        float  lineWidth = 0.0f;
        size_t lineEnd   = lineStart;
        while (lineEnd < words.size()) {
            if (words[lineEnd].hardBreak) { lineEnd++; break; }
            float needed = words[lineEnd].advance;
            if (lineEnd > lineStart && !words[lineEnd].cjk && !words[lineEnd - 1].cjk) {
                needed += spAdv;
            }
            if (lineWidth + needed > wrapLimit && lineEnd > lineStart) break;
            lineWidth += needed;
            lineEnd++;
        }
        if (lineEnd == lineStart) {
            if (words[lineStart].advance > wrapLimit) result.overflow = true;
            lineEnd = lineStart + 1;
        }
        result.lineCount++;
        wi = lineEnd;
    }

    return result;
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

    if (!m_Font || !m_Text || m_Text[0] == '\0') {
        return;
    }

    // ASM-spec v1.6.1 BakedStringBox::RebuildMeshes @0x002469c0: shrink font (from
    // m_BaseFontSize, step 1.0, floor 6.0) until lineCount<=m_MaxLines && no overflow,
    // then build geometry.
    // Helpers: FitStrings @0x00246800, FitStringToWidth @0x00248734,
    //          GetFinalPointSize @0x002468fc (standalone copy of the same loop).
    {
        const int cap = (m_MaxLines < 1) ? 999999 : m_MaxLines;
        m_FontSize = m_BaseFontSize;
        for (;;) {
            MeasureResult mr = MeasureWrap(m_Font, m_Text, m_FontSize, (float)m_BoxWidth);
            if ((mr.lineCount <= cap && !mr.overflow) || m_FontSize <= 6.0f) break;
            m_FontSize -= 1.0f;
        }
    }

    const float requestedSize = m_FontSize;
    if (requestedSize < 1.0f) {
        return;
    }

    const float wrapLimit  = (float)m_BoxWidth;
    // m_GradTop is the primary fill colour (binary m_FillTop +0x7c). SetColour writes
    // m_GradTop; Layout() uses it for vertex colours. BakeGradient() overwrites for mode>=2.
    const uint32_t packed  = m_GradTop.PlatformColour();

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
                    // Shadow-blur support (#257): remember codepoint + pre-bearing pen X so
                    // Draw()'s shadow pass can refetch a BLUR-effect glyph at the same pen slot.
                    line.glyphCodepoints.push_back(cp);
                    line.glyphPenX.push_back(curX);

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
        // step = (int)(m_CurrentFontSize + (m_LineSpacing - (m_BaseFontSize - m_CurrentFontSize)*0.5))
        // Binary truncates (C-cast to int), not rounds. m_BaseFontSize tracks the initial
        // or last-SetFontSize size; after FitIntoVerticalBounds+SetFontSize both are equal
        // so the shrink term is 0 and step = (int)(fontSize + m_LineSpacing).
        float diffShrink = m_BaseFontSize - requestedSize;
        float step = (float)(int)(requestedSize + ((float)m_LineSpacing - diffShrink * 0.5f));

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
        // ASM-spec v1.6.1 BakedStringBox::RebuildAlignments @0x00245c78: top-anchored
        // ink-center = translationY - step/2 (descent sign was flipped; kept in sync with
        // ComputeBaselineY's top-anchored branch).
        const BakedStringBoxLine& l0 = m_Lines[0];
        float ascentSpan = l0.maxBearingY - l0.minBottom;
        float descent    = -l0.minBottom;
        baselineY = -(ascentSpan * 0.5f) - step * 0.5f + descent;
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
        m_GradMode != 2 || m_MetallicFlag != 0) {
        m_GradMode = 2;
        m_GradTop = top;
        m_GradBottom = bottom;
        m_MetallicFlag = 0;
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
        m_GradMode != 4 || m_MetallicFlag != 1) {
        m_GradMode      = 4;
        m_MetallicFlag  = 1;
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
        // ASM-spec v1.6.1 BakedStringBox::RebuildAlignments @0x00245c78: top-anchored
        // ink-center = translationY - step/2 (descent sign was flipped).
        float ascentSpan = maxBearingY - minBottom;
        float descent    = -minBottom;
        return -(ascentSpan * 0.5f) - step * 0.5f + descent;
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

    // ASM-spec v1.6.1 BakedStringBox::Draw @0x00246e20 (per-line dispatch; matrix via
    // BakedStringTTF::Draw @0x002497a8): each pass below sets the world matrix per line
    // as T(anchor[+shadowOffset]) * RotZ(rotation) * ScaleRows(scale) * TranslateLocal(0,localBaseY,0),
    // algebraically identical to the old per-vertex CPU formula
    //   x' = cosT*(x*scale.x) - sinT*((y+localBaseY)*scale.y) + anchor.x[+sdx]
    //   y' = sinT*(x*scale.x) + cosT*((y+localBaseY)*scale.y) + anchor.y[+sdy]
    // No box-level Push/Pop wrapper -- the binary never wraps at box level; each
    // per-line Reset() below handles it (mirrors BakedStringTTF::Draw's world.Reset()).
    // Port specific: MatrixManager/MatrixStack aren't linked by the FN_GL_STUB
    // unit-test build (which only exercises ComputeBaselineY, never Draw's render
    // path); `world` is declared under the same guard as its 3 per-pass call
    // sites below so the stub build never references MatrixManager/MatrixStack.
    // Guard is FN_GL_STUB only (NOT __bada__ too) -- this is the real binary-faithful
    // matrix path (mirrors BakedStringTTF::Draw @0x002497a8, which has no such guard
    // at all) and must run on bada production + the asm-verify cross-build, unlike
    // the host-only DIFFERS/debug blocks below that legitimately exclude __bada__.
    // Renderer IS stubbed under FN_GL_STUB, so `renderer` stays unguarded.
#if !defined(FN_GL_STUB)
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
#endif

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
    // Each shadow glyph is a SEPARATELY-RASTERISED BLUR-effect glyph (v1.6.1 RenderGlyph
    // @0x0024f5dc, BuildBlur @0x0024f030) refetched via FontCacheObjectTTF::GetGlyph(cp,
    // size, FONT_EFFECT_BLUR, radius) -- not a solid copy of the sharp glyph mesh.
    // ASM-verified: 2026-07-08T00:00Z v1.6.1 BakedStringTTF::BakedStringTTF @ 0x00249a5c
    //   (blur radius = ceil(effectScale * FontInterface.m_FontScale @+0xc); FontInterface::Initialize
    //   @0x00250470 sets +0xc=m_FontScale, +0x10=m_InvFontScale) (asm-inspector)
    //   radius = clamp(ceil(m_ShadowScale * atlas->m_FontScale), 0, 32)  [RASTER px]
    {
        const bool doShadow = (!m_ShadowFlag && m_ShadowScale > 0.0f) ||
                              (m_ShadowFlag  && m_ShadowScale >= 0.0f);
        if (doShadow) {
            // Radius is an atlas/raster-pixel count -> scales by the forward m_FontScale (+0xc),
            // NOT the inverse. The binary uses m_InvFontScale (+0x10) only for the shadow *offset*.
            FontInterface* shadowAtlas = m_Font->GetAtlas();
            float fontScale = shadowAtlas ? shadowAtlas->m_FontScale : 1.0f;
            float radF = ceilf(m_ShadowScale * fontScale);
            if (radF < 0.0f) radF = 0.0f;
            if (radF > 32.0f) radF = 32.0f;
            const int shadowRadius = (int)radF;

            // Pre-render pass: create/cache every blurred glyph this line set needs
            // BEFORE the atlas upload below, mirroring Layout()'s pre-render-then-
            // BuildPendingTextures pattern for the sharp glyphs. Without this, a
            // newly-rasterised shadow glyph's atlas page would still be dirty when
            // the batched draw calls below bind it this frame (one-frame stale/blank
            // glyph on first use).
            for (size_t pli = 0; pli < m_Lines.size(); pli++) {
                const BakedStringBoxLine& pline = m_Lines[pli];
                for (size_t pgi = 0; pgi < pline.glyphCodepoints.size(); pgi++) {
                    m_Font->GetGlyph(pline.glyphCodepoints[pgi], m_FontSize,
                                     Mortar::FontCacheObjectTTF::FONT_EFFECT_BLUR,
                                     shadowRadius);
                }
            }
            if (shadowAtlas) shadowAtlas->BuildPendingTextures();

            const uint32_t shadowPacked = m_ShadowCol.PlatformColour();
            const float sdx = m_ShadowOffset.x;
            const float sdy = m_ShadowOffset.y;
            for (size_t li = 0; li < m_Lines.size(); li++) {
                const BakedStringBoxLine& sline = m_Lines[li];
                const size_t numShadowGlyphs = sline.glyphCodepoints.size();
                if (numShadowGlyphs == 0) continue;
                const float localBaseY = baselineY - (float)li * step;

                // Build the blurred-glyph shadow mesh for this line (own UVs/quad sizes --
                // NOT sline.verts, which holds the sharp glyph mesh).
                std::vector<QUADCUSTOMVERTEX> wv;
                wv.reserve(numShadowGlyphs * 6);
                std::vector<uint32_t> shadowPageTexIDs;
                shadowPageTexIDs.reserve(numShadowGlyphs);

                for (size_t gi = 0; gi < numShadowGlyphs; gi++) {
                    const GlyphAtlasEntry* g = m_Font->GetGlyph(sline.glyphCodepoints[gi], m_FontSize,
                                                                Mortar::FontCacheObjectTTF::FONT_EFFECT_BLUR,
                                                                shadowRadius);
                    if (!g || g->width <= 0.0f || g->height <= 0.0f) continue;
                    const float penX = sline.glyphPenX[gi];
                    const float x0 = penX + g->bearingX;
                    const float y1 = g->bearingY;
                    const float x1 = x0 + g->width;
                    const float y0 = y1 - g->height;

                    QUADCUSTOMVERTEX v[6] = {
                        { x0, y0, 0.f, 0,0,1, shadowPacked, g->u0, g->v1 },
                        { x0, y1, 0.f, 0,0,1, shadowPacked, g->u0, g->v0 },
                        { x1, y0, 0.f, 0,0,1, shadowPacked, g->u1, g->v1 },
                        { x1, y1, 0.f, 0,0,1, shadowPacked, g->u1, g->v0 },
                    };
                    v[4] = v[3];
                    v[5] = v[3];
                    for (int k = 0; k < 6; k++) wv.push_back(v[k]);
                    shadowPageTexIDs.push_back((uint32_t)g->pageTextureID);
                }
                if (wv.empty()) continue;

                const int nVerts = (int)wv.size();
                // Wire degenerate connector between glyphs in the tri-strip (unchanged
                // topology patch -- NOT a transform; operates on local coordinates now).
                for (int gi2 = 1; gi2 * 6 < nVerts; gi2++) {
                    wv[gi2 * 6 - 1] = wv[gi2 * 6];
                }
                // Matrix-driven per-line transform (shadow anchor includes m_ShadowOffset).
                // Port specific: guarded -- not linked in the FN_GL_STUB unit-test build.
#if !defined(FN_GL_STUB)
                world.Reset();
                world.TranslateLocal(Vec3(0.0f, localBaseY, 0.0f));
                world.ScaleRows(scale.x, scale.y, 1.0f);
                world.RotZ(rotation);
                world.Translate(Vec3(anchor.x + sdx, anchor.y + sdy, anchor.z));
                MatrixManager::GetInstance().UploadModelViewOnly();
#endif
                // Per-page batch: draw consecutive same-page glyph runs.
                {
                    const int numGlyphs = (int)shadowPageTexIDs.size();
                    int gIdx = 0;
                    while (gIdx < numGlyphs) {
                        uint32_t curTex = shadowPageTexIDs[gIdx];
                        int runStart = gIdx;
                        while (gIdx < numGlyphs && shadowPageTexIDs[gIdx] == curTex) gIdx++;
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
    // ASM-spec v1.6.1 FancyBakedString::Draw @0x0024b8e4: m_pGlow (stroke) drawn at drawPos,
    // BEHIND m_pMain -- a single SDF-outlined glyph (RenderGlyph @0x0024f5dc effect==1 STROKE,
    // BuildStrokes @0x0024edb8), same pad-then-rasterise pattern as the shadow BLUR pass above.
    // Radius reuses atlas->m_FontScale exactly like the shadow pass (BakedStringTTF ctor
    // @0x00249a5c formula), not a separate stroke-specific scale field.
    // Dead in v1.6.1: INNER_GLOW layer (BuildInnerGlow @0x0024f27c) has no setter writing its
    //   gate (+0x68) -> zero call sites; not ported.
    // ASM-spec v1.6.1 BakedStringBox::RebuildMeshes @0x002469c0: per line, m_StrokeMode(+0x58)
    //   2 -> ApplyStrokeGradient(Col0,Col1); 3 -> ApplyStrokeGradient(Col0,Col1,Col2); 1 -> solid Col0.
    // ASM-spec v1.6.1 FancyBakedString::ApplyStrokeGradient @0x0024afb0 (2-arg): top=Col0, bottom=Col1,
    //   per-vertex top->bottom lerp over the glow(stroke) layer's own mesh Y-bbox, per wrapped line.
    // ASM-spec v1.6.1 FancyBakedString::ApplyStrokeGradient @0x0024b010 (3-arg): base Col0->Col2 then
    //   split at 0.5 -> Col1 on the upper half.
    if (m_StrokeWidth > 0.0f && m_StrokeCount >= 1) {
        FontInterface* strokeAtlas = m_Font->GetAtlas();
        float strokeFontScale = strokeAtlas ? strokeAtlas->m_FontScale : 1.0f;
        float strokeRadF = ceilf(m_StrokeWidth * strokeFontScale);
        if (strokeRadF < 0.0f) strokeRadF = 0.0f;
        if (strokeRadF > 32.0f) strokeRadF = 32.0f;
        const int strokeRadius = (int)strokeRadF;

        // Pre-render pass, mirroring the shadow pass above: build/cache every stroke
        // glyph this line set needs before the batched upload+draw below.
        for (size_t pli = 0; pli < m_Lines.size(); pli++) {
            const BakedStringBoxLine& pline = m_Lines[pli];
            for (size_t pgi = 0; pgi < pline.glyphCodepoints.size(); pgi++) {
                m_Font->GetGlyph(pline.glyphCodepoints[pgi], m_FontSize,
                                 Mortar::FontCacheObjectTTF::FONT_EFFECT_STROKE,
                                 strokeRadius);
            }
        }
        if (strokeAtlas) strokeAtlas->BuildPendingTextures();

        const uint32_t strokePacked = m_StrokeCol0.PlatformColour();
        for (size_t li = 0; li < m_Lines.size(); li++) {
            const BakedStringBoxLine& sline = m_Lines[li];
            const size_t numStrokeGlyphs = sline.glyphCodepoints.size();
            if (numStrokeGlyphs == 0) continue;
            const float localBaseY = baselineY - (float)li * step;

            // Build the SDF-outlined stroke mesh for this line (own UVs/quad sizes --
            // NOT sline.verts, which holds the sharp glyph mesh).
            std::vector<QUADCUSTOMVERTEX> wv;
            wv.reserve(numStrokeGlyphs * 6);
            std::vector<uint32_t> strokePageTexIDs;
            strokePageTexIDs.reserve(numStrokeGlyphs);

            for (size_t gi = 0; gi < numStrokeGlyphs; gi++) {
                const GlyphAtlasEntry* g = m_Font->GetGlyph(sline.glyphCodepoints[gi], m_FontSize,
                                                            Mortar::FontCacheObjectTTF::FONT_EFFECT_STROKE,
                                                            strokeRadius);
                if (!g || g->width <= 0.0f || g->height <= 0.0f) continue;
                const float penX = sline.glyphPenX[gi];
                const float x0 = penX + g->bearingX;
                const float y1 = g->bearingY;
                const float x1 = x0 + g->width;
                const float y0 = y1 - g->height;

                QUADCUSTOMVERTEX v[6] = {
                    { x0, y0, 0.f, 0,0,1, strokePacked, g->u0, g->v1 },
                    { x0, y1, 0.f, 0,0,1, strokePacked, g->u0, g->v0 },
                    { x1, y0, 0.f, 0,0,1, strokePacked, g->u1, g->v1 },
                    { x1, y1, 0.f, 0,0,1, strokePacked, g->u1, g->v0 },
                };
                v[4] = v[3];
                v[5] = v[3];
                for (int k = 0; k < 6; k++) wv.push_back(v[k]);
                strokePageTexIDs.push_back((uint32_t)g->pageTextureID);
            }
            if (wv.empty()) continue;

            const int nVerts = (int)wv.size();

            // 2/3-colour stroke gradient: per-line, over this line's own stroke mesh Y-bbox
            // (pre-transform local Y, matching the binary's per-FancyBakedString-instance scope).
            if (m_StrokeCount >= 2) {
                float yTop = wv[0].y, yBot = wv[0].y;
                for (int i = 1; i < nVerts; i++) {
                    if (wv[i].y > yTop) yTop = wv[i].y;
                    if (wv[i].y < yBot) yBot = wv[i].y;
                }
                float range = yTop - yBot;
                if (range < 1.0f) range = 1.0f;
                const Colour& topC = m_StrokeCol0;
                const Colour& botC = (m_StrokeCount == 3) ? m_StrokeCol2 : m_StrokeCol1;
                const float mid = 0.5f * (yTop + yBot);
                for (int i = 0; i < nVerts; i++) {
                    const float y = wv[i].y;
                    const float t = (y >= yTop || y < yBot) ? 0.0f : (yTop - y) / range;
                    int r = (int)(((topC.r / 255.0f) * (1.0f - t) + (botC.r / 255.0f) * t) * 255.0f);
                    int g = (int)(((topC.g / 255.0f) * (1.0f - t) + (botC.g / 255.0f) * t) * 255.0f);
                    int b = (int)(((topC.b / 255.0f) * (1.0f - t) + (botC.b / 255.0f) * t) * 255.0f);
                    int a = (int)(((topC.a / 255.0f) * (1.0f - t) + (botC.a / 255.0f) * t) * 255.0f);
                    if (m_StrokeCount == 3 && y > mid) {
                        r = m_StrokeCol1.r;
                        g = m_StrokeCol1.g;
                        b = m_StrokeCol1.b;
                        a = m_StrokeCol1.a;
                    }
                    wv[i].colour = Colour((unsigned char)r, (unsigned char)g,
                                           (unsigned char)b, (unsigned char)a).PlatformColour();
                }
            }

            // Wire degenerate connector between glyphs in the tri-strip (unchanged
            // topology patch -- NOT a transform; operates on local coordinates now).
            for (int gi2 = 1; gi2 * 6 < nVerts; gi2++) {
                wv[gi2 * 6 - 1] = wv[gi2 * 6];
            }
            // Matrix-driven per-line transform (mirrors BakedStringTTF::Draw @0x002497a8).
            // Port specific: guarded -- not linked in the FN_GL_STUB unit-test build.
#if !defined(FN_GL_STUB)
            world.Reset();
            world.TranslateLocal(Vec3(0.0f, localBaseY, 0.0f));
            world.ScaleRows(scale.x, scale.y, 1.0f);
            world.RotZ(rotation);
            world.Translate(anchor);
            MatrixManager::GetInstance().UploadModelViewOnly();
#endif
            // Per-page batch: draw consecutive same-page glyph runs.
            {
                const int numGlyphs = (int)strokePageTexIDs.size();
                int gIdx = 0;
                while (gIdx < numGlyphs) {
                    uint32_t curTex = strokePageTexIDs[gIdx];
                    int runStart = gIdx;
                    while (gIdx < numGlyphs && strokePageTexIDs[gIdx] == curTex) gIdx++;
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, (GLuint)curTex);
                    glEnable(GL_TEXTURE_2D);
                    TexEnvModulate();
                    renderer->DrawTriStrip(&wv[runStart * 6], (gIdx - runStart) * 6);
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

        // Local (untransformed) copy: still needed for the tri-strip degenerate-join
        // patch below (topology fix, NOT a transform -- DrawTriStrip() also requires
        // a non-const buffer). The rotate/scale/translate math is gone; the matrix
        // set right after handles it on the GPU.
        std::vector<QUADCUSTOMVERTEX> wv(line.verts);

        // Wire degenerate connector between glyphs in the tri-strip.
        for (int gi = 1; gi * 6 < nVerts; gi++) {
            wv[gi * 6 - 1] = wv[gi * 6];
        }

        // Matrix-driven per-line transform (mirrors BakedStringTTF::Draw @0x002497a8).
        // Port specific: guarded -- not linked in the FN_GL_STUB unit-test build.
#if !defined(FN_GL_STUB)
        world.Reset();
        world.TranslateLocal(Vec3(0.0f, localBaseY, 0.0f));
        world.ScaleRows(scale.x, scale.y, 1.0f);
        world.RotZ(rotation);
        world.Translate(anchor);
        MatrixManager::GetInstance().UploadModelViewOnly();
#endif

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

        // Port specific: accumulate ink bounds for the debug overlay. wv is local
        // (untransformed) now that the actual draw is matrix-driven, so re-derive
        // world-space corners here for the overlay only -- host debug tooling,
        // no binary counterpart, independent of the GPU matrix set above.
#if !defined(__bada__) && !defined(FN_GL_STUB)
        if (FN::g_DebugHitboxes && !FN::g_SuppressTextOverlay) {
            const float dbgTheta = rotation * (3.14159265f / 180.0f);
            const float dbgSinT  = sinf(dbgTheta);
            const float dbgCosT  = cosf(dbgTheta);
            for (int i = 0; i < nVerts; i++) {
                const float lx = wv[i].x * scale.x;
                const float ly = (wv[i].y + localBaseY) * scale.y;
                const float wx = dbgCosT * lx - dbgSinT * ly + anchor.x;
                const float wy = dbgSinT * lx + dbgCosT * ly + anchor.y;
                if (!dbgHasInk) {
                    dbgInkX0 = dbgInkX1 = wx;
                    dbgInkY0 = dbgInkY1 = wy;
                    dbgHasInk = true;
                } else {
                    if (wx < dbgInkX0) dbgInkX0 = wx;
                    if (wx > dbgInkX1) dbgInkX1 = wx;
                    if (wy < dbgInkY0) dbgInkY0 = wy;
                    if (wy > dbgInkY1) dbgInkY1 = wy;
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
        const bool hasBox = (m_BoxWidth > 0);
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
}

// SetWorldspaceClipping  binary @ 0x00245c28 (real body)
// ASM-spec v1.6.1 Mortar::BakedStringBox::SetWorldspaceClipping @0x00245c28: (int x0, int y0, int w, int h).
// Stores clip values as int (+0x94..+0xa0), sets m_HasClip(+0xa4)=true, m_Visible(+0x01)=true.
// Binary also checks FontInterface::GetInstance()[+0x150] (float y-scale): if == -1.0f,
// negates m_ClipY0. Port: FontInterface is port-specific (no singleton/+0x150 field);
// TODO: v1.6.1 BakedStringBox::SetWorldspaceClipping @0x00245c28 -- y-sign flip via
//   FontInterface::GetInstance()[+0x150] == -1.0f; skipped (no binary FontInterface singleton in port).
// ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0: args (-240, -46, 400, 108).
// Args: x0/y0 = top-left corner in worldspace; w/h = width/height (not far corner).
void BakedStringBox::SetWorldspaceClipping(int x0, int y0, int w, int h) {
    m_ClipX0 = x0;
    m_ClipY0 = y0;
    m_ClipW  = w;
    m_ClipH  = h;
    m_HasClip = true;
    m_Visible = true;
}

// Update  binary @ 0x0015ab80 (AddLine call site @0x0015aaf0)
// ASM-spec v1.6.1 AboutScreen::AddLine @0x0015aaf0: called after SetWorldspaceClipping.
void BakedStringBox::Update() {
    if (m_Dirty) Layout();
}

// ReshapeBounds  binary @ 0x00245ab8 (v1.6.1 BakedStringBox::ReshapeBounds)
// Writes m_MaxLines=p3, m_BoxWidth=w, m_BoxHeight=h, m_LineSpacing=p4, m_Dirty=true unconditionally.
// Binary emits str (integer store) not vcvt/vstr -- fields are int.
// ASM-spec v1.6.1 Mortar::BakedStringBox::ReshapeBounds @0x00245ab8: (int, int, int, int).
// ASM-spec v1.6.1 BSButton::Init @0x0015ea40: ReshapeBounds(54,20,1,0) -> m_MaxLines=1.
void BakedStringBox::ReshapeBounds(int width, int height, int maxLines, int lineSpacing) {
    m_MaxLines    = maxLines;
    m_BoxWidth    = width;
    m_BoxHeight   = height;
    m_LineSpacing = lineSpacing;
    m_Dirty       = true;
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

// GetTextWidth  binary v1.6.1 BakedStringBox::GetBounds + MortarRectangleT::Width
// ASM-spec v1.6.1 BakedStringBox::GetBounds + MortarRectangleT::Width: binary returns
// integer pixel width; port lays out 1:1 world=pixel so float max-line-width is equivalent.
// Triggers a lazy Layout() if m_Dirty is set (same pattern as Draw()).
// Used by ShopListItem::DrawFloatingText @0x001b4bc8 to anchor NEW/SELECTED badges.
float BakedStringBox::GetTextWidth() const {
    if (m_Dirty) {
        const_cast<BakedStringBox*>(this)->Layout();
    }
    float maxW = 0.0f;
    for (size_t i = 0; i < m_Lines.size(); ++i) {
        if (m_Lines[i].width > maxW) maxW = m_Lines[i].width;
    }
    return maxW;
}

} // namespace Mortar
