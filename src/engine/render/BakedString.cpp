#include "render/BakedString.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/Font.h"
#include "render/Utf8StringIterator.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include "asset/Mesh.h"
#include "asset/Texture.h"
#include "math/Colour.h"
#include "math/Vec3.h"
#include "math/MathUtil.h"
#include <new>

namespace Mortar {

// Binary @ 0x0019789c (default ctor — zero-init)
BakedString::BakedString()
    : m_unknown0(0)
    , m_pPageTextures(0)
    , m_PageCount(0)
    , m_pPageVertices(0)
    , m_pPageQuadCounts(0)
    , m_Width(0.0f)
    , m_Height(0.0f)
{
}

// Binary @ 0x0019789c — explicit-construct entry; same body as Bake().
BakedString::BakedString(Font* font, Utf8StringIterator iter, const Colour& colour)
    : m_unknown0(0)
    , m_pPageTextures(0)
    , m_PageCount(0)
    , m_pPageVertices(0)
    , m_pPageQuadCounts(0)
    , m_Width(0.0f)
    , m_Height(0.0f)
{
    Bake(font, iter, colour);
}

// Binary @ 0x00197564
BakedString::~BakedString() {
    Clear();
}

// Binary @ 0x0019789c — build the per-page glyph-quad vertex cache.
//
// Heap layout produced (matches binary):
//   m_pPageTextures[-2] = 4          (allocation tag; binary writes literal 4)
//   m_pPageTextures[-1] = pageCount  (used by the dtor's reverse SmartPtr walk)
//   m_pPageTextures[0..n-1] = SmartPtr<Texture> page-texture ref per used page
//   m_pPageVertices[i]    = QUADCUSTOMVERTEX[6 * glyphsOnPage_i]
//   m_pPageQuadCounts[i]  = 6 * glyphsOnPage_i
//
// Two passes over the string: pass 1 counts glyphs per font page and assigns a
// compact output-buffer index per used page; pass 2 fills the vertex buffers.
void BakedString::Bake(Font* font, Utf8StringIterator iter, const Colour& colour) {
    Clear();
    if (!font) {
        return;
    }

    const int fontPages = font->m_PageCount;

    // Scratch arrays, one slot per font page.
    //   pageBufIdx[p] = compact output index for font page p, or -1 if unused.
    //   pageGlyphs[p] = glyph count landing on font page p.
    int* pageBufIdx = new int[fontPages];
    int* pageGlyphs = new int[fontPages];
    for (int p = 0; p < fontPages; ++p) {
        pageBufIdx[p] = -1;
        pageGlyphs[p] = 0;
    }

    // ---- Pass 1: count glyphs per page, assign compact buffer indices ----
    // Binary takes the iterator by value and walks it; it Reset()s before pass 2.
    // Port mirrors this: the by-value `iter` is walked in pass 1, Reset() rewinds
    // it to the string start for pass 2.
    m_PageCount = 0;
    while (iter.m_CurrentCodepoint != 0) {
        Font::CharTemplate* ct = font->GetCharTemplate(iter.m_CurrentCodepoint);
        if (ct != 0) {
            int page = (int)ct->page;
            if (pageBufIdx[page] == -1) {
                pageBufIdx[page] = (int)m_PageCount;
                m_PageCount++;
            }
            pageGlyphs[page]++;
        }
        iter++;
    }

    const int usedPages = (int)m_PageCount;

    // ---- Allocate the page-texture block: [tag, count, SmartPtr x N] ----
    // Binary allocates (count + 2) * 4 bytes via operator new[] and stores
    // &block[2] in m_pPageTextures; block[0]=4 (tag), block[1]=count.
    {
        uint32_t* block = (uint32_t*)operator new[]((usedPages + 2) * sizeof(uint32_t));
        block[0] = 4;
        block[1] = (uint32_t)usedPages;
        m_pPageTextures = (SmartPtr<Texture>*)(block + 2);
        for (int i = 0; i < usedPages; ++i) {
            new (&m_pPageTextures[i]) SmartPtr<Texture>();
        }
    }

    // ---- Allocate per-page count + vertex-pointer arrays ----
    m_pPageQuadCounts = new uint32_t[usedPages];
    m_pPageVertices   = new QUADCUSTOMVERTEX*[usedPages];

    for (int p = 0; p < fontPages; ++p) {
        const int bi = pageBufIdx[p];
        if (bi != -1) {
            // Copy the font page texture into the SmartPtr slot.
            Font::Page* fpage = font->GetPage(p);
            if (fpage) {
                m_pPageTextures[bi] = fpage->texture;
            }
            m_pPageQuadCounts[bi] = 0;
            m_pPageVertices[bi] = new QUADCUSTOMVERTEX[pageGlyphs[p] * 6];
        }
    }

    // ---- Pass 2: fill vertex buffers ----
    m_Width  = 0.0f;
    m_Height = 0.0f;

    const uint32_t packedColour = colour.PlatformColour();
    const float lineHeight = font->m_LineHeight;
    const float scaleW = (float)font->m_ScaleW;
    const float scaleH = (float)font->m_ScaleH;

    float cursorX = 0.0f;

    // Binary calls Utf8StringIterator::Reset between the count pass and the fill
    // pass (it re-walks the same by-value iterator twice from the string start).
    iter.Reset();
    {
        while (iter.m_CurrentCodepoint != 0) {
            Font::CharTemplate* ct = font->GetCharTemplate(iter.m_CurrentCodepoint);
            if (ct != 0) {
                const int bi = pageBufIdx[(int)ct->page];
                QUADCUSTOMVERTEX* buf = m_pPageVertices[bi];
                // m_pPageQuadCounts holds the running per-page vertex count; it is
                // the base index of the next glyph's 6 vertices.
                const int base = (int)m_pPageQuadCounts[bi];

                const float u0 = ct->u0;
                const float v0 = ct->v0;
                const float w  = ct->w;
                const float h  = ct->h;
                const float negYoff = -(ct->yoff);
                const float left  = cursorX + ct->xoff;
                const float rightU  = u0 + (lineHeight / scaleW) * w;
                const float bottomV = v0 + (lineHeight / scaleH) * h;

                // vert[0] (top-left)
                QUADCUSTOMVERTEX* v = &buf[base];
                v[0].x = left;
                v[0].y = negYoff;
                v[0].u = u0;
                v[0].v = v0;

                // Bridge the tristrip: duplicate this glyph's first vertex into the
                // previous glyph's trailing degenerate slot (only when not first).
                if (base != 0) {
                    v[-1].x = v[0].x;
                    v[-1].y = v[0].y;
                }

                // vert[1] (bottom-left)
                v[1].x = left;
                v[1].y = negYoff - h;
                v[1].u = u0;
                v[1].v = bottomV;

                // vert[2] (top-right)
                v[2].x = left + w;
                v[2].y = negYoff;
                v[2].u = rightU;
                v[2].v = v0;

                // vert[3] (bottom-right)
                v[3].x = left + w;
                v[3].y = negYoff - h;
                v[3].u = rightU;
                v[3].v = bottomV;

                // vert[4] = trailing degenerate, dup of bottom-right (x,y only)
                v[4].x = v[3].x;
                v[4].y = v[3].y;

                // Per-vertex common fields: z=0, nx=0, ny=0, nz=1, colour, on all 6.
                for (int k = 0; k < 6; ++k) {
                    v[k].z      = 0.0f;
                    v[k].nx     = 0.0f;
                    v[k].ny     = 0.0f;
                    v[k].nz     = 1.0f;
                    v[k].colour = packedColour;
                }

                cursorX += ct->xadv;
                m_pPageQuadCounts[bi] = (uint32_t)(base + 6);

                if (m_Width < cursorX) {
                    m_Width = cursorX;
                }
                const float glyphBottom = ct->yoff + h;
                if (glyphBottom > m_Height) {
                    m_Height = glyphBottom;
                }
            }
            iter++;
        }
    }

    delete[] pageGlyphs;
    delete[] pageBufIdx;
}

// Binary @ 0x0019738c
void BakedString::Draw(float scale, float rotZ, uint32_t align, const Vec3& pos) {
    MatrixManager& mm = MatrixManager::GetInstance();
    MatrixStack& world = mm.GetWorldStack();

    // 1. Push current world matrix.
    world.Push();

    // 2. Horizontal alignment offset (applied before scale/rot, in local space).
    if ((align & 0x3) == 0x2) {
        // Right-align: shift left by full width.
        world.m_Current.GlobalTranslate44(-m_Width, 0.0f, 0.0f);
        world.m_Version++;
    } else if ((align & 0x3) == 0x3) {
        // Centre-H: shift left by half width.
        world.m_Current.GlobalTranslate44(m_Width * -0.5f, 0.0f, 0.0f);
        world.m_Version++;
    }

    // 3. Vertical alignment offset.
    if ((align & 0xC) == 0x8) {
        // Bottom-align: shift up by full height.
        world.m_Current.GlobalTranslate44(0.0f, m_Height, 0.0f);
        world.m_Version++;
    } else if ((align & 0xC) == 0xC) {
        // Centre-V: shift up by half height.
        world.m_Current.GlobalTranslate44(0.0f, m_Height * 0.5f, 0.0f);
        world.m_Version++;
    }

    // 4. Scale uniformly on X and Y.
    world.m_Current.ApplyScale(scale, scale, 1.0f);
    world.m_Version++;

    // 5. Z rotation (degrees -> 16-bit angle index via *182.0).
    {
        const uint16_t idx = (uint16_t)(int)(rotZ * 182.0f);
        world.m_Current.RotZ44(SinIdx(idx), CosIdx(idx));
        world.m_Version++;
    }

    // 6. Translate to world anchor.
    world.m_Current.GlobalTranslate44(pos);
    world.m_Version++;

    // 7. Upload modelview (binary calls _UploadCurrentMatrices(true)).
    mm.UploadModelViewOnly();

    // 8. Per page: bind texture, draw cached tristrip, unbind.
    for (uint32_t i = 0; i < m_PageCount; ++i) {
        Texture* tex = m_pPageTextures[i].Get();
        if (tex) {
            tex->Set();
            TexEnvModulate();  // Set owns tex-env (binary model); port Set() doesn't set it.
        }
        Mesh::DrawTriStrip(m_pPageVertices[i], (long)m_pPageQuadCounts[i], false, 0);
        if (tex) {
            tex->UnSet();
        }
    }

    // 9. Pop world matrix back.
    world.Pop();
}

void BakedString::Clear() {
    // Binary @ 0x00197564 — dtor frees the page-texture block as (block-2) after
    // destroying the N embedded SmartPtrs in reverse (count read from block[-1]).
    if (m_pPageTextures) {
        uint32_t* block = ((uint32_t*)m_pPageTextures) - 2;
        const uint32_t count = block[1];
        for (uint32_t i = count; i != 0; --i) {
            m_pPageTextures[i - 1].~SmartPtr<Texture>();
        }
        operator delete[]((void*)block);
        m_pPageTextures = 0;
    }

    if (m_pPageVertices) {
        for (uint32_t i = 0; i < m_PageCount; ++i) {
            if (m_pPageVertices[i]) {
                delete[] m_pPageVertices[i];
            }
        }
        delete[] m_pPageVertices;
        m_pPageVertices = 0;
    }
    if (m_pPageQuadCounts) {
        delete[] m_pPageQuadCounts;
        m_pPageQuadCounts = 0;
    }

    m_PageCount = 0;
    m_Width = 0.0f;
    m_Height = 0.0f;
}

// Binary @ 0x001971c8 — single `bx lr`; a genuine no-op in the shipped Bada build.
void BakedString::AddDropShadow() {}

// TODO: 0x0019762c -- LayoutToCircle: reposition baked glyph quads onto a circular
//   arc of the given radius. BLOCKED on the matrix-vector transform subsystem:
//   the binary builds a per-glyph translate+RotZ matrix and runs each of the 6
//   glyph vertices through _Matrix44<float>::MultVec44 (0x000fc57c) using a Vec4
//   transform helper (MakeVec3_Engine / _Vector3 ctor @ 0x0019760c) plus a base
//   matrix copied from a DAT global. Neither MultVec44 nor the Vec4 transform
//   helper is ported yet; port those before implementing this faithfully.
void BakedString::LayoutToCircle(float) {}

} // namespace Mortar
