// Analysed: 2026-04-25T22:15
#include "render/Font.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "asset/TextureManager.h"
#include "math/Vec3.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace Mortar {

Font::Font()
    : m_PageCount(0)
    , m_LineHeight(0)
    , m_Base(0)
    , m_ScaleW(256)
    , m_ScaleH(256)
    , m_Scale(1.0f)
{
    memset(m_Glyphs, 0, sizeof(m_Glyphs));
}

Font::~Font() {
    m_PageTextures.clear();
}

// Parse a key=value pair from BMFont .fnt line
static bool ParseInt(const char* line, const char* key, int& out) {
    const char* p = strstr(line, key);
    if (!p) return false;
    p += strlen(key);
    if (*p == '=') p++;
    out = atoi(p);
    return true;
}

static bool ParseString(const char* line, const char* key, char* out, int maxLen) {
    const char* p = strstr(line, key);
    if (!p) return false;
    p += strlen(key);
    if (*p == '=') p++;
    if (*p == '"') {
        p++;
        int i = 0;
        while (*p && *p != '"' && i < maxLen - 1) {
            out[i++] = *p++;
        }
        out[i] = '\0';
        return true;
    }
    // Unquoted value
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < maxLen - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

// Matches Font::Load (0x00199e9c, 270 lines)
SmartPtr<Font> Font::Load(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Font::Load: failed to open '%s'\n", path);
        return SmartPtr<Font>();
    }

    Font* font = new Font();
    char line[512];
    char pagePaths[16][256]; // up to 16 pages
    memset(pagePaths, 0, sizeof(pagePaths));

    // Extract base directory from path
    std::string basePath;
    std::string pathStr(path);
    size_t lastSlash = pathStr.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        basePath = pathStr.substr(0, lastSlash + 1);
    }

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "common ", 7) == 0) {
            ParseInt(line, "lineHeight", font->m_LineHeight);
            ParseInt(line, "base", font->m_Base);
            ParseInt(line, "scaleW", font->m_ScaleW);
            ParseInt(line, "scaleH", font->m_ScaleH);
            ParseInt(line, "pages", font->m_PageCount);
        }
        else if (strncmp(line, "page ", 5) == 0) {
            int id = 0;
            ParseInt(line, "id", id);
            if (id >= 0 && id < 16) {
                ParseString(line, "file", pagePaths[id], 256);
            }
        }
        else if (strncmp(line, "char ", 5) == 0) {
            FontGlyph g;
            memset(&g, 0, sizeof(g));
            ParseInt(line, "id", g.id);
            ParseInt(line, "x", g.x);
            ParseInt(line, "y", g.y);
            ParseInt(line, "width", g.width);
            ParseInt(line, "height", g.height);
            ParseInt(line, "xoffset", g.xoffset);
            ParseInt(line, "yoffset", g.yoffset);
            ParseInt(line, "xadvance", g.xadvance);
            ParseInt(line, "page", g.page);
            if (g.id >= 0 && g.id < 256) {
                font->m_Glyphs[g.id] = g;
            }
        }
    }
    fclose(f);

    // Load page textures.
    // The .fnt files reference the original BMFont .tga atlas (e.g.
    // "font_fruit_ninja_0.tga"), but the Bada distribution shipped them
    // pre-converted as .tex at the data root (Data/font_fruit_ninja_0.tex)
    // -- NOT alongside the .fnt in fonts/ and NOT in textures/. Swap the
    // extension and try the data root first; fall back to the basePath
    // (alongside the .fnt) so other distributions still work.
    font->m_PageTextures.resize(font->m_PageCount);
    for (int i = 0; i < font->m_PageCount && i < 16; i++) {
        if (!pagePaths[i][0]) continue;
        std::string pageName(pagePaths[i]);
        size_t dot = pageName.find_last_of('.');
        if (dot != std::string::npos) {
            pageName = pageName.substr(0, dot) + ".tex";
        }
        const char* dataDir = TextureManager::GetDataDir();
        std::string rootPath = std::string(dataDir ? dataDir : ".") + "/" + pageName;
        font->m_PageTextures[i] = TextureManager::GetInstance().Load(rootPath.c_str());
        if (!font->m_PageTextures[i].IsValid()) {
            std::string sidePath = basePath + pageName;
            font->m_PageTextures[i] = TextureManager::GetInstance().Load(sidePath.c_str());
        }
    }

    return SmartPtr<Font>(font);
}

