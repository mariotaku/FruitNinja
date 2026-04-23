#include "render/BakedString.h"
#include "render/MatrixManager.h"

namespace Mortar {

BakedString::BakedString()
    : m_Width(0)
    , m_Height(0)
{
}

BakedString::~BakedString() {
    Clear();
}

void BakedString::Bake(Font* font, float scale, float maxWidth, float z,
                       const char* text, const Vec3& pos,
                       const Colour& colour, int alignment) {
    Clear();
    if (!font || !text || !*text) return;

    // Measure dimensions
    m_Width = font->MeasureWidth(scale, text);
    m_Height = font->GetLineHeight(scale);

    // Use Font's page textures
    m_Pages.resize(font->m_PageCount);
    for (int i = 0; i < font->m_PageCount; i++) {
        if (i < (int)font->m_PageTextures.size()) {
            m_Pages[i].texture = font->m_PageTextures[i];
        }
    }

    // TODO: Bake vertices by capturing Font::DrawString output
    // For now, BakedString::Draw falls back to Font::DrawString
    // A full implementation would intercept the glyph quad generation
    (void)maxWidth; (void)z; (void)pos; (void)colour; (void)alignment;
}

// Matches BakedString::Draw (0x0019738c)
void BakedString::Draw() {
    for (size_t pg = 0; pg < m_Pages.size(); pg++) {
        PageData& page = m_Pages[pg];
        if (page.vertices.empty()) continue;

        if (page.texture.IsValid()) {
            page.texture->Set();
        }

        MatrixManager& mm = MatrixManager::GetInstance();
        (void)mm; // MVP is already set by caller

        int stride = sizeof(QUADCUSTOMVERTEX);
        QUADCUSTOMVERTEX* verts = page.vertices.data();
        int vertCount = (int)page.vertices.size();

        glEnable(GL_TEXTURE_2D);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLfloat)GL_MODULATE);
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

        if (page.texture.IsValid()) {
            page.texture->UnSet();
        }
    }
}

void BakedString::Clear() {
    m_Pages.clear();
    m_Width = 0;
    m_Height = 0;
}

} // namespace Mortar
