#include "render/BakedStringBox.h"
#include "render/FancyBakedString.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontInterface.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "render/Renderer.h"
#include "render/Utf8StringIterator.h"
#include "math/Matrix44.h"
#include "math/_Vector2.h"
#include "math/_Vector3.h"
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

// One wrapped-line result from FitStrings(): the reconstructed line text (a raw
// substring of the original m_Text, preserving original inter-word spacing) plus
// the ink-extent metrics ComputeBaselineY needs. File-scope (not a class member)
// because sizeof(BakedStringBox) is pinned at 200B -- these are always recomputed
// on demand (RebuildMeshes / FitIntoVerticalBounds / TotalHeight / GetTextWidth),
// never cached on the box itself.
struct WrappedLineInfo {
    std::string text;
    // Horizontal align offset is NOT computed here -- ASM-spec v1.6.1 BakedStringBox::
    // RebuildAlignments @0x00245c78 reads it off the ALREADY-BUILT line's rendered mesh
    // bounds (FancyBakedString::GetBounds), which don't exist until RebuildMeshes builds
    // the per-line FancyBakedString. See RebuildMeshes.
    float maxBearingY;   // max bearingY across glyphs on this line (above baseline)
    float minBottom;     // min (bearingY-height) across glyphs on this line (<=0, below baseline)
    float advanceWidth;  // tight (baked-bearing/layoutX) pen-advance width, pre-ink-trim,
                          // matches the render pen -- used by GetTextWidth
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
    , m_ExtraParam(0)                         // +0xac
    , m_Extra1Colour(0, 0, 0, 0)              // +0xb0
    , m_Extra2Colour(0, 0, 0, 0)              // +0xb4
    // m_WrappedLines default-constructed     // +0xb8
    , m_FontSize(fontSize)                    // +0xc4
{
}

BakedStringBox::~BakedStringBox() {
    DeleteStrings();
    if (m_Text) {
        free(m_Text);
        m_Text = 0;
    }
}

// DeleteStrings -- delete + clear every FancyBakedString* in m_Lines.
void BakedStringBox::DeleteStrings() {
    for (size_t i = 0; i < m_Lines.size(); ++i) {
        delete m_Lines[i];
    }
    m_Lines.clear();
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
// FancyBakedString::ApplyGradient(colour) per line immediately (does NOT set m_Dirty --
// the existing lines are recoloured in place), else m_Dirty=true (lazy rebuild).
void BakedStringBox::SetColour(Colour colour, bool eager) {
    if (m_GradTop.r != colour.r || m_GradTop.g != colour.g ||
        m_GradTop.b != colour.b || m_GradTop.a != colour.a) {
        m_GradTop = colour;
        m_GradMode = 1;
        m_MetallicFlag = 0;
        if (eager) {
            for (size_t i = 0; i < m_Lines.size(); ++i) {
                if (m_Lines[i]) m_Lines[i]->ApplyGradient(colour);
            }
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
void BakedStringBox::SetTranslation(_Vector3<float> pos, bool preShift) {
    _Vector3<float> p = pos;
    if (preShift) {
        // ASM-spec v1.6.1 BakedStringBox::SetTranslation @0x00246238: preShift!=0 pre-shifts
        // -(boxW/2) in X, +(boxH/2) in Y, using SIGNED INT /2 (truncates: 75/2=37, not 37.5).
        // m_BoxWidth/m_BoxHeight are int; integer division truncates correctly.
        p.x -= (float)(m_BoxWidth  / 2);
        p.y += (float)(m_BoxHeight / 2);
    }
    // ASM-spec v1.6.1 BakedStringBox::SetTranslation @0x00246238: writes position
    // fields only; does NOT set m_Dirty. m_Pos is a draw-time translate anchor
    // consumed in Draw() as Vec3 anchor = m_Pos; it is never read by RebuildMeshes().
    // The previous port code set m_Dirty=true on position change, causing a full
    // re-layout + GL atlas upload every frame when a caller (e.g. MainScreen::Draw)
    // updates position each frame via SetTranslation (performance fix).
    m_Pos = p;
}

// Measure world-unit advance of a word (ASCII chars, length len) at requestedSize.
// ASM-spec v1.6.1 BakedStringTTF::FitStringToWidth @0x00248734: per-glyph wrap step =
//   m_GlyphScale.x (= layoutX = floor(advance/64) - bitmap_left) + tracking*fontScale + 1.0.
// tracking (m_Base[0x28]) ~= 0 for these plates, so the step is the TIGHT layoutX + 1.0 --
// identical to the render pen (penX += layoutX + 1.0) and GetTextWidth, so wrap breaks match
// the render + the binary. (Supersedes the old bare-advanceX workaround: layoutX = advanceX -
// bitmap_left is <= full advance, so "SLICE FRUIT" fits under boxW 75 by construction.)
static float MeasureWord(FontCacheObjectTTF* font, const char* ptr, int len,
                         float requestedSize) {
    float adv = 0.0f;
    const char* p   = ptr;
    const char* end = ptr + len;
    while (p < end) {
        uint32_t cp = Mortar::utf8::decode_next_unicode_character(&p);
        if (cp == 0) break;
        const GlyphAtlasEntry* g = font->GetGlyph(cp, requestedSize);
        if (g) adv += g->layoutX + 1.0f;
    }
    return adv;
}

static float SpaceAdvance(FontCacheObjectTTF* font, float requestedSize) {
    // Space is a real glyph in the binary's scan: layoutX + 1.0 (matches MeasureWord).
    const GlyphAtlasEntry* sp = font->GetGlyph((uint32_t)' ', requestedSize);
    return sp ? sp->layoutX + 1.0f : 0.0f;
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

// FitStrings -- word-wrap `text` at `requestedSize` into per-line WrappedLineInfo records:
// reconstructed line text (raw substring of `text`, preserving original inter-word
// spacing) + vertical ink-extent metrics (maxBearingY/minBottom) + raw advance width.
// No FancyBakedString/vertex data is produced -- callers decide whether to build line
// objects (RebuildMeshes) or just measure (FitIntoVerticalBounds, TotalHeight, GetTextWidth).
// Does NOT compute the horizontal align offset -- ASM-spec v1.6.1 BakedStringBox::
// RebuildAlignments @0x00245c78 reads that off the ALREADY-BUILT line's rendered mesh
// bounds (FancyBakedString::GetBounds), so RebuildMeshes computes it after constructing
// each line, not here.
// ASM-spec v1.6.1 Mortar::BakedStringBox::FitStrings @0x00246800 (line splitting).
static void FitStrings(FontCacheObjectTTF* font, const char* text, float requestedSize,
                       int boxWidth, std::vector<WrappedLineInfo>& outLines)
{
    outLines.clear();
    if (!font || !text || text[0] == '\0') return;

    const float wrapLimit = (float)boxWidth;

    // Tokenise into logical lines split by '\n', then words within each line.
    std::vector<WordToken> words;
    {
        const char* p = text;
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
                tok.advance   = MeasureWord(font, ws, tok.len, requestedSize);
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
                tok.advance   = MeasureWord(font, ws, tok.len, requestedSize);
                tok.hardBreak = false;
                tok.cjk       = false;
                words.push_back(tok);
            }
        }
    }
    if (words.empty()) return;

    const float spAdv = SpaceAdvance(font, requestedSize);

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

        // Vertical ink-extent + text-span pass. Pen starts at 0; used for the vertical
        // metrics (bearingY/height) and the tight advance width. Horizontal align offset
        // is NOT computed here -- see RebuildMeshes (needs the built line's rendered bounds).
        float penX = 0.0f;
        bool firstWordInk = true;
        size_t prevInkWj = lineStart; // last non-hardBreak word processed
        float lineMaxBearingY = 0.0f;
        float lineMinBottom   = 0.0f;
        const char* spanStart = 0;
        const char* spanEnd   = 0;
        for (size_t wj = lineStart; wj < lineEnd; wj++) {
            if (words[wj].hardBreak) continue;
            if (!firstWordInk) {
                // v1.6.1 WordWrap::CanBreakLineAt @ 0x002509cc: no space between adjacent CJK tokens
                if (!words[wj].cjk && !words[prevInkWj].cjk) penX += spAdv;
            } else {
                spanStart = words[wj].start;
            }
            spanEnd = words[wj].start + words[wj].len;
            prevInkWj = wj;
            firstWordInk = false;

            const char* wp    = words[wj].start;
            const char* wpEnd = wp + words[wj].len;
            while (wp < wpEnd) {
                uint32_t cp = Mortar::utf8::decode_next_unicode_character(&wp);
                if (cp == 0) break;
                const GlyphAtlasEntry* g = font->GetGlyph(cp, requestedSize);
                if (!g) continue;
                // ASM-spec v1.6.1 BakedStringBox::RebuildMeshes @0x00246944: measures via
                // the same baked-bearing LeftJustify the render path uses (BakedStringTTF::
                // GetKerning @0x0024ea78 returns m_GlyphScale.x = layoutX; FullInternalRebuild
                // places ink AT the pen, no separate bearingX add). Was: penX += advanceX
                // (full advance) + ink at penX+bearingX -- looser than the render, causing
                // shrink/alignment decisions to diverge from what actually gets drawn.
                if (g->width > 0.0f && g->height > 0.0f) {
                    if (g->bearingY > lineMaxBearingY) lineMaxBearingY = g->bearingY;
                    float bottom = g->bearingY - g->height;
                    if (bottom < lineMinBottom) lineMinBottom = bottom;
                }
                penX += g->layoutX + 1.0f;
            }
        }

        WrappedLineInfo info;
        if (spanStart && spanEnd && spanEnd > spanStart) {
            info.text.assign(spanStart, (size_t)(spanEnd - spanStart));
        }
        info.maxBearingY  = lineMaxBearingY;
        info.minBottom    = lineMinBottom;
        // advanceWidth: tight (layoutX-stepped) pen total from the ink loop above, NOT
        // the loose greedy-fill `lineWidth` (MeasureWord/advanceX) used only to decide
        // where to BREAK the line. GetTextWidth()/FitStringToWidth callers (e.g.
        // ShopListItem::DrawFloatingText positioning NEW/SELECTED badges against
        // m_pBox0/m_pBox1) need the width that MATCHES what actually gets drawn.
        info.advanceWidth = penX;
        outLines.push_back(info);

        wi = lineEnd;
    }
}

void BakedStringBox::FitIntoVerticalBounds() {
    if (!m_Font) return;
    // ASM-spec v1.6.1 BakedStringBox::FitIntoVerticalBounds @ 0x00246fbc:
    // Shrinks m_FontSize in 1.0-px steps until total ink height < m_BoxHeight (HEIGHT predicate,
    // NOT line-count). Binary loop:
    //   RebuildMeshes(); N = numLines; if (N == 0) return;
    //   step = per-line pitch (RebuildAlignments @ 0x00245c78)
    //   totalInkHeight = maxBearingY(line0) + (N-1)*step + (-minBottom(lineN-1))
    //   if (totalInkHeight < m_BoxHeight) return;  // fits
    //   nextSize = m_FontSize - 1.0f;
    //   if (nextSize < 6.0f) return;               // floor: stop without applying the too-small size
    //   SetFontSize(nextSize);  // writes BOTH m_FontSize AND m_BaseFontSize, marks dirty
    //
    // No per-line ink-extent cache survives on a built FancyBakedString line (sizeof(box)
    // is pinned at 200B), so this re-measures via FitStrings after each RebuildMeshes call
    // rather than reading it back off the line objects -- mirrors the binary's likely
    // multiple near-duplicate measurement loops (GetFinalPointSize is already documented
    // as "a standalone copy of the same loop").
    for (;;) {
        RebuildMeshes();
        int N = (int)m_Lines.size();
        if (N == 0) return;

        std::vector<WrappedLineInfo> wl;
        FitStrings(m_Font, m_Text, m_FontSize, m_BoxWidth, wl);
        if (wl.empty()) return;

        float diffShrink = m_BaseFontSize - m_FontSize;
        float step = (float)(int)(m_FontSize + ((float)m_LineSpacing - diffShrink * 0.5f));
        float totalInkHeight = wl[0].maxBearingY
                             + (float)(N - 1) * step
                             + (-wl[N - 1].minBottom);
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
    std::vector<WrappedLineInfo> wl;
    FitStrings(m_Font, m_Text, m_FontSize, m_BoxWidth, wl);
    int N = (int)wl.size();
    if (N == 0) return 0.0f;
    float diffShrink = m_BaseFontSize - m_FontSize;
    float step = (float)(int)(m_FontSize + ((float)m_LineSpacing - diffShrink * 0.5f));
    return wl[0].maxBearingY
         + (float)(N - 1) * step
         + (-wl[N - 1].minBottom);
}

// RebuildMeshes -- thin per-line FancyBakedString dispatcher build path.
// ASM-spec v1.6.1 Mortar::BakedStringBox::RebuildMeshes @0x002469c0: shrink-by-linecount
// loop (FitStrings/MeasureWrap, floor 6.0px) picks m_FontSize; DeleteStrings() frees the
// previous line set; then one FancyBakedString per wrapped line is constructed (17-arg
// ctor), the active gradient/stroke-gradient is applied, and each line's LOCAL draw
// offset is written into its own LineOffset(): vertical baseline via ComputeBaselineY
// (unchanged), horizontal align offset via the just-built line's rendered GetBounds
// extent (ASM-spec v1.6.1 RebuildAlignments @0x00245c78 -- see the per-line loop below).
void BakedStringBox::RebuildMeshes() {
    DeleteStrings();
    m_WrappedLines.clear();
    m_Dirty = false;

    if (!m_Font || !m_Text || m_Text[0] == '\0') {
        return;
    }

    // Shrink font (from m_BaseFontSize, step 1.0, floor 6.0) until lineCount<=m_MaxLines
    // && no overflow. Helpers: FitStringToWidth @0x00248734, GetFinalPointSize @0x002468fc
    // (standalone copy of the same loop).
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

    // Pre-render every codepoint in the string so atlas UVs are populated before the
    // per-line FancyBakedString ctors below (which each re-measure/re-fetch the same
    // glyphs at construction time).
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

    std::vector<WrappedLineInfo> wl;
    FitStrings(m_Font, m_Text, requestedSize, m_BoxWidth, wl);
    if (wl.empty()) return;

    const int nLines = (int)wl.size();

    // ASM-spec v1.6.1 BakedStringBox::RebuildAlignments @ 0x00245c78:
    // step = (int)(m_CurrentFontSize + (m_LineSpacing - (m_BaseFontSize - m_CurrentFontSize)*0.5))
    // Binary truncates (C-cast to int), not rounds. m_BaseFontSize tracks the initial
    // or last-SetFontSize size; after FitIntoVerticalBounds+SetFontSize both are equal
    // so the shrink term is 0 and step = (int)(fontSize + m_LineSpacing).
    float diffShrink = m_BaseFontSize - requestedSize;
    float step = (float)(int)(requestedSize + ((float)m_LineSpacing - diffShrink * 0.5f));

    // maxSpan = max(maxBearingY - minBottom) across all lines -- multi-line centre-V input.
    float maxSpan = 0.0f;
    for (int i = 0; i < nLines; i++) {
        float span = wl[i].maxBearingY - wl[i].minBottom;
        if (span > maxSpan) maxSpan = span;
    }

    for (int i = 0; i < nLines; i++) {
        m_WrappedLines.push_back(wl[i].text);

        FancyBakedString* line = new FancyBakedString(
            m_Font, wl[i].text.c_str(), m_FontSize,
            m_GradTop, m_AlignMode, 0.0f,
            m_StrokeWidth, m_StrokeCol0,
            m_ShadowScale, m_ShadowCol,
            m_StrokeLayerWidth, m_StrokeLayerColour,
            m_ShadowFlag, m_ExtraWidth, m_ExtraParam,
            m_Extra1Colour, m_Extra2Colour);

        // ASM-spec v1.6.1 BakedStringBox::RebuildMeshes @0x002469c0: m_MetallicFlag ->
        // ApplyMetallicGradient; else m_ColourMode==2 -> ApplyGradient(top,bottom);
        // ==3 -> ApplyGradient(top,mid,bottom). Mode 1 (solid) needs no extra call --
        // mainCol already carries m_GradTop from the ctor above.
        if (m_MetallicFlag) {
            line->ApplyMetallicGradient(m_GradTop, m_GradBottom, m_GradCol2, m_GradCol3);
        } else if (m_GradMode == 2) {
            line->ApplyGradient(m_GradTop, m_GradBottom);
        } else if (m_GradMode == 3) {
            // 3-colour SetGradient(top,mid,bottom) @0x00245780: top/mid/bottom =
            // m_GradTop/m_GradBottom/m_GradCol2 (binary reuses m_FillBottom for MID).
            line->ApplyGradient(m_GradTop, m_GradBottom, m_GradCol2);
        }

        // ASM-spec v1.6.1 BakedStringBox::RebuildMeshes @0x002469c0: m_StrokeCount(+0x58)
        // 2 -> ApplyStrokeGradient(Col0,Col1); 3 -> ApplyStrokeGradient(Col0,Col1,Col2);
        // 1 -> solid Col0 (already the glowCol passed to the ctor above; no extra call).
        if (m_StrokeCount == 2) {
            line->ApplyStrokeGradient(m_StrokeCol0, m_StrokeCol1);
        } else if (m_StrokeCount == 3) {
            line->ApplyStrokeGradient(m_StrokeCol0, m_StrokeCol1, m_StrokeCol2);
        }

        // Per-line LOCAL draw offset (reuses ComputeBaselineY unchanged from the
        // pre-restructure Layout()/Draw() vertical-baseline math).
        //
        // ASM-spec v1.6.1 BakedStringBox::RebuildAlignments @0x00245c78 (align width =
        // rendered GetBounds extent): the horizontal offset uses w = |GetBounds.right -
        // GetBounds.left| read off the line JUST CONSTRUCTED above (its main layer is
        // built eagerly by the BakedStringTTF ctor -> FullInternalRebuild, so bounds are
        // already the actual rendered mesh extent -- cellW+1 based, not the ink/bitmap
        // width). Was: lineInkWidth from FitStrings' pre-render g->width scan, which
        // undercounts by ~1px/glyph vs what FinishMesh actually draws (cellW+1 per glyph),
        // drifting right-aligned text off the box edge by up to ~1px/glyph.
        float lineOffsetX = 0.0f;
        {
            const int horizAlign = m_Align & 0x3;
            if (horizAlign == 2 || horizAlign == 3) {
                MortarRectangleT<long>* bounds = line->GetBounds();
                const float w = bounds ? (float)bounds->Width() : 0.0f;
                if (horizAlign == 3) {
                    lineOffsetX = (float)m_BoxWidth * 0.5f - w * 0.5f;
                } else { // == 2: right
                    lineOffsetX = (float)m_BoxWidth - w;
                }
                lineOffsetX = (float)(int)lineOffsetX;
            }
            // 0 or 1: left-justified, lineOffsetX = 0.
        }

        float localBaseY = ComputeBaselineY(m_Align, nLines, i, wl[i].maxBearingY, wl[i].minBottom,
                                            (float)m_BoxHeight, step, maxSpan, requestedSize);
        line->LineOffset() = _Vector3<float>(lineOffsetX, localBaseY, 0.0f);

        m_Lines.push_back(line);
    }

    m_Visible = true;
}

// SetGradient  binary @ 0x0024566c
// ASM-spec v1.6.1 BakedStringBox::SetGradient @0x0024566c: perGlyph!=0 calls
// FancyBakedString::ApplyGradient(top,bottom) per existing line immediately (no m_Dirty).
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
        } else {
            for (size_t i = 0; i < m_Lines.size(); ++i) {
                if (m_Lines[i]) m_Lines[i]->ApplyGradient(top, bottom);
            }
        }
    }
}

// SetGradient (3-colour)  binary @ 0x00245780
// ASM-spec v1.6.1 BakedStringBox::SetGradient @0x00245780: 3-stop sibling of
// SetMetallicGradient -- TopBottom(top,bottom) + single Split(mid,0.5), vs metallic's
// TopBottom + Split(0.51) + Split(0.49). m_GradMode=3.
void BakedStringBox::SetGradient(Colour top, Colour mid, Colour bottom, bool perGlyph) {
    if (m_GradTop.r != top.r || m_GradTop.g != top.g || m_GradTop.b != top.b || m_GradTop.a != top.a ||
        m_GradBottom.r != mid.r || m_GradBottom.g != mid.g || m_GradBottom.b != mid.b || m_GradBottom.a != mid.a ||
        m_GradCol2.r != bottom.r || m_GradCol2.g != bottom.g || m_GradCol2.b != bottom.b || m_GradCol2.a != bottom.a ||
        m_GradMode != 3 || m_MetallicFlag != 0) {
        m_GradMode = 3;
        m_GradTop = top;
        m_GradBottom = mid;
        m_GradCol2 = bottom;
        m_MetallicFlag = 0;
        if (!perGlyph) {
            m_Dirty = true;
        } else {
            for (size_t i = 0; i < m_Lines.size(); ++i) {
                if (m_Lines[i]) m_Lines[i]->ApplyGradient(top, mid, bottom);
            }
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
            for (size_t i = 0; i < m_Lines.size(); ++i) {
                if (m_Lines[i]) m_Lines[i]->ApplyMetallicGradient(top, bottom, c2, c3);
            }
        } else {
            m_Dirty = true;
        }
    }
    (void)flag;
}

// SetShadow  binary @ 0x002462c0
// ASM-spec v1.6.1 Mortar::BakedStringBox::SetShadow @0x002462c0: (float, Colour, _Vector3<float>, int).
// Note: SetColour/SetTranslation use bool; SetShadow uses int.
void BakedStringBox::SetShadow(float scale, Colour col, _Vector3<float> offset, int flag) {
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
// Thin per-line dispatcher -- NO inline vertex work. All glyph rasterisation, shadow/
// glow/stroke/bevel layering, and gradient application happens inside FancyBakedString/
// BakedStringTTF (built by RebuildMeshes). This method only: lazily rebuilds, computes
// the box-level anchor, and for each line reads its precomputed LineOffset(), rotates/
// scales it, translates by the anchor, and calls FancyBakedString::Draw.
void BakedStringBox::Draw(_Vector2<float> scale, float rotation, bool center) {
    if (!m_Font) return;
    if (m_Dirty) RebuildMeshes();
    if (m_Lines.empty()) return;

    FontInterface* atlas = m_Font->GetAtlas();
    if (!atlas) return;
    atlas->BuildPendingTextures();

    // World-space anchor.
    _Vector3<float> anchor = m_Pos;

    // ASM-spec v1.6.1 BakedStringBox::Draw @0x00246e20: center recenters only the scale-shrink
    // delta (0 at scale=1); per-line centering is baked into LineOffset() by RebuildMeshes.
    //   anchor.x += boxW*0.5 - boxW*scale.x*0.5
    //   anchor.y -= boxH*0.5 - boxH*scale.y*0.5
    // At scale=(1,1) both correction terms are 0 -> anchor == m_Pos.
    if (center) {
        anchor.x += m_BoxWidth  * 0.5f - m_BoxWidth  * scale.x * 0.5f;
        anchor.y -= m_BoxHeight * 0.5f - m_BoxHeight * scale.y * 0.5f;
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
        // Stage-2 2D batching: raw scissor change bypasses Renderer::
        // SetClipRect, so drain pending 2D verts before it takes effect.
        if (Renderer* r = Renderer::GetInstance()) r->Flush2D();
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx, sy, sw, sh);
    }
#endif

    // Per-line dispatch: read LineOffset() (set by RebuildMeshes via ComputeBaselineY),
    // apply scale then rotation (matches the binary's per-vertex transform order: scale
    // first, rotate second), translate by the box anchor, hand off to FancyBakedString::
    // Draw with align=9 (bits0-1=1 "left", bits2-3=0x8 -- both fall through
    // BakedStringTTF::Draw's align-offset switch as no-ops, since the box has already
    // baked horizontal/vertical placement into LineOffset()).
    const bool hasRotation = (rotation != 0.0f);
    const float theta = rotation * (3.14159265f / 180.0f);
    const float sinT = hasRotation ? sinf(theta) : 0.0f;
    const float cosT = hasRotation ? cosf(theta) : 1.0f;

    for (size_t li = 0; li < m_Lines.size(); li++) {
        FancyBakedString* line = m_Lines[li];
        if (!line) continue;

        const _Vector3<float>& lo = line->LineOffset();
        float lx = lo.x * scale.x;
        float ly = lo.y * scale.y;
        float rx = lx, ry = ly;
        if (hasRotation) {
            rx = cosT * lx - sinT * ly;
            ry = sinT * lx + cosT * ly;
        }
        _Vector3<float> pos(rx + anchor.x, ry + anchor.y, lo.z * scale.x + anchor.z);
        line->Draw(pos, scale, rotation, (ALIGNMENT_TYPE)9);
    }

    // Port specific: box + anchor debug overlay. No per-vertex ink-bounds tracking
    // possible any more (FancyBakedString owns its own vertex data), so only the
    // declared box rect is drawn -- pass hasInk=false rather than faking an ink
    // rect equal to the box rect (that used to draw the same rectangle twice,
    // once green once yellow, on top of itself). No binary counterpart; host
    // debug tooling only.
#if !defined(__bada__) && !defined(FN_GL_STUB)
    if (FN::g_DebugHitboxes >= 3 && !FN::g_SuppressTextOverlay) {
        const bool hasBox = (m_BoxWidth > 0);
        const float bx0 = anchor.x - m_BoxWidth  * 0.5f;
        const float bx1 = anchor.x + m_BoxWidth  * 0.5f;
        const float by0 = anchor.y - m_BoxHeight * 0.5f;
        const float by1 = anchor.y + m_BoxHeight * 0.5f;
        FN::DebugText_Overlay(anchor.x, anchor.y,
                              hasBox,
                              bx0, by0, bx1, by1,
                              false, 0.0f, 0.0f, 0.0f, 0.0f);
        if (Renderer* r = Renderer::GetInstance()) r->BindTexture2D(0);
    }
#endif

#if !defined(__bada__) && !defined(FN_GL_STUB)
    if (m_HasClip) {
        // Stage-2 2D batching: the clipped line draws above are still
        // pending -- flush them while the scissor is active.
        if (Renderer* r = Renderer::GetInstance()) r->Flush2D();
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
    if (m_Dirty) RebuildMeshes();
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
// Triggers a lazy RebuildMeshes() if m_Dirty is set (same pattern as Draw()), then
// re-measures via FitStrings for the tight (baked-bearing/layoutX) advance width --
// matches what BakedStringTTF actually renders (no per-line width cache is kept on a
// built FancyBakedString line -- sizeof(BakedStringBox) is pinned at 200B).
// Used by ShopListItem::DrawFloatingText @0x001b4bc8 to anchor NEW/SELECTED badges --
// a loose (full-advance) width here over-estimates and misplaces those badges.
float BakedStringBox::GetTextWidth() const {
    if (m_Dirty) {
        const_cast<BakedStringBox*>(this)->RebuildMeshes();
    }
    std::vector<WrappedLineInfo> wl;
    FitStrings(m_Font, m_Text, m_FontSize, m_BoxWidth, wl);
    float maxW = 0.0f;
    for (size_t i = 0; i < wl.size(); ++i) {
        if (wl[i].advanceWidth > maxW) maxW = wl[i].advanceWidth;
    }
    return maxW;
}

} // namespace Mortar
