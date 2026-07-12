//
// NineSlice -- Port specific 9-slice panel draw (see header). No binary counterpart.
//

#include "NineSlice.h"
#include "asset/Mesh.h"
#include "asset/Texture.h"
#include "render/MatrixManager.h"
#include "math/Matrix44.h"
#include "math/Vec3.h"
#include <math.h>

namespace Mortar {

void NineSlice::Draw(Texture* tex, float centerX, float centerY,
                     float destW, float destH,
                     float srcBorderXPx, float srcBorderYPx,
                     float destBorderX, float destBorderY,
                     Colour colour, bool flipV) {
    if (!tex) return;
    float texW = (float)tex->GetWidth();
    float texH = (float)tex->GetHeight();
    if (texW <= 0.0f || texH <= 0.0f || destW <= 0.0f || destH <= 0.0f) return;

    // Clamp the dest border per axis so the stretched middle never inverts.
    float dbx = destBorderX;
    float dby = destBorderY;
    if (dbx * 2.0f > destW) dbx = destW * 0.5f;
    if (dby * 2.0f > destH) dby = destH * 0.5f;

    // UV border fractions from the texture's own pixel dimensions (per axis).
    float fu = srcBorderXPx / texW;
    float fv = srcBorderYPx / texH;
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

    // UV splits: u left/centre/right, v top/centre/bottom. Full [0,1] coverage --
    // no cropping -- so corner/edge art (e.g. protruding decor) renders whole.
    float u0 = 0.0f, u1 = fu,        u2 = 1.0f - fu, u3 = 1.0f;
    float v0 = 0.0f, v1 = fv,        v2 = 1.0f - fv, v3 = 1.0f;

    // flipV: swap which V-row each dest row SAMPLES (top dest cell reads the
    // texture's bottom rows and vice versa) -- geometry (topCy/botCy) is
    // unchanged, only the UV assignment mirrors. See header doc.
    float topV0 = flipV ? v2 : v0, topV1 = flipV ? v3 : v1;
    float botV0 = flipV ? v0 : v2, botV1 = flipV ? v1 : v3;

    struct Cell { float x, y, w, h, uMin, uMax, vMin, vMax; };
    Cell cells[9] = {
        // top row (dest top, sampled V per flipV above)
        { leftCx,  topCy, leftW,  topH, u0, u1, topV0, topV1 },
        { midCx,   topCy, midW,   topH, u1, u2, topV0, topV1 },
        { rightCx, topCy, rightW, topH, u2, u3, topV0, topV1 },
        // middle row (v1..v2, unaffected by flipV -- centre strip is symmetric)
        { leftCx,  midCy, leftW,  midH, u0, u1, v1, v2 },
        { midCx,   midCy, midW,   midH, u1, u2, v1, v2 },
        { rightCx, midCy, rightW, midH, u2, u3, v1, v2 },
        // bottom row (dest bottom, sampled V per flipV above)
        { leftCx,  botCy, leftW,  botH, u0, u1, botV0, botV1 },
        { midCx,   botCy, midW,   botH, u1, u2, botV0, botV1 },
        { rightCx, botCy, rightW, botH, u2, u3, botV0, botV1 }
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

namespace {

// One tile along a 1-D tiled span: dest offset/size (from the span's start) and
// the fraction of the full source-UV extent to sample (1.0 for a full tile, <1.0
// for the clipped last tile so it doesn't overhang the span).
struct TileSpan1D {
    float destOffset;
    float destSize;
    float uvFrac;
};

// Fill `out` (capacity outCap) with tiles covering [0, spanLen) at `tileLen`
// world-units per tile. Returns the tile count (<= outCap). Skips a degenerate
// (<=0) trailing tile.
static int TileSpan(float spanLen, float tileLen, TileSpan1D* out, int outCap) {
    if (spanLen <= 0.0f || tileLen <= 0.0f) return 0;
    int count = (int)ceilf(spanLen / tileLen);
    if (count < 1) count = 1;
    if (count > outCap) count = outCap;
    int n = 0;
    for (int i = 0; i < count; ++i) {
        float offset = (float)i * tileLen;
        float rem = spanLen - offset;
        if (rem <= 0.0f) break;
        float size = (rem < tileLen) ? rem : tileLen;
        out[n].destOffset = offset;
        out[n].destSize = size;
        out[n].uvFrac = size / tileLen;
        ++n;
    }
    return n;
}

} // anonymous namespace

void NineSlice::DrawTiled(Texture* tex, float centerX, float centerY,
                          float destW, float destH,
                          float srcBorderXPx, float srcBorderYPx,
                          float worldScale,
                          float centerTileWPx, float centerTileHPx,
                          Colour colour) {
    if (!tex) return;
    float texW = (float)tex->GetWidth();
    float texH = (float)tex->GetHeight();
    if (texW <= 0.0f || texH <= 0.0f || destW <= 0.0f || destH <= 0.0f) return;
    if (worldScale <= 0.0f) return;

    // Fixed, aspect-correct corner size in world units.
    float cornerW = srcBorderXPx * worldScale;
    float cornerH = srcBorderYPx * worldScale;
    if (cornerW * 2.0f > destW) cornerW = destW * 0.5f;
    if (cornerH * 2.0f > destH) cornerH = destH * 0.5f;

    // UV splits (u: left/centre/right; v: top/centre/bottom).
    float ux0 = 0.0f, ux1 = srcBorderXPx / texW, ux2 = 1.0f - srcBorderXPx / texW, ux3 = 1.0f;
    float uy0 = 0.0f, uy1 = srcBorderYPx / texH, uy2 = 1.0f - srcBorderYPx / texH, uy3 = 1.0f;
    if (ux1 > 0.5f) { ux1 = 0.5f; ux2 = 0.5f; }
    if (uy1 > 0.5f) { uy1 = 0.5f; uy2 = 0.5f; }

    // Dest geometry (+X right, +Y up; top row is high-Y, matching Draw()'s convention).
    float leftCx  = centerX - destW * 0.5f + cornerW * 0.5f;
    float rightCx = centerX + destW * 0.5f - cornerW * 0.5f;
    float topCy   = centerY + destH * 0.5f - cornerH * 0.5f;
    float botCy   = centerY - destH * 0.5f + cornerH * 0.5f;
    float midW = destW - 2.0f * cornerW;
    float midH = destH - 2.0f * cornerH;

    MatrixManager& mm = MatrixManager::GetInstance();
    tex->Set();

    // -- 4 fixed corners --
    struct Corner { float x, y, uMin, uMax, vMin, vMax; };
    Corner corners[4] = {
        { leftCx,  topCy, ux0, ux1, uy0, uy1 }, // top-left
        { rightCx, topCy, ux2, ux3, uy0, uy1 }, // top-right
        { leftCx,  botCy, ux0, ux1, uy2, uy3 }, // bottom-left
        { rightCx, botCy, ux2, ux3, uy2, uy3 }, // bottom-right
    };
    for (int i = 0; i < 4; ++i) {
        Corner& c = corners[i];
        if (cornerW <= 0.0f || cornerH <= 0.0f) continue;
        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(cornerW, cornerH, 1.0f);
        mat.GlobalTranslate44(Vec3(c.x, c.y, 0.0f));
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(colour, c.uMin, c.uMax, c.vMin, c.vMax, NULL);
    }

    // -- top/bottom edges: tile the middle-column strip horizontally --
    if (midW > 0.0f && cornerH > 0.0f) {
        float srcStripXPx = texW - 2.0f * srcBorderXPx;
        float tileW = srcStripXPx * worldScale;
        float uvSpanX = ux2 - ux1;
        TileSpan1D spans[256];
        int n = TileSpan(midW, tileW, spans, 256);
        float left = centerX - midW * 0.5f;
        for (int i = 0; i < n; ++i) {
            float w = spans[i].destSize;
            float cx = left + spans[i].destOffset + w * 0.5f;
            float uMax = ux1 + uvSpanX * spans[i].uvFrac;

            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(w, cornerH, 1.0f);
            mat.GlobalTranslate44(Vec3(cx, topCy, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawQuadUnCached(colour, ux1, uMax, uy0, uy1, NULL);

            mm.GetWorldStack().Reset();
            mat = Matrix44::MakeScale(w, cornerH, 1.0f);
            mat.GlobalTranslate44(Vec3(cx, botCy, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawQuadUnCached(colour, ux1, uMax, uy2, uy3, NULL);
        }
    }

    // -- left/right edges: tile the middle-row strip vertically --
    if (midH > 0.0f && cornerW > 0.0f) {
        float srcStripYPx = texH - 2.0f * srcBorderYPx;
        float tileH = srcStripYPx * worldScale;
        float uvSpanY = uy2 - uy1;
        TileSpan1D spans[256];
        int n = TileSpan(midH, tileH, spans, 256);
        float top = centerY + midH * 0.5f;
        for (int i = 0; i < n; ++i) {
            float h = spans[i].destSize;
            float cy = top - spans[i].destOffset - h * 0.5f;
            float vMax = uy1 + uvSpanY * spans[i].uvFrac;

            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(cornerW, h, 1.0f);
            mat.GlobalTranslate44(Vec3(leftCx, cy, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawQuadUnCached(colour, ux0, ux1, uy1, vMax, NULL);

            mm.GetWorldStack().Reset();
            mat = Matrix44::MakeScale(cornerW, h, 1.0f);
            mat.GlobalTranslate44(Vec3(rightCx, cy, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawQuadUnCached(colour, ux2, ux3, uy1, vMax, NULL);
        }
    }

    // -- centre: tile a small fixed texel window from the texture's own centre --
    if (midW > 0.0f && midH > 0.0f && centerTileWPx > 0.0f && centerTileHPx > 0.0f) {
        float cuMin = 0.5f - (centerTileWPx * 0.5f) / texW;
        float cuSpan = centerTileWPx / texW;
        float cvMin = 0.5f - (centerTileHPx * 0.5f) / texH;
        float cvSpan = centerTileHPx / texH;
        float tileW = centerTileWPx * worldScale;
        float tileH = centerTileHPx * worldScale;

        TileSpan1D colsArr[256];
        TileSpan1D rowsArr[256];
        int cols = TileSpan(midW, tileW, colsArr, 256);
        int rows = TileSpan(midH, tileH, rowsArr, 256);

        float left = centerX - midW * 0.5f;
        float top  = centerY + midH * 0.5f;

        for (int row = 0; row < rows; ++row) {
            float h = rowsArr[row].destSize;
            float cy = top - rowsArr[row].destOffset - h * 0.5f;
            float vMax = cvMin + cvSpan * rowsArr[row].uvFrac;

            for (int col = 0; col < cols; ++col) {
                float w = colsArr[col].destSize;
                float cx = left + colsArr[col].destOffset + w * 0.5f;
                float uMax = cuMin + cuSpan * colsArr[col].uvFrac;

                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(w, h, 1.0f);
                mat.GlobalTranslate44(Vec3(cx, cy, 0.0f));
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();
                Mortar::Mesh::DrawQuadUnCached(colour, cuMin, uMax, cvMin, vMax, NULL);
            }
        }
    }

    tex->UnSet();
}

} // namespace Mortar
