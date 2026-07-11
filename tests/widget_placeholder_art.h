// widget_placeholder_art.h -- shared in-memory placeholder textures for the
// dead-code settings/dropdown widget render + interactive tests.
//
// The faithful widget .tex art (checked/unchecked switch, slider track/thumb,
// dialog bar, expand/scroll arrows) is NOT shipped in v1.6.1 (the widgets are
// dead code), so the tests inject PROCEDURALLY-DRAWN substitute textures via
// each widget's SetTexturesForTest hook. These helpers build them.
//
// Extracted verbatim from test_settings_widgets_render.cpp /
// test_dropdown_render.cpp so all three tests (those two plus
// test_settings_interactive.cpp) share one definition. Output is byte-identical
// to the originals -- only the location moved.
//
// Test-only; no binary counterpart. Host-only TU (GL + Mortar::Texture). Each
// test exe includes this header in exactly one TU, so the inline functions
// never collide. Cross-build (asm-verify) never sees tests.

#ifndef FN_TEST_WIDGET_PLACEHOLDER_ART_H
#define FN_TEST_WIDGET_PLACEHOLDER_ART_H

#include "render/gl_funcs.h"
#include "asset/Texture.h"
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

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glBindTexture(GL_TEXTURE_2D, 0);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = (a != 255);
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

// Antialiased coverage from a signed distance: 1 fully inside, 0 fully
// outside, linear ramp across a ~1.5px band at the edge.
inline float Coverage(float d)
{
    if (d <= -0.75f) return 1.0f;
    if (d >=  0.75f) return 0.0f;
    return 0.5f - (d / 1.5f);
}

// ---------------------------------------------------------------------------
// Build a procedural TOGGLE-SWITCH texture: a rounded "pill" track (alpha=0
// outside the pill so the shape reads as a switch against the background, not a
// filled rectangle) with a filled circular knob offset toward one end.
//   on=false (OFF/unchecked): grey pill, light knob centered in the LEFT third.
//   on=true  (ON/checked):    green pill, light knob centered in the RIGHT third.
//
// Deliberately fills the FULL w x h texture (no hit-rect inset): CheckBox's
// clickable bound is a binary-hardcoded hit-rect, pos.x +/-36 / pos.y +/-28.5
// (72x57), independent of this placeholder's drawn size (CheckBox::Draw scales
// a 128x64 quad; Update tests +/-36/+/-28.5). That means this placeholder's
// outer margin is honestly visible-but-unclickable, matching how an oversized
// switch graphic would actually behave against the real hit-rect -- an inset
// pill would misrepresent the true quad size. A real/proper checked.tex should
// author its switch graphic WITHIN that hit-rect so visible == clickable;
// this placeholder does not attempt that.
// ---------------------------------------------------------------------------
inline Mortar::SmartPtr<Mortar::Texture> MakeSwitchTex(bool on, int w, int h)
{
    const float margin = 4.0f;
    const float cx     = (float)w * 0.5f;
    const float cy     = (float)h * 0.5f;
    const float halfW  = (float)w * 0.5f - margin;
    const float halfH  = (float)h * 0.5f - margin;
    const float knobR  = halfH - 3.0f;
    const float thirdW = (float)w / 3.0f;
    const float knobCx = on ? ((float)w - thirdW * 0.5f) : (thirdW * 0.5f);

    const uint8_t trackR = on ?  60 : 120;
    const uint8_t trackG = on ? 190 : 120;
    const uint8_t trackB = on ?  60 : 120;

    std::vector<uint8_t> px((size_t)w * (size_t)h * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float fx = (float)x + 0.5f;
            float fy = (float)y + 0.5f;

            float trackCov = Coverage(StadiumSDF(fx, fy, cx, cy, halfW, halfH));
            if (trackCov <= 0.0f) continue;

            float knobCov = Coverage(CircleSDF(fx, fy, knobCx, cy, knobR));

            uint8_t* out = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
            if (knobCov > 0.0f) {
                // Light knob, blended over the track colour at its own edge.
                out[0] = (uint8_t)(230.0f * knobCov + (float)trackR * (1.0f - knobCov));
                out[1] = (uint8_t)(230.0f * knobCov + (float)trackG * (1.0f - knobCov));
                out[2] = (uint8_t)(235.0f * knobCov + (float)trackB * (1.0f - knobCov));
            } else {
                out[0] = trackR;
                out[1] = trackG;
                out[2] = trackB;
            }
            out[3] = (uint8_t)(255.0f * trackCov);
        }
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glBindTexture(GL_TEXTURE_2D, 0);

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

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glBindTexture(GL_TEXTURE_2D, 0);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = true;
    return Mortar::SmartPtr<Mortar::Texture>(t);
}

// ---------------------------------------------------------------------------
// Procedural up-arrow (opaque triangle pointing up, transparent elsewhere). The
// bottom scroller arrow reuses this via the binary's U+V-flipped DrawQuadUnCached.
// ---------------------------------------------------------------------------
inline Mortar::SmartPtr<Mortar::Texture> MakeArrowTex(
    uint8_t r, uint8_t g, uint8_t b, int w, int h)
{
    std::vector<uint8_t> px((size_t)w * (size_t)h * 4, 0);
    for (int y = 0; y < h; ++y) {
        // Row 0 = top (narrow tip); increasing y widens the triangle.
        float t = (float)y / (float)(h - 1);
        int half = (int)(t * (float)w * 0.5f);
        int cx = w / 2;
        for (int x = cx - half; x <= cx + half; ++x) {
            if (x < 0 || x >= w) continue;
            uint8_t* out = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
            out[0] = r; out[1] = g; out[2] = b; out[3] = 255;
        }
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glBindTexture(GL_TEXTURE_2D, 0);

    Mortar::Bada::Texture2D_Bada* t = new Mortar::Bada::Texture2D_Bada();
    t->m_TexId = id;
    t->SetDimensions(w, h);
    t->m_HasAlpha = true;
    return Mortar::SmartPtr<Mortar::Texture>(t);
}

} // namespace fn_widget_art

#endif // FN_TEST_WIDGET_PLACEHOLDER_ART_H
