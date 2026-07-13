// WidgetPlaceholderArt.h -- shared in-memory procedurally-drawn textures for
// the dead-code settings/dropdown widgets (CheckBox / SliderControl / ComboBox /
// ListBox / VerticalScroller).
//
// The real widget art (checked/unchecked checkbox, slider track/thumb, combo/
// dialog bar, expand/scroll arrows) IS shipped -- generated from
// assets/ui-widgets/*.svg at build time by fn_asset_staging
// (tools/assets/svg-to-webp.mjs) -- so SettingsScreen and the widget render
// tests load those directly via LoadLocalisedTexture, no fallback. This header
// now only supplies the handful of elements with NO real .tex counterpart
// (SettingsScreen's list-row tint canvas, modal backdrop dim) plus
// test_settings_interactive.cpp's dev-harness textures.
//
// Host-only TU (GL + Mortar::Texture); header-only inline functions, safe to
// include from multiple TUs.

#ifndef FN_HUD_WIDGET_PLACEHOLDER_ART_H
#define FN_HUD_WIDGET_PLACEHOLDER_ART_H

#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "util/SmartPtr.h"
#include <vector>
#include <cstdint>
#include <cmath>

namespace fn_widget_art {

// ---------------------------------------------------------------------------
// Solid-colour GL texture wrapped in a Texture2D_Bada. Gives the widget a valid
// texId + apparent dimensions (SliderControl/VerticalScroller read the dims to
// size their track/thumb/arrow quads).
// ---------------------------------------------------------------------------
inline Mortar::SmartPtr<Mortar::Texture> MakeSolidTex(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a, int w, int h)
{
    std::vector<uint8_t> px((size_t)w * (size_t)h * 4);
    for (int i = 0; i < w * h; ++i) {
        px[i * 4 + 0] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = a;
    }

    uint32_t id = Mortar::TextureManager::CreateTextureFromRGBA(&px[0], w, h, /*linearFilter=*/true);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = (a != 255);
    return Mortar::SmartPtr<Mortar::Texture>(t);
}

// ---------------------------------------------------------------------------
// White vertical alpha-ramp texture: RGB fixed at (255,255,255), alpha ramps
// linearly from `aTop` (row 0, texture top) to `aBottom` (last row). Modulated
// by a Colour tint at draw time (glColor / vertex-colour path), the tint's own
// RGB shows through true (unlike a texture with the tint baked into its RGB,
// which MODULATE-multiplies and darkens/shifts the result) while the alpha
// ramp still fades the tint in/out -- used for edge-fade bands that must match
// an arbitrary UI colour rather than a fixed baked-in gradient asset.
// ---------------------------------------------------------------------------
inline Mortar::SmartPtr<Mortar::Texture> MakeVerticalAlphaRampTex(
    uint8_t aTop, uint8_t aBottom, int w, int h)
{
    std::vector<uint8_t> px((size_t)w * (size_t)h * 4);
    for (int y = 0; y < h; ++y) {
        float t = (h > 1) ? (float)y / (float)(h - 1) : 0.0f;
        uint8_t a = (uint8_t)((float)aTop + ((float)aBottom - (float)aTop) * t + 0.5f);
        for (int x = 0; x < w; ++x) {
            uint8_t* out = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
            out[0] = 255; out[1] = 255; out[2] = 255; out[3] = a;
        }
    }

    uint32_t id = Mortar::TextureManager::CreateTextureFromRGBA(&px[0], w, h, /*linearFilter=*/true);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = true;
    return Mortar::SmartPtr<Mortar::Texture>(t);
}

// ---------------------------------------------------------------------------
// Procedural shape helpers (test-only synthetic bitmaps; no external deps).
//
// Signed distance to a "stadium" -- an axis-aligned rounded rect whose corner
// radius equals its half-height, i.e. a pill/track shape. Negative = inside.
// ---------------------------------------------------------------------------
inline float StadiumSDF(float px, float py, float cx, float cy,
                        float halfW, float halfH)
{
    float dx = std::fabs(px - cx);
    float dy = std::fabs(py - cy);
    float qx = dx - (halfW - halfH);
    if (qx < 0.0f) qx = 0.0f;
    return std::sqrt(qx * qx + dy * dy) - halfH;
}

// Signed distance to a circle. Negative = inside.
inline float CircleSDF(float px, float py, float cx, float cy, float radius)
{
    float dx = px - cx;
    float dy = py - cy;
    return std::sqrt(dx * dx + dy * dy) - radius;
}

// Signed distance to an axis-aligned rounded rectangle (square-ish box with
// corner radius `radius`). Negative = inside.
inline float RoundedRectSDF(float px, float py, float cx, float cy,
                            float halfW, float halfH, float radius)
{
    float dx = std::fabs(px - cx) - (halfW - radius);
    float dy = std::fabs(py - cy) - (halfH - radius);
    float qx = dx > 0.0f ? dx : 0.0f;
    float qy = dy > 0.0f ? dy : 0.0f;
    float outside = std::sqrt(qx * qx + qy * qy);
    float inside = (dx > dy ? dx : dy);
    if (inside > 0.0f) inside = 0.0f;
    return outside + inside - radius;
}

// Signed distance to a thick line segment (a..b, half-width `halfW`). Negative
// = inside the stroke. Used to rasterize the checkmark tick.
inline float SegmentSDF(float px, float py, float ax, float ay,
                        float bx, float by, float halfW)
{
    float abx = bx - ax, aby = by - ay;
    float apx = px - ax, apy = py - ay;
    float abLenSq = abx * abx + aby * aby;
    float t = abLenSq > 0.0f ? (apx * abx + apy * aby) / abLenSq : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float cx = ax + abx * t, cy = ay + aby * t;
    float dx = px - cx, dy = py - cy;
    return std::sqrt(dx * dx + dy * dy) - halfW;
}

// Antialiased coverage from a signed distance: 1 fully inside, 0 fully
// outside, linear ramp across a ~1.5px band at the edge.
inline float Coverage(float d)
{
    if (d <= -0.75f) return 1.0f;
    if (d >=  0.75f) return 0.0f;
    return 0.5f - (d / 1.5f);
}

// ---------------------------------------------------------------------------
// Build a procedural CHECKBOX texture: a rounded-square box (alpha=0 outside
// the box so the shape reads as a discrete checkbox against the background,
// not a filled rectangle), optionally filled + marked with a checkmark tick.
//   checked=false (unchecked): empty box -- grey outline, transparent interior.
//   checked=true  (checked):   filled green box + white checkmark tick.
//
// CheckBox's clickable bound is a binary-hardcoded hit-rect, pos.x +/-36 /
// pos.y +/-28.5 (72x57) -- see CheckBox::Update / CheckBox.cpp lines 106-109 --
// independent of this placeholder's drawn size (CheckBox::Draw scales a
// 128x64 quad regardless). So the box is drawn well WITHIN that hit-rect and
// the whole visible shape sits inside the clickable area. A real/proper
// checked.tex should do the same; this placeholder mirrors that constraint.
//
// `halfBox` is the box half-extent in TEXTURE pixels (mapped 1:1 to on-screen
// px via the 128x64 draw quad -> actually the 128x64 quad scales the whole
// texture to 128x64 on screen, so the drawn box size == 2*halfBox * (128/w).
// For the canonical w=128,h=64 texture, on-screen box size == 2*halfBox px).
// Callers pass a balanced value (e.g. 22 -> 44px box) so the checkbox reads at
// the same visual weight as the slider track / combo bar. It MUST stay inside
// the +/-36 x / +/-28.5 y hit-rect (halfBox <= 28).
// ---------------------------------------------------------------------------
inline Mortar::SmartPtr<Mortar::Texture> MakeCheckboxTex(bool checked, int w, int h,
                                                         float halfBox = 27.5f)
{
    const float cx      = (float)w * 0.5f;
    const float cy      = (float)h * 0.5f;
    const float corner  = 6.0f;           // rounded-square corner radius
    const float strokeW = 3.0f;           // outline thickness (unchecked)

    const uint8_t fillR = 40,  fillG = 170, fillB = 70;   // checked: green fill
    const uint8_t lineR = 180, lineG = 180, lineB = 180;  // unchecked: mid-grey outline
    const uint8_t tickR = 255, tickG = 255, tickB = 255;  // checked: white tick

    // Tick geometry: two strokes forming a checkmark, in texture pixels.
    const float tickHalfW = 3.0f;
    const float p0x = cx - halfBox * 0.55f, p0y = cy + halfBox * 0.05f;
    const float p1x = cx - halfBox * 0.12f, p1y = cy + halfBox * 0.45f;
    const float p2x = cx + halfBox * 0.60f, p2y = cy - halfBox * 0.50f;

    std::vector<uint8_t> px((size_t)w * (size_t)h * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float fx = (float)x + 0.5f;
            float fy = (float)y + 0.5f;

            float boxD = RoundedRectSDF(fx, fy, cx, cy, halfBox, halfBox, corner);

            float cov;
            uint8_t r, g, b;
            if (checked) {
                cov = Coverage(boxD);
                r = fillR; g = fillG; b = fillB;
                if (cov > 0.0f) {
                    float tickCov = Coverage(SegmentSDF(fx, fy, p0x, p0y, p1x, p1y, tickHalfW));
                    float tickCov2 = Coverage(SegmentSDF(fx, fy, p1x, p1y, p2x, p2y, tickHalfW));
                    if (tickCov2 > tickCov) tickCov = tickCov2;
                    if (tickCov > 0.0f) {
                        r = (uint8_t)((float)tickR * tickCov + (float)fillR * (1.0f - tickCov));
                        g = (uint8_t)((float)tickG * tickCov + (float)fillG * (1.0f - tickCov));
                        b = (uint8_t)((float)tickB * tickCov + (float)fillB * (1.0f - tickCov));
                    }
                }
            } else {
                // Outline only: coverage of the outer edge minus coverage of the
                // inner edge (box shrunk by strokeW) leaves just the border band.
                float outerCov = Coverage(boxD);
                float innerCov = Coverage(RoundedRectSDF(fx, fy, cx, cy, halfBox - strokeW, halfBox - strokeW, corner - strokeW));
                cov = outerCov - innerCov;
                if (cov < 0.0f) cov = 0.0f;
                r = lineR; g = lineG; b = lineB;
            }

            if (cov <= 0.0f) continue;
            uint8_t* out = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
            out[0] = r;
            out[1] = g;
            out[2] = b;
            out[3] = (uint8_t)(255.0f * cov);
        }
    }

