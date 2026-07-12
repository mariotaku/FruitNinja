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
    // outer size destW x destH, and PER-AXIS border thickness -- both in the
    // source texture (srcBorderXPx/srcBorderYPx, in TEXTURE pixels, drives the UV
    // splits using the texture's own dimensions) and in the destination
    // (destBorderX/destBorderY, in world units). The 4 corners are drawn at fixed
    // destBorderX x destBorderY size; left/right edges stretch vertically;
    // top/bottom edges stretch horizontally; the centre stretches both axes. UV
    // always spans the FULL [0,1] texture (no cropping) -- so any art protruding
    // into the corner/edge cells (e.g. decorative joint/lashing overhang) renders
    // whole. The caller need NOT Set() the texture -- Draw binds+unbinds it.
    // Degenerate cells (a border >= half the panel on its axis) are clamped so
    // edges/centre don't invert.
    // flipV (default false): mirror the SAMPLED texture rows top<->bottom
    // while the 9 dest cells stay in their normal screen position -- i.e. the
    // texture is drawn upside-down in place, not the geometry. Lets one
    // asset serve both an upright and an upside-down placement (e.g. a
    // rounded-top fade band reused mirrored as a rounded-bottom band)
    // without a second source SVG, matching the existing point-up/point-down
    // arrow-reuse convention (see hud/WidgetPlaceholderArt.h MakeArrowTex).
    static void Draw(Texture* tex, float centerX, float centerY,
                     float destW, float destH,
                     float srcBorderXPx, float srcBorderYPx,
                     float destBorderX, float destBorderY,
                     Colour colour, bool flipV = false);

    // Draw a 9-slice panel with FIXED (unstretched, aspect-correct) corners and
    // TILED (repeated, 1:1 texel density) edges + centre -- unlike Draw(), which
    // stretches the edges/centre to fill the dest rect. Use when the source art's
    // border texture has a repeating pattern (e.g. a bamboo-joint frame) that
    // would smear if stretched.
    //
    // worldScale = world units per source texel; it sets both the fixed corner
    // size (srcBorderXPx*worldScale x srcBorderYPx*worldScale) and the 1:1 tile
    // pitch for the edges. Border thickness is independent per axis (srcBorderXPx
    // for left/right, srcBorderYPx for top/bottom) -- unlike Draw()'s single
    // srcBorderPx, matching source art with asymmetric frame thickness.
    //
    // Top/bottom edges tile the source's middle-column strip horizontally; left/
    // right edges tile the middle-row strip vertically. The centre tiles a small
    // fixed centerTileWPx x centerTileHPx texel window taken from the texture's
    // own centre (avoids repeating a baked-in centre highlight/gradient). All
    // three tiled regions clip their last row/column to the remaining dest space,
    // scaling the sampled UV extent by the same fraction so the partial tile
    // doesn't overhang. The caller need NOT Set() the texture -- DrawTiled binds
    // and unbinds it. Degenerate (<=0) tiles/cells are skipped.
    static void DrawTiled(Texture* tex, float centerX, float centerY,
                          float destW, float destH,
                          float srcBorderXPx, float srcBorderYPx,
                          float worldScale,
                          float centerTileWPx, float centerTileHPx,
                          Colour colour);
};

} // namespace Mortar

#endif // MORTAR_NINE_SLICE_H
