//
// NineSlice -- Port specific 9-slice panel draw (see header). No binary counterpart.
//

#include "NineSlice.h"
#include "asset/Mesh.h"
#include "asset/Texture.h"
#include "render/MatrixManager.h"
#include "math/Matrix44.h"
#include "math/Vec3.h"

namespace Mortar {

void NineSlice::Draw(Texture* tex, float centerX, float centerY,
                     float destW, float destH,
                     float srcBorderPx, float destBorder, Colour colour) {
    if (!tex) return;
    float texW = (float)tex->GetWidth();
    float texH = (float)tex->GetHeight();
    if (texW <= 0.0f || texH <= 0.0f || destW <= 0.0f || destH <= 0.0f) return;

    // Clamp the dest border per axis so the stretched middle never inverts.
    float dbx = destBorder;
    float dby = destBorder;
    if (dbx * 2.0f > destW) dbx = destW * 0.5f;
    if (dby * 2.0f > destH) dby = destH * 0.5f;

    // UV border fractions from the texture's own pixel dimensions.
    float fu = srcBorderPx / texW;
    float fv = srcBorderPx / texH;
    if (fu > 0.5f) fu = 0.5f;
    if (fv > 0.5f) fv = 0.5f;

    // Column geometry (x, +X right): widths + centres.
    float leftW = dbx, rightW = dbx, midW = destW - 2.0f * dbx;
    float leftCx  = centerX - destW * 0.5f + dbx * 0.5f;
    float midCx   = centerX;
    float rightCx = centerX + destW * 0.5f - dbx * 0.5f;

    // Row geometry (y, +Y up): the TOP row is high-Y and samples the texture top
    // (Renderer::DrawQuad maps quad top -> vMin).
    float topH = dby, botH = dby, midH = destH - 2.0f * dby;
    float topCy = centerY + destH * 0.5f - dby * 0.5f;
    float midCy = centerY;
    float botCy = centerY - destH * 0.5f + dby * 0.5f;

    // UV splits: u left/centre/right, v top/centre/bottom.
    float u0 = 0.0f, u1 = fu,        u2 = 1.0f - fu, u3 = 1.0f;
    float v0 = 0.0f, v1 = fv,        v2 = 1.0f - fv, v3 = 1.0f;

    struct Cell { float x, y, w, h, uMin, uMax, vMin, vMax; };
    Cell cells[9] = {
        // top row (v0..v1)
        { leftCx,  topCy, leftW,  topH, u0, u1, v0, v1 },
        { midCx,   topCy, midW,   topH, u1, u2, v0, v1 },
        { rightCx, topCy, rightW, topH, u2, u3, v0, v1 },
        // middle row (v1..v2)
        { leftCx,  midCy, leftW,  midH, u0, u1, v1, v2 },
        { midCx,   midCy, midW,   midH, u1, u2, v1, v2 },
        { rightCx, midCy, rightW, midH, u2, u3, v1, v2 },
        // bottom row (v2..v3)
        { leftCx,  botCy, leftW,  botH, u0, u1, v2, v3 },
        { midCx,   botCy, midW,   botH, u1, u2, v2, v3 },
        { rightCx, botCy, rightW, botH, u2, u3, v2, v3 }
    };

    MatrixManager& mm = MatrixManager::GetInstance();
    tex->Set();
    for (int i = 0; i < 9; ++i) {
        Cell& c = cells[i];
        if (c.w <= 0.0f || c.h <= 0.0f) continue;   // skip a collapsed edge/centre
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(c.w, c.h, 1.0f);
        mat.GlobalTranslate44(Vec3(c.x, c.y, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(colour, c.uMin, c.uMax, c.vMin, c.vMax, NULL);
    }
    tex->UnSet();
}

} // namespace Mortar
