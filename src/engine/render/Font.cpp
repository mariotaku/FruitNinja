#include "render/Font.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "asset/TextureManager.h"
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
    // — NOT alongside the .fnt in fonts/ and NOT in textures/. Swap the
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

float Font::MeasureWidth(float scale, const char* text) const {
    float width = 0;
    float maxWidth = 0;
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
            width += m_Glyphs[ch].xadvance * scale * m_Scale;
        }
    }
    if (width > maxWidth) maxWidth = width;
    return maxWidth;
}

// Matches Font::DrawString (0x00198e44, 13 params simplified)
void Font::DrawString(float scale, float maxWidth, float z,
                      const char* text, const Vec3& pos,
                      const Colour& colour, int alignment) {
    if (!text || !*text) return;

    float finalScale = scale * m_Scale;
    float invW = 1.0f / (float)m_ScaleW;
    float invH = 1.0f / (float)m_ScaleH;

    // Calculate starting position based on alignment
    float startX = pos.x;
    float startY = pos.y;

    if (alignment & FONT_ALIGN_CENTER) {
        startX -= MeasureWidth(scale, text) * 0.5f;
    } else if (alignment & FONT_ALIGN_RIGHT) {
        startX -= MeasureWidth(scale, text);
    }

    // Vertical alignment: glyphs are positioned with cursorY as the top of
    // each line (yoffset is subtracted later). FONT_ALIGN_MIDDLE shifts Y up
    // by half-line; FONT_ALIGN_BOTTOM shifts up by full line. Without these,
    // text drawn at a row's basePos sits below the row visually.
    const float lineH = (float)m_LineHeight * finalScale;
    if (alignment & FONT_ALIGN_MIDDLE) {
        startY += lineH * 0.5f;
    }
    if (alignment & FONT_ALIGN_BOTTOM) {
        startY += lineH;
    }

    // Batch vertices per page
    std::vector<std::vector<QUADCUSTOMVERTEX>> pageVerts(m_PageCount);

    float cursorX = startX;
    float cursorY = startY;
    uint32_t currentColour = colour.PlatformColour();

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            cursorX = startX;
            cursorY -= m_LineHeight * finalScale;
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
                // Parse 6-char hex color
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

        // Word wrap
        if (maxWidth > 0 && *p == ' ') {
            // Measure next word
            float wordW = 0;
            for (const char* wp = p + 1; *wp && *wp != ' ' && *wp != '\n'; wp++) {
                uint8_t wch = (uint8_t)*wp;
                if (wch < 256) wordW += m_Glyphs[wch].xadvance * finalScale;
            }
            if (cursorX - startX + wordW > maxWidth) {
                cursorX = startX;
                cursorY -= m_LineHeight * finalScale;
                continue;
            }
        }

        uint8_t ch = (uint8_t)*p;
        if (ch >= 256) continue;
        const FontGlyph& g = m_Glyphs[ch];
        if (g.width == 0 && g.height == 0) {
            cursorX += g.xadvance * finalScale;
            continue;
        }

        if (g.page < 0 || g.page >= m_PageCount) {
            cursorX += g.xadvance * finalScale;
            continue;
        }

        // Build quad for this glyph
        float gx = cursorX + g.xoffset * finalScale;
        float gy = cursorY - g.yoffset * finalScale;
        float gw = g.width * finalScale;
        float gh = g.height * finalScale;

        float u0 = g.x * invW;
        float v0 = g.y * invH;
        float u1 = (g.x + g.width) * invW;
        float v1 = (g.y + g.height) * invH;

        // 6 vertices (2 triangles) for this glyph
        QUADCUSTOMVERTEX v[6];

        // Triangle 1: top-left, top-right, bottom-left
        v[0] = { gx,      gy,      z, 0,0,1, currentColour, u0, v0 };
        v[1] = { gx + gw, gy,      z, 0,0,1, currentColour, u1, v0 };
        v[2] = { gx,      gy - gh, z, 0,0,1, currentColour, u0, v1 };

        // Triangle 2: top-right, bottom-right, bottom-left
        v[3] = { gx + gw, gy,      z, 0,0,1, currentColour, u1, v0 };
        v[4] = { gx + gw, gy - gh, z, 0,0,1, currentColour, u1, v1 };
        v[5] = { gx,      gy - gh, z, 0,0,1, currentColour, u0, v1 };

        for (int vi = 0; vi < 6; vi++) {
            pageVerts[g.page].push_back(v[vi]);
        }

        cursorX += g.xadvance * finalScale;
    }

    // Flush per-page batches
    // Font rendering builds vertex arrays that the game-level Renderer
    // will draw via DrawTriList. Store them for the caller to flush.
    // For direct rendering, game code should call Renderer::DrawTriList
    // after Font::DrawString with each page's texture bound.
    for (int pg = 0; pg < m_PageCount; pg++) {
        if (pageVerts[pg].empty()) continue;
        if (pg < (int)m_PageTextures.size() && m_PageTextures[pg].IsValid()) {
            m_PageTextures[pg]->Set();
        }

        // Fixed-function glyph draw: per-vertex RGBA colour modulated
        // with the page texture. Matches DrawTriList wiring.
        MatrixManager& mm = MatrixManager::GetInstance();
        Matrix44 mvp = mm.GetMVP();
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(mvp.ptr());
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_TEXTURE_2D);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLfloat)GL_MODULATE);
        glDisable(GL_LIGHTING);
        glColor4ub(255, 255, 255, 255);

        // Glyph atlases are alpha-keyed; without GL_BLEND the entire quad
        // renders as the atlas's RGB and the glyph mask is ignored. The
        // HUD pipeline doesn't enable blend by default, so do it here.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Text sits in the same Z plane as HUD elements; depth-test against
        // anything already there would reject equal-depth fragments under
        // GL_LESS. Disable depth test for the glyph batch so text always
        // overlays. (HUD::Draw already has depth-write off.)
        glDisable(GL_DEPTH_TEST);

        int stride = sizeof(QUADCUSTOMVERTEX);
        QUADCUSTOMVERTEX* verts = pageVerts[pg].data();
        int vertCount = (int)pageVerts[pg].size();

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, stride, &verts->x);
        glClientActiveTexture(GL_TEXTURE0);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, stride, &verts->u);
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, &verts->colour);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDrawArrays(GL_TRIANGLES, 0, vertCount);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);

        if (pg < (int)m_PageTextures.size() && m_PageTextures[pg].IsValid()) {
            m_PageTextures[pg]->UnSet();
        }

        // Restore depth test for downstream HUD draws that expect it on.
        glEnable(GL_DEPTH_TEST);
    }
}

} // namespace Mortar