    uint32_t id = Mortar::TextureManager::CreateTextureFromRGBA(&px[0], w, h, /*linearFilter=*/true);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = true;
    return Mortar::SmartPtr<Mortar::Texture>(t);
}

// ---------------------------------------------------------------------------
// Build a procedural filled-circle texture (round knob for the SliderControl
// thumb instead of a flat rectangle). Alpha=0 outside the circle.
// ---------------------------------------------------------------------------
inline Mortar::SmartPtr<Mortar::Texture> MakeCircleTex(
    uint8_t r, uint8_t g, uint8_t b, int w, int h)
{
    const float cx = (float)w * 0.5f;
    const float cy = (float)h * 0.5f;
    const float radius = (cx < cy ? cx : cy) - 1.0f;

    std::vector<uint8_t> px((size_t)w * (size_t)h * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float cov = Coverage(CircleSDF((float)x + 0.5f, (float)y + 0.5f, cx, cy, radius));
            if (cov <= 0.0f) continue;
            uint8_t* out = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
            out[0] = r;
            out[1] = g;
            out[2] = b;
            out[3] = (uint8_t)(255.0f * cov);
        }
    }

    uint32_t id = Mortar::TextureManager::CreateTextureFromRGBA(&px[0], w, h, /*linearFilter=*/true);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = true;
    return Mortar::SmartPtr<Mortar::Texture>(t);
}

