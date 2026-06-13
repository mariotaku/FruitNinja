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

namespace Mortar {

BakedStringBox::BakedStringBox(FontCacheObjectTTF* font,
                               float fontSize,
                               float width,
                               float height,
                               int align,
                               int wrapMode,
                               float lineSpacing,
                               int param8)
    : m_Font(font)
    , m_FontSize(fontSize)
    , m_BoxWidth(width)
    , m_BoxHeight(height)
    , m_Align(align)
    , m_WrapMode(wrapMode)
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
    , m_GradMode(0)
    , m_GradFlag(false)
    , m_Dirty(true)
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
    m_Pos = pos;
    if (flag) {
        m_Dirty = true;
    }
}

void BakedStringBox::FitIntoVerticalBounds() {
    if (!m_Font) return;
    // Shrink fontSize in 1-pixel steps, floor 6.0px, until total wrapped
    // height fits within m_BoxHeight. Rebuilds layout at each candidate size.
    while (m_FontSize > 6.0f) {
        Layout();
        if (TotalHeight() <= m_BoxHeight) {
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

// Helper: measure pixel advance of a word (ASCII chars, length len) at ps.
// Returns advance in raw pixels (= world units in the 1:1 ortho space).
static float MeasureWord(FontCacheObjectTTF* font, const char* ptr, int len, int ps) {
    float adv = 0.0f;
    for (int c = 0; c < len; c++) {
        uint32_t cp = (uint32_t)(unsigned char)ptr[c];
        const GlyphAtlasEntry* g = font->GetGlyph(cp, ps);
        if (g) adv += (float)g->advanceX;
    }
    return adv;
}

static float SpaceAdvance(FontCacheObjectTTF* font, int ps) {
    const GlyphAtlasEntry* sp = font->GetGlyph((uint32_t)' ', ps);
    return sp ? (float)sp->advanceX : 0.0f;
}

// Greedy word-wrap layout at m_FontSize pixels into m_Lines.
// Glyph coords are raw FreeType pixel values (bearingX/Y, width, height, advanceX).
// These are emitted unchanged as vertex coordinates. The ortho projection is 1:1
// pixel = world unit, so do NOT divide by fontSize or any invPS factor — doing so
// would produce sub-pixel quads that render as invisible slivers.
void BakedStringBox::Layout() {
    m_Lines.clear();
    m_Dirty = false;

    if (!m_Font || m_Text[0] == '\0') {
        return;
    }

    const int ps = (int)(m_FontSize + 0.5f);
    if (ps < 1) {
        return;
    }

    const float wrapLimit  = m_BoxWidth;
    const uint32_t packed  = m_Colour.PlatformColour();

    // Pre-render every codepoint in the string so atlas UVs are populated.
    {
        const char* p = m_Text;
        while (*p) {
            m_Font->GetGlyph((uint32_t)(unsigned char)*p, ps);
            p++;
        }
    }
    FontInterface* atlas = m_Font->GetAtlas();
    if (atlas) atlas->BuildPendingTextures();

    // Tokenise into words (split on ASCII space).
    struct WordToken {
        const char* start;
        int         len;
        float       advance; // in raw pixels (= world units)
    };
    std::vector<WordToken> words;
    {
        const char* p = m_Text;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p) break;
            const char* ws = p;
            while (*p && *p != ' ') p++;
            WordToken tok;
            tok.start   = ws;
            tok.len     = (int)(p - ws);
            tok.advance = MeasureWord(m_Font, ws, tok.len, ps);
            words.push_back(tok);
        }
    }
    if (words.empty()) return;

    const float spAdv = SpaceAdvance(m_Font, ps);

    // Greedy line-fill loop.
    size_t wi = 0;
    while (wi < words.size()) {
        size_t lineStart = wi;

        // Measure how many words fit.
        float lineWidth = 0.0f;
        size_t lineEnd  = lineStart;
        while (lineEnd < words.size()) {
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

        // Horizontal alignment offset.
        float lineOffsetX = 0.0f;
        {
            const int horizAlign = m_Align & 0x3;
            if (horizAlign == 1) {
                // centre within box
                lineOffsetX = (wrapLimit - lineWidth) * 0.5f;
            } else if (horizAlign == 2) {
                lineOffsetX = wrapLimit - lineWidth;
            }
        }

        BakedStringBoxLine line;
        float curX = lineOffsetX;
        float lineH = 0.0f;

        for (size_t wj = lineStart; wj < lineEnd; wj++) {
            if (wj > lineStart) curX += spAdv;
            const char* wp = words[wj].start;
            for (int c = 0; c < words[wj].len; c++) {
                uint32_t cp = (uint32_t)(unsigned char)wp[c];
                const GlyphAtlasEntry* g = m_Font->GetGlyph(cp, ps);
                if (!g) continue;

                if (g->width > 0 && g->height > 0) {
                    // Glyph quad in raw pixel units (= world units in 1:1 ortho).
                    float x0 = curX + (float)g->bearingX;
                    float y1 = (float)g->bearingY;           // top (above baseline)
                    float x1 = x0 + (float)g->width;
                    float y0 = y1 - (float)g->height;        // bottom

                    // 6-vert tri-strip quad: BL, TL, BR, TR, TR(degen), TR(degen)
                    QUADCUSTOMVERTEX v[6];
                    v[0] = { x0, y0, 0.f, 0,0,1, packed, g->u0, g->v1 };
                    v[1] = { x0, y1, 0.f, 0,0,1, packed, g->u0, g->v0 };
                    v[2] = { x1, y0, 0.f, 0,0,1, packed, g->u1, g->v1 };
                    v[3] = { x1, y1, 0.f, 0,0,1, packed, g->u1, g->v0 };
                    v[4] = v[3];
                    v[5] = v[3];
                    for (int k = 0; k < 6; k++) line.verts.push_back(v[k]);

                    float gh = (float)g->bearingY;
                    if (gh > lineH) lineH = gh;
                }

                curX += (float)g->advanceX;
            }
        }

        // Line height: use FreeType ascender in pixels, fallback to max bearing.
        {
            int asc    = m_Font->GetAscender(ps);
            float ascF = (float)asc;
            line.height = (ascF > lineH) ? ascF : lineH;
        }
        line.width = lineWidth;
        m_Lines.push_back(line);
        wi = lineEnd;
    }
}

// SetGradient  binary @ 0x0024566c
void BakedStringBox::SetGradient(Colour top, Colour bottom, bool perGlyph) {
    if (m_GradTop.r != top.r || m_GradTop.g != top.g || m_GradTop.b != top.b || m_GradTop.a != top.a ||
        m_GradBottom.r != bottom.r || m_GradBottom.g != bottom.g || m_GradBottom.b != bottom.b || m_GradBottom.a != bottom.a ||
        m_GradMode != 2 || m_GradFlag != false) {
        m_GradMode = 2;
        m_GradTop = top;
        m_GradBottom = bottom;
        m_GradFlag = false;
        if (!perGlyph) {
            m_Dirty = true;
        }
    }
}

// SetShadow  binary @ 0x002462c0
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

void BakedStringBox::Draw(float rotationDegrees, Vec2 scale, int center) {
    if (!m_Font) return;
    if (m_Dirty) Layout();
    if (m_Lines.empty()) return;

    FontInterface* atlas = m_Font->GetAtlas();
    if (!atlas) return;
    atlas->BuildPendingTextures();

    const float totalH = TotalHeight();

    // Build rotation coefficients.
    const float theta = rotationDegrees * (3.14159265f / 180.0f);
    const float sinT  = sinf(theta);
    const float cosT  = cosf(theta);

    // World-space anchor. If center==1, shift up by half total height so the
    // block is centred vertically on m_Pos.
    Vec3 anchor = m_Pos;
    if (center) {
        anchor.y += totalH * 0.5f;
    }

    // Use identity world matrix so vertex transforms are in world space.
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    world.Push();
    world.m_Current.Identity();
    world.m_Version++;
    MatrixManager::GetInstance().UploadModelViewOnly();

    Renderer* renderer = Renderer::GetInstance();

    // Render lines top-to-bottom. First line baseline at y=0 (anchor.y after
    // centering offset). Each subsequent line shifts down by (line.height +
    // m_LineSpacing), i.e. lineY grows.
    float lineY = 0.0f;

    for (size_t li = 0; li < m_Lines.size(); li++) {
        const BakedStringBoxLine& line = m_Lines[li];
        if (line.verts.empty()) {
            lineY += line.height + m_LineSpacing;
            continue;
        }

        // Local y offset for this line's baseline.
        const float localBaseY = -lineY;
        const int nVerts = (int)line.verts.size();

        // Copy + transform vertices into world space.
        std::vector<QUADCUSTOMVERTEX> wv(line.verts);
        for (int i = 0; i < nVerts; i++) {
            float lx = wv[i].x * scale.x;
            float ly = (wv[i].y + localBaseY) * scale.y;
            // Rotate then translate to anchor.
            wv[i].x = cosT * lx - sinT * ly + anchor.x;
            wv[i].y = sinT * lx + cosT * ly + anchor.y;
        }

        // Wire inter-glyph connectors: last slot of each 6-vert glyph becomes
        // a duplicate of the first vert of the next glyph to keep the
        // tri-strip degenerate across the glyph boundary.
        for (int gi = 1; gi * 6 < nVerts; gi++) {
            wv[gi * 6 - 1] = wv[gi * 6];
        }

        glBindTexture(GL_TEXTURE_2D, atlas->GetTextureID());
        glEnable(GL_TEXTURE_2D);
        if (renderer) {
            renderer->DrawTriStrip(&wv[0], nVerts);
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        lineY += line.height + m_LineSpacing;
    }

    world.Pop();
}

} // namespace Mortar
