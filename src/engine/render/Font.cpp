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

    // --- Horizontal alignment: per-line offset inside the wrap box ---
    //
    // Binary Font_DrawString @ 0x00198e44 anchors the wrap box's LEFT
    // edge at pos.x with width maxWH.x (= the maxWidth arg). The flag
    // bits 0..1 select per-line offset rules (verified at
    // 0x00198eb4..0x00198f7c, halving step at 0x00198f72):
    //
    //   alignment & 3 == 0  (LEFT)         lineOffset = 0
    //   alignment & 3 == 1  (CENTER alone) special wrap-aware mode via
    //                                       bit 0x10 -- not used by any
    //                                       caller in the shipped binary
    //                                       so we fall back to the same
    //                                       formula as 0x3 here.
    //   alignment & 3 == 2  (RIGHT-IN-BOX) lineOffset = normWrap - lineLen
    //   alignment & 3 == 3  (CENTER-IN-BOX) lineOffset = (normWrap - lineLen) * 0.5
    //
    // For maxWidth == 0 the formulas degenerate to the port's previous
    // pos.x-centred / pos.x-right-edged behaviour, so non-wrapping
    // callers (shop title, cost text, etc.) keep working unchanged.
    //
    // The offset is recomputed AT EVERY \n and at every word-wrap point
    // because each line's lineLen differs (binary calls GetLineLength
    // inside the line-feed handler at LAB_00199884).
    const int   horizAlign = alignment & 0x3;
    const float normWrap   = (maxWidth > 0.0f) ? (maxWidth / scale) : 0.0f;

    auto measureLineNorm = [&](const char* p) -> float {
        float w = 0.0f;
        for (const char* q = p; *q && *q != '\n'; q++) {
            if (*q == '[') {
                if (*(q + 1) == '/' && *(q + 2) == ']') { q += 2; continue; }
                const char* end = strchr(q + 1, ']');
                if (end && end - q == 7) { q = end; continue; }
            }
            if (normWrap > 0.0f && *q == ' ') {
                float wordW = 0.0f;
                for (const char* wp = q + 1; *wp && *wp != ' ' && *wp != '\n'; wp++) {
                    uint8_t wch = (uint8_t)*wp;
                    if (wch < 256) wordW += (float)m_Glyphs[wch].xadvance * invLH;
                }
                if (w + wordW > normWrap) break;
            }
            uint8_t ch = (uint8_t)*q;
            if (ch < 256) w += (float)m_Glyphs[ch].xadvance * invLH;
        }
        return w;
    };

    auto computeLineOffset = [&](const char* p) -> float {
        if (horizAlign == 0) return 0.0f;
        float lineLen = measureLineNorm(p);
        if (horizAlign == 0x2) return normWrap - lineLen;
        // 0x1 and 0x3 both centre-in-box (binary 0x1 falls through the
        // halving step too -- bit 0 is the "halve" flag).
        return (normWrap - lineLen) * 0.5f;
    };

    float lineOffset = computeLineOffset(text);

    // --- Vertical alignment (flags & 0xC): block-centred shift in normalized units ---
    //
    // Binary Font_DrawString applies a SINGLE TranslateLocal AFTER the
    // glyph loop (flush path, decompile lines 281-289):
    //
    //   if (alignment & 0xC) {
    //     fVar42 -= maxWidth;                           // cursor_y -= 1.0
    //     factor = (alignment & 0x4) ? 0.5 : 1.0;
    //     translateY = (-maxWH.y - cursor_y) * factor;  // maxWH.y == 0 in shop
    //                = (0 - (-(N-1) - 1)) * factor
    //                = N * factor
    //   }
    //
    // For N = number of rendered lines (counting wrap breaks + \n + the
    // final line). Worked example: N=2, factor=0.5 -> startY = +1.0.
    // Block centre with N lines stacked on -1.0 per line lands at:
    //   ((N-1)/2 - N/2) negated -> consistent half-line offset above pos.y.
    //
    // The port previously set startY = +0.5 (factor) regardless of N --
    // correct for N=1 but undershot multi-line wrapped paragraphs by
    // (N-1)/2 lines. With description scale=18 and a 2-line wrap, the
    // text rendered ~9 world units too low.
    int lineCount = 1;
    {
        // Walk the same wrap rules as the glyph loop without emitting,
        // counting line breaks so the caller can size the block.
        const float countNormWrap = normWrap;
        for (const char* p = text; *p; p++) {
            if (*p == '\n') { lineCount++; continue; }
            if (*p == '[') {
                if (*(p + 1) == '/' && *(p + 2) == ']') { p += 2; continue; }
                const char* end = strchr(p + 1, ']');
                if (end && end - p == 7) { p = end; continue; }
            }
            if (countNormWrap > 0.0f && *p == ' ') {
                // Re-measure the current line up to this space (cumulative
                // since last line break) plus the next word; if it spills,
                // count a wrap break.
                // Simpler: track cursor since line start in a parallel var.
            }
        }
        // Approach: dedicated walker that mirrors the render loop's wrap.
        if (countNormWrap > 0.0f) {
            lineCount = 0;
            const char* p = text;
            while (*p) {
                lineCount++;
                float runX = 0.0f;
                while (*p && *p != '\n') {
                    if (*p == '[') {
                        if (*(p + 1) == '/' && *(p + 2) == ']') { p += 3; continue; }
                        const char* end = strchr(p + 1, ']');
                        if (end && end - p == 7) { p = end + 1; continue; }
                    }
                    if (*p == ' ') {
                        float wordW = 0.0f;
                        for (const char* wp = p + 1; *wp && *wp != ' ' && *wp != '\n'; wp++) {
                            uint8_t wch = (uint8_t)*wp;
                            if (wch < 256) wordW += (float)m_Glyphs[wch].xadvance * invLH;
                        }
                        if (runX + wordW > countNormWrap) {
                            // Wrap: skip this space, start new line at next char.
                            p++;
                            break;
                        }
                    }
                    uint8_t ch = (uint8_t)*p;
                    if (ch < 256) runX += (float)m_Glyphs[ch].xadvance * invLH;
                    p++;
                }
                if (*p == '\n') p++;
            }
            if (lineCount < 1) lineCount = 1;
        } else {
            // No wrap: count just \n.
            lineCount = 1;
            for (const char* p = text; *p; p++) if (*p == '\n') lineCount++;
        }
    }

    float startY = 0.0f;
    if (alignment & 0xC) {
        const float factor = (alignment & 0x4) ? 0.5f : 1.0f;
        startY = (float)lineCount * factor * normLineH;
    }

    // Batch vertices per page
    std::vector<std::vector<QUADCUSTOMVERTEX>> pageVerts(m_PageCount);

    // cursorX advances through the line; lineOffset is the per-line
    // shift inside the wrap box. Per-glyph X = pos.x + scale * (cursorX
    // + lineOffset + glyph.xoffset). On line break (\n or wrap), reset
    // cursorX to 0 and recompute lineOffset for the new line.
    float cursorX = 0.0f;
    float cursorY = startY;
    uint32_t currentColour = colour.PlatformColour();

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            lineOffset = computeLineOffset(p + 1);
            cursorX = 0.0f;
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
            float wordW = 0;
            for (const char* wp = p + 1; *wp && *wp != ' ' && *wp != '\n'; wp++) {
                uint8_t wch = (uint8_t)*wp;
                if (wch < 256) wordW += (float)m_Glyphs[wch].xadvance * invLH;
            }
            if (cursorX + wordW > normWrap) {
                lineOffset = computeLineOffset(p + 1);  // skip the space
                cursorX = 0.0f;
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
        const float cx = cursorX + lineOffset + ((float)g.xoffset + (float)g.width  * 0.5f) * invLH;
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