// ---------------------------------------------------------------------------
// Procedural triangle arrow (opaque, transparent elsewhere). Points UP by default
// (tip at top): the VerticalScroller's top arrow uses this, and its bottom arrow
// reuses the same texture via the binary's U+V-flipped DrawQuadUnCached.
// Pass pointDown=true for a downward tip -- used by the ComboBox expand arrow,
// the conventional "tap to open a dropdown" indicator. (The real expand_arrow.tex
// is not shipped in v1.6.1, so its true orientation is unknown; down is a
// placeholder convention, not a fidelity claim.)
// ---------------------------------------------------------------------------
inline Mortar::SmartPtr<Mortar::Texture> MakeArrowTex(
    uint8_t r, uint8_t g, uint8_t b, int w, int h, bool pointDown = false)
{
    std::vector<uint8_t> px((size_t)w * (size_t)h * 4, 0);
    for (int y = 0; y < h; ++y) {
        // Up: row 0 = top narrow tip, widening downward. Down: mirror vertically
        // so the tip sits at the bottom.
        float t = pointDown ? (float)(h - 1 - y) / (float)(h - 1)
                            : (float)y / (float)(h - 1);
        int half = (int)(t * (float)w * 0.5f);
        int cx = w / 2;
        for (int x = cx - half; x <= cx + half; ++x) {
            if (x < 0 || x >= w) continue;
            uint8_t* out = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
            out[0] = r; out[1] = g; out[2] = b; out[3] = 255;
        }
    }

    uint32_t id = Mortar::TextureManager::CreateTextureFromRGBA(&px[0], w, h, /*linearFilter=*/true);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = true;
    return Mortar::SmartPtr<Mortar::Texture>(t);
}

} // namespace fn_widget_art

#endif // FN_HUD_WIDGET_PLACEHOLDER_ART_H
