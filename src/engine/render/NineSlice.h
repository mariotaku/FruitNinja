#ifndef MORTAR_NINE_SLICE_H
#define MORTAR_NINE_SLICE_H

//
// NineSlice -- Port specific: draw a textured panel as a 9-slice (9-patch) so a
// bordered image (dialog_box.tex etc.) can be scaled to an arbitrary size WITHOUT
// distorting its corners. NO binary counterpart -- the Mortar engine drew dialog
// panels as single fixed-size quads (Mesh::DrawQuadUnCached with full [0,1] UV);
// this is a port-improvement for the resurrected settings UI, which needs an
// arbitrarily-sized panel.
//
// Built from the engine's own primitive: Mesh::DrawQuadUnCached(colour, uMin, uMax,
// vMin, vMax, fx) (@0x00240a70) draws the unit quad [-0.5,0.5] with a UV sub-rect.
// A 9-slice is 9 of those -- 4 fixed corners, 4 one-axis-stretched edges, 1
// two-axis-stretched centre -- each with its own world matrix + UV sub-rect.
// V convention matches Renderer::DrawQuad: quad TOP (high Y) = vMin, so the panel's
// top row samples the texture's top rows.
//

#include "math/Colour.h"

namespace Mortar {

class Texture;

class NineSlice {
public:
    // Draw a 9-slice panel centred at (centerX, centerY) in HUD/world space, with
    // outer size destW x destH. The corners are drawn at destBorder world units;
    // srcBorderPx is the matching corner inset in TEXTURE pixels (drives the UV
    // splits, using the texture's own dimensions). Edges stretch on one axis, the
    // centre on both. The caller need NOT Set() the texture -- Draw binds+unbinds
    // it. Degenerate cells (border >= half the panel) are clamped/skipped.
    static void Draw(Texture* tex, float centerX, float centerY,
                     float destW, float destH,
                     float srcBorderPx, float destBorder, Colour colour);
};

} // namespace Mortar

#endif // MORTAR_NINE_SLICE_H