// MeasureWidth returns the text width in lineHeight-normalized units
// (i.e. xadvance / m_LineHeight per glyph). This matches the vertex
// coordinate space used by DrawString below. Callers that want a world-unit
// width must multiply by scale themselves.
float Font::MeasureWidth(float /*scale*/, const char* text) const {
    float width = 0;
    float maxWidth = 0;
    const float invLH = (m_LineHeight > 0) ? (1.0f / (float)m_LineHeight) : 1.0f;
    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            if (width > maxWidth) maxWidth = width;
            width = 0;
            continue;
        }
        // Skip color tags [FFFFFF] and [/]
        if (*p == '[') {
            if (*(p + 1) == '/') { p += 2; continue; }
            const char* end = strchr(p + 1, ']');
            if (end && end - p <= 7) { p = end; continue; }
        }
        uint8_t ch = (uint8_t)*p;
        if (ch < 256) {
            width += (float)m_Glyphs[ch].xadvance * invLH;
        }
    }
    if (width > maxWidth) maxWidth = width;
    return maxWidth;
}

// Matches Font_DrawString (0x00198e44).
//
// Coordinate space contract
// --------------------------
// - `pos` is the world-space anchor, passed unchanged to MatrixStack::Translate.
// - Glyph vertex positions (cursorX/Y, hw/hh) are in NORMALIZED atlas-pixel
//   units: each component = atlas_pixels / m_ScaleW (or m_ScaleH). The
//   MatrixStack::Scale(scale, scale, 1) step converts them to world units.
// - Alignment offsets (CENTER/RIGHT shift, MIDDLE/BOTTOM shift) are also in
//   normalized units so they live in the same space as the cursor.
// - `scale` is the em size in world units, applied directly as the MatrixStack
//   scale factor. There is NO division by m_LineHeight (binary confirmed).
//
// Render pipeline (matches binary steps 1-8)
// -------------------------------------------
//  1. MatrixStack::Push  -- save current world matrix
//  2. MatrixStack::Scale(scale, scale, 1.0)  -- em-size scale
//  3. MatrixStack::Translate(pos)  -- world anchor
//  4. Build per-page vertex arrays (glyph quads in normalized units)
//  5. Per page: Texture::Set + Renderer::DrawTriStrip(verts, count)
//     (DrawTriStrip calls GetMVP() which picks up the modified world stack)
//  6. MatrixStack::Pop  -- restore
//
// NOTE: No direct GL state calls are made here. The blend/depth state is
// whatever the surrounding HUD pipeline has established. This matches the
// binary, which makes zero raw GL calls inside Font_DrawString.
void Font::DrawString(float scale, float maxWidth, float z,
                      const char* text, const Vec3& pos,
                      const Colour& colour, int alignment) {
    if (!text || !*text) return;

    // Vertex metrics are stored in lineHeight-normalized space:
    //   stored_metric = atlas_pixels / lineHeight
    // MatrixStack::Scale(scale, scale, 1) then multiplies by `scale` (em size in
    // world units), giving:
    //   world_size = (atlas_px / lineHeight) * scale
    // Worked example: 16-px glyph, scale=25, lineHeight=28 -> 14.3 world units.
    //
    // UVs use scaleW/scaleH (atlas-pixel coords / atlas dimensions).
    // Earlier port versions divided metrics by scaleW/scaleH too (9x too small) —
    // that's why text rendered as thin horizontal slivers.
    const float invW  = (m_ScaleW      > 0) ? (1.0f / (float)m_ScaleW)      : 1.0f;
    const float invH  = (m_ScaleH      > 0) ? (1.0f / (float)m_ScaleH)      : 1.0f;
    const float invLH = (m_LineHeight  > 0) ? (1.0f / (float)m_LineHeight)  : 1.0f;
    // In lineHeight-normalized vertex space, one line = 1.0 unit exactly.
    const float normLineH = 1.0f;

    // --- Horizontal alignment: cursor starts shifted left by text width ---
    // For wrapped text we need to re-measure each rendered line and apply
    // the alignment offset PER LINE -- otherwise wrapped lines (which can
    // be much shorter than the full text) all share the offset that was
    // computed for the whole text and end up visually misaligned (e.g.,
    // a centered first line with shorter wrapped lines that drift right).
    //
    // Pre-pass: walk the text using the same word-wrap rule as the render
    // loop and record the starting offset in normalized units for each
    // line break. We index by `p - text` so the render loop can look up
    // the line's startX as soon as it begins emitting glyphs from that
    // pointer.
    auto measureLine = [&](const char* lineBegin, const char* lineEnd) -> float {
        float w = 0.0f;
        for (const char* q = lineBegin; q < lineEnd; q++) {
            uint8_t ch = (uint8_t)*q;
            // Skip color tags exactly like the main render loop.
            if (*q == '[') {
                if (*(q + 1) == '/' && *(q + 2) == ']') {
                    q += 2; continue;
                }
                const char* end = (q + 1 < lineEnd) ? (const char*)memchr(q + 1, ']', lineEnd - q - 1) : nullptr;
                if (end && end - q == 7) { q = end; continue; }
            }
            if (ch < 256) w += (float)m_Glyphs[ch].xadvance * invLH;
        }
        return w;
    };

    auto computeStartX = [&](float lineWidth) -> float {
        if (alignment & FONT_ALIGN_CENTER) return -lineWidth * 0.5f;
        if (alignment & FONT_ALIGN_RIGHT)  return -lineWidth;
        return 0.0f;
    };

    // Initial line starts at p=text. If wrap forces a break we'll update
    // these on the fly when the render loop hits the wrap point.
    float startX = 0.0f;
    {
        const char* lineEnd = text;
        if (maxWidth > 0) {
            float normMax = maxWidth / scale;
            float runX = 0.0f;
            for (const char* p = text; *p; p++) {
                if (*p == '\n') { lineEnd = p; break; }
                if (*p == ' ') {
                    float wordW = 0;
                    for (const char* wp = p + 1; *wp && *wp != ' ' && *wp != '\n'; wp++) {
                        uint8_t wch = (uint8_t)*wp;
                        if (wch < 256) wordW += (float)m_Glyphs[wch].xadvance * invLH;
                    }
                    if (runX + wordW > normMax) { lineEnd = p; break; }
                }
                uint8_t ch = (uint8_t)*p;
                if (ch < 256) runX += (float)m_Glyphs[ch].xadvance * invLH;
            }
            if (*lineEnd == '\0') lineEnd = text + strlen(text);
            startX = computeStartX(measureLine(text, lineEnd));
        } else {
            startX = computeStartX(MeasureWidth(scale, text));
        }
    }

    // --- Vertical alignment (flags & 0xC): shift startY in normalized units ---
    // Binary at 0x00199920-0x00199964 issues TranslateLocal with a POSITIVE
    // Y offset (factor 0.5 when flags & 0x4 set, else 1.0). Earlier port
    // negated the sign, putting glyphs `scale` world-units below the binary
    // -- which manifested as the shop title rendering ~20 px above its
    // intended position (titleScale=20 in HD mode, 25 in SD).
    // See docs/engine/font.md "Font_DrawString Implementation".
    float startY = 0.0f;
    if (alignment & 0xC) {
        if (alignment & 0x4) {
            startY += normLineH * 0.5f;
        } else {
            startY += normLineH;
        }
    }

    // Batch vertices per page
    std::vector<std::vector<QUADCUSTOMVERTEX>> pageVerts(m_PageCount);

    float cursorX = startX;
    float cursorY = startY;
    uint32_t currentColour = colour.PlatformColour();

    // Helper: re-compute startX for a fresh line beginning at `p` so
    // CENTER / RIGHT alignment apply per-line.
    auto recomputeStartXFor = [&](const char* p) -> float {
        const char* lineEnd = p;
        if (maxWidth > 0) {
            float normMax = maxWidth / scale;
            float runX = 0.0f;
            for (const char* q = p; *q; q++) {
                if (*q == '\n') { lineEnd = q; break; }
                if (*q == ' ') {
                    float wordW = 0;
                    for (const char* wp = q + 1; *wp && *wp != ' ' && *wp != '\n'; wp++) {
                        uint8_t wch = (uint8_t)*wp;
                        if (wch < 256) wordW += (float)m_Glyphs[wch].xadvance * invLH;
                    }
                    if (runX + wordW > normMax) { lineEnd = q; break; }
                }
                uint8_t ch = (uint8_t)*q;
                if (ch < 256) runX += (float)m_Glyphs[ch].xadvance * invLH;
            }
            if (*lineEnd == '\0') lineEnd = p + strlen(p);
            return computeStartX(measureLine(p, lineEnd));
        }
        return computeStartX(MeasureWidth(scale, p));
    };

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            startX = recomputeStartXFor(p + 1);
            cursorX = startX;
            cursorY -= normLineH;
            continue;
        }

        // Color tag support: [FFFFFF]text[/]
        if (*p == '[') {
            if (*(p + 1) == '/' && *(p + 2) == ']') {
                currentColour = colour.PlatformColour();
                p += 2;
                continue;
            }
            const char* end = strchr(p + 1, ']');
            if (end && end - p == 7) {
                char hex[7];
                memcpy(hex, p + 1, 6);
                hex[6] = '\0';
                unsigned int rgb = (unsigned int)strtoul(hex, nullptr, 16);
                uint8_t cr = (rgb >> 16) & 0xFF;
                uint8_t cg = (rgb >> 8) & 0xFF;
                uint8_t cb = rgb & 0xFF;
                Colour tagCol(cr, cg, cb, colour.a);
                currentColour = tagCol.PlatformColour();
                p = end;
                continue;
            }
        }

        // Word wrap: maxWidth is in world units; divide by scale to get
        // normalized threshold so we can compare against cursor (normalized).
        if (maxWidth > 0 && *p == ' ') {
            float normMax = maxWidth / scale;
            float wordW = 0;
            for (const char* wp = p + 1; *wp && *wp != ' ' && *wp != '\n'; wp++) {
                uint8_t wch = (uint8_t)*wp;
                if (wch < 256) wordW += (float)m_Glyphs[wch].xadvance * invLH;
            }
            if (cursorX - startX + wordW > normMax) {
                startX = recomputeStartXFor(p + 1);  // skip the space, start next word
                cursorX = startX;
                cursorY -= normLineH;
                continue;
            }
        }

        uint8_t ch = (uint8_t)*p;
        if (ch >= 256) continue;
        const FontGlyph& g = m_Glyphs[ch];
        if (g.width == 0 && g.height == 0) {
            cursorX += (float)g.xadvance * invLH;
            continue;
        }

        if (g.page < 0 || g.page >= m_PageCount) {
            cursorX += (float)g.xadvance * invLH;
            continue;
        }

        // Build centered quad in lineHeight-normalized space.
        // Binary stores all metric values (width, height, xoffset, yoffset,
        // xadvance) as atlas_pixels / lineHeight. After MatrixStack::Scale
        // (scale, scale, 1) world_size = (atlas_px / lineHeight) * scale,
        // giving glyphs at em pixel sizes.
        //
        // UVs use scaleW / scaleH (atlas-pixel coords / atlas dimensions).
        // z = DAT_00199a94 = 0.0f for all vertices (binary constant).
        const float cx = cursorX + ((float)g.xoffset + (float)g.width  * 0.5f) * invLH;
        const float cy = cursorY - ((float)g.yoffset + (float)g.height * 0.5f) * invLH;
        const float hw = (float)g.width  * 0.5f * invLH;
        const float hh = (float)g.height * 0.5f * invLH;

        const float u0 = (float)g.x * invW;
        const float v0 = (float)g.y * invH;
        const float u1 = (float)(g.x + g.width)  * invW;
        const float v1 = (float)(g.y + g.height) * invH;

        // 6 vertices (2 triangles, GL_TRIANGLES) — z = 0.0f (binary DAT_00199a94)
        QUADCUSTOMVERTEX v[6];
        // Triangle 1: TL, TR, BL
        v[0] = { cx - hw, cy - hh, 0.0f, 0,0,1, currentColour, u0, v0 };
        v[1] = { cx + hw, cy - hh, 0.0f, 0,0,1, currentColour, u1, v0 };
        v[2] = { cx - hw, cy + hh, 0.0f, 0,0,1, currentColour, u0, v1 };
        // Triangle 2: TR, BR, BL
        v[3] = { cx + hw, cy - hh, 0.0f, 0,0,1, currentColour, u1, v0 };
        v[4] = { cx + hw, cy + hh, 0.0f, 0,0,1, currentColour, u1, v1 };
        v[5] = { cx - hw, cy + hh, 0.0f, 0,0,1, currentColour, u0, v1 };

        for (int vi = 0; vi < 6; vi++) {
            pageVerts[g.page].push_back(v[vi]);
        }

        cursorX += (float)g.xadvance * invLH;
    }

    // Per-glyph DrawQuad pipeline. The batched DrawTriList path didn't
    // render correctly (cause not yet root-caused), so we issue one
    // DrawQuad per glyph using the proven HUD quad pipeline. Slow at high
    // glyph counts but correct.
    //
    // For each glyph in pageVerts, vertices are stored in lineHeight-
    // normalized space with layout [TL, TR, BL, TR, BR, BL] per glyph.
    // We extract TL/BR to derive the world-space scale + translate that
    // SetupQuadMatrix-style code expects, then DrawQuad with the glyph's
    // UV bounds.
    // Font draw is always 2D; disable depth test so glyphs don't z-fight
    // neighbouring HUD quads at the same z. We don't restore depth at the
    // end -- callers that need depth back on (e.g. 3D scene) must
    // glEnable(GL_DEPTH_TEST) themselves. Previously this function ended
    // with an unconditional glEnable, which broke 2D HUD chains that
    // drew further quads at z=0 after a font draw (they got rejected
    // against the depth-buffered earlier quad).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    Renderer* renderer = Renderer::GetInstance();
    if (renderer) {
        MatrixStack& worldStack = MatrixManager::GetInstance().GetWorldStack();
        for (int pg = 0; pg < m_PageCount; pg++) {
            if (pg >= (int)m_PageTextures.size() || !m_PageTextures[pg].IsValid()) continue;
            Texture* tex = m_PageTextures[pg].Get();
            tex->Set();
            const int N = (int)pageVerts[pg].size();
            for (int gi = 0; gi + 5 < N; gi += 6) {
                QUADCUSTOMVERTEX& vTL = pageVerts[pg][gi];
                QUADCUSTOMVERTEX& vBR = pageVerts[pg][gi + 4];
                float wx = (vTL.x + vBR.x) * 0.5f * scale + pos.x;
                float wy = (vTL.y + vBR.y) * 0.5f * scale + pos.y;
                float ww = (vBR.x - vTL.x) * scale;
                float wh = (vBR.y - vTL.y) * scale;
                if (ww < 0) ww = -ww;
                if (wh < 0) wh = -wh;

                worldStack.Reset();
                Matrix44 mat = Matrix44::MakeScale(ww, wh, 1.0f);
                mat.GlobalTranslate44(Vec3(wx, wy, pos.z));
                worldStack.SetCurrentMatrix(mat);
                MatrixManager::GetInstance().UploadModelViewOnly();
                renderer->DrawQuad(colour, vTL.u, vTL.v, vBR.u, vBR.v);
            }
            tex->UnSet();
        }
    }

    (void)z; (void)maxWidth;
}

} // namespace Mortar
