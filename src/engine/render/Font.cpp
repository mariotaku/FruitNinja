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

// MeasureWidth returns the text width in normalized atlas-pixel units
// (i.e. xadvance / m_ScaleW per glyph). This matches the vertex coordinate
// space used by DrawString below: glyph positions are stored as
// (atlas_pixel / scaleW/H) and the MatrixStack scale brings them to world
// units. Callers that want a world-unit width must multiply by scale themselves.
float Font::MeasureWidth(float /*scale*/, const char* text) const {
    float width = 0;
    float maxWidth = 0;
    const float invW = (m_ScaleW > 0) ? (1.0f / (float)m_ScaleW) : 1.0f;
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
            width += (float)m_Glyphs[ch].xadvance * invW;
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

    const float invW = (m_ScaleW > 0) ? (1.0f / (float)m_ScaleW) : 1.0f;
    const float invH = (m_ScaleH > 0) ? (1.0f / (float)m_ScaleH) : 1.0f;
    // Normalized line height (atlas pixels / scaleH)
    const float normLineH = (float)m_LineHeight * invH;

    // --- Horizontal alignment: cursor starts shifted left by text width ---
    // MeasureWidth now returns normalized units.
    float startX = 0.0f;
    if (alignment & FONT_ALIGN_CENTER) {
        startX -= MeasureWidth(scale, text) * 0.5f;
    } else if (alignment & FONT_ALIGN_RIGHT) {
        startX -= MeasureWidth(scale, text);
    }

    // --- Vertical alignment (flags & 0xC): shift startY in normalized units ---
    // Binary: flags & 0x4 selects 0.5 factor, flags & 0x8 selects 1.0 factor.
    // 0xC fires both checks; the net result is a 0.5 shift (middle-of-line).
    float startY = 0.0f;
    if (alignment & 0xC) {
        if (alignment & 0x4) {
            startY -= normLineH * 0.5f;
        } else {
            startY -= normLineH;
        }
    }

    // Batch vertices per page
    std::vector<std::vector<QUADCUSTOMVERTEX>> pageVerts(m_PageCount);

    float cursorX = startX;
    float cursorY = startY;
    uint32_t currentColour = colour.PlatformColour();

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
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
                if (wch < 256) wordW += (float)m_Glyphs[wch].xadvance * invW;
            }
            if (cursorX - startX + wordW > normMax) {
                cursorX = startX;
                cursorY -= normLineH;
                continue;
            }
        }

        uint8_t ch = (uint8_t)*p;
        if (ch >= 256) continue;
        const FontGlyph& g = m_Glyphs[ch];
        if (g.width == 0 && g.height == 0) {
            cursorX += (float)g.xadvance * invW;
            continue;
        }

        if (g.page < 0 || g.page >= m_PageCount) {
            cursorX += (float)g.xadvance * invW;
            continue;
        }

        // Build centered quad in normalized atlas-pixel units.
        // Binary vertex layout (docs/engine/font.md -- Vertex Geometry section):
        //   vertex[0] = (cx - hw, cy - hh)  top-left
        //   vertex[1] = (cx - hw, cy + hh)  bottom-left
        //   vertex[2] = (cx + hw, cy - hh)  top-right
        //   vertex[3] = (cx + hw, cy + hh)  bottom-right
        // The MatrixStack::Scale(scale,scale,1) applied above turns these
        // normalized units into world-space sizes.
        //
        // z = DAT_00199a94 = 0.0f for all vertices (binary constant).
        const float cx = cursorX + ((float)g.xoffset + (float)g.width  * 0.5f) * invW;
        const float cy = cursorY - ((float)g.yoffset + (float)g.height * 0.5f) * invH;
        const float hw = (float)g.width  * 0.5f * invW;
        const float hh = (float)g.height * 0.5f * invH;

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

        cursorX += (float)g.xadvance * invW;
    }

    // --- MatrixStack push / scale / translate / draw / pop ---
    // Matches binary pipeline steps 1-6 (Font_DrawString @ 0x00198e44):
    //   1. MatrixStack::Push
    //   2. MatrixStack::Scale(Vec3(scale, scale, 1.0))
    //   3. MatrixStack::Translate(pos)   [world anchor, applied after scale]
    //   4-5. per-page: Texture::Set + Mesh::DrawTriStrip
    //   6. MatrixStack::Pop
    // rotZ (param_3) is 0.0 for all current call sites; omitted here.
    MatrixStack& worldStack = MatrixManager::GetInstance().GetWorldStack();
    worldStack.Push();
    worldStack.Scale(Vec3(scale, scale, 1.0f));
    worldStack.Translate(pos);

    Renderer* renderer = Renderer::GetInstance();
    for (int pg = 0; pg < m_PageCount; pg++) {
        if (pageVerts[pg].empty()) continue;
        if (pg < (int)m_PageTextures.size() && m_PageTextures[pg].IsValid()) {
            m_PageTextures[pg]->Set();
        }

        // DrawTriStrip calls MatrixManager::GetMVP() internally, which
        // picks up the world matrix we just configured above.
        renderer->DrawTriStrip(pageVerts[pg].data(), (int)pageVerts[pg].size());

        if (pg < (int)m_PageTextures.size() && m_PageTextures[pg].IsValid()) {
            m_PageTextures[pg]->UnSet();
        }
    }

    worldStack.Pop();

    (void)z; // z is encoded into pos.z via the world translate; per-vertex z = 0.0f
    (void)maxWidth; // handled above in the word-wrap path
}

} // namespace Mortar
