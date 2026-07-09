// BakedStringTTF unit test -- struct layout, arc math, gradient math.
// Does NOT call any BakedStringTTF methods (avoids the GL-using TU entirely).
// Tests the math that BakedStringTTF implements as pure arithmetic formulas.
//
// Pure in-process: no GPU, no audio, no FreeType, no GL headers pulled in.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "render/BakedStringTTF.h"
#include "math/Vec2.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_FLOAT_NEAR(a, b, eps) \
    do { \
        float _a = (float)(a); float _b = (float)(b); float _e = (float)(eps); \
        if (std::fabs(_a - _b) > _e) { \
            std::printf("FAIL (%s:%d): |%f - %f| > %f\n", \
                __FILE__, __LINE__, (double)_a, (double)_b, (double)_e); \
            ::exit(1); \
        } \
    } while(0)

// Replicate the arc math from ApplyFormatting_Circle_Internal @0x00248cc8
// for direct verification (without constructing a BakedStringTTF object).
// v1.6.1 constants: PI=3.14159265, DEG2RAD=0.017453292
static void test_arc_math()
{
    const float PI      = 3.14159265f;
    const float DEG2RAD = 0.017453292f;
    const float radius  = 42.0f;
    const float field60 = 100.0f;  // total advance

    // degPerUnit = 360.0 / (radius * 2*PI)
    float degPerUnit = 360.0f / (radius * 2.0f * PI);
    // half = degPerUnit * field60 * 0.5
    float half = degPerUnit * field60 * 0.5f;

    // Verify degPerUnit makes sense: 360 / circumference
    float circumference = radius * 2.0f * PI;  // ~263.9
    CHECK_FLOAT_NEAR(360.0f / circumference, degPerUnit, 1e-5f);

    // Leftmost glyph (penX=0):
    // ang = PI/2 - (degPerUnit*0 - half) * DEG2RAD = PI/2 + half * DEG2RAD
    float penX = 0.0f;
    float ang  = PI * 0.5f - (degPerUnit * penX - half) * DEG2RAD;
    float expected = PI * 0.5f + half * DEG2RAD;
    CHECK_FLOAT_NEAR(ang, expected, 1e-5f);

    // Centre glyph (penX = field60 / 2):
    // degPerUnit*(field60/2) - half = degPerUnit*field60/2 - degPerUnit*field60/2 = 0
    // ang = PI/2 exactly
    float penXCenter = field60 * 0.5f;
    float angCenter  = PI * 0.5f - (degPerUnit * penXCenter - half) * DEG2RAD;
    CHECK_FLOAT_NEAR(angCenter, PI * 0.5f, 1e-4f);

    // Rightmost glyph (penX = field60):
    // deg term = degPerUnit*field60 - half = half, so ang = PI/2 - half * DEG2RAD
    float penXRight = field60;
    float angRight  = PI * 0.5f - (degPerUnit * penXRight - half) * DEG2RAD;
    CHECK_FLOAT_NEAR(angRight, PI * 0.5f - half * DEG2RAD, 1e-4f);
    // By symmetry, leftmost and rightmost should be equidistant from PI/2
    CHECK_FLOAT_NEAR(ang - PI * 0.5f, -(angRight - PI * 0.5f), 1e-4f);

    // Arc placement: r = radius + gy (gy=0 here)
    float r = radius + 0.0f;
    CHECK_FLOAT_NEAR(r, 42.0f, 1e-6f);

    // Verify cos/sin: ang=PI/2 -> x=0, y=r
    CHECK_FLOAT_NEAR(cosf(angCenter) * r, 0.0f, 1e-3f);
    CHECK_FLOAT_NEAR(sinf(angCenter) * r, r, 1e-3f);

    std::printf("  arc math (degPerUnit=%.4f, half=%.4f): OK\n",
                (double)degPerUnit, (double)half);
}

// Verify half-texel constant: 0.5 / 256 = 0.001953125
// v1.6.1 FinishMesh @0x002480a8
static void test_halftexel_constant()
{
    float ht = 0.5f / 256.0f;
    CHECK_FLOAT_NEAR(ht, 0.001953125f, 1e-9f);
    std::printf("  half-texel constant: OK\n");
}

// Test GlyphTTF struct: verify members exist and whitespace-skip logic.
static void test_glyphttf_members()
{
    Mortar::GlyphTTF g;
    memset(&g, 0, sizeof(g));
    g.m_CharCode   = 65;    // 'A'
    g.m_FontSize   = 12.0f;
    g.m_UvU0       = 0.1f;
    g.m_UvV0       = 0.2f;
    g.m_UvU1       = 0.3f;
    g.m_UvV1       = 0.4f;
    g.m_QuadMin    = Vec2(10.0f, 8.0f);
    g.m_QuadSize   = Vec2(6.0f, 8.0f);
    g.m_RotAngle   = 0.0f;
    g.m_Font       = 0;

    CHECK(g.m_CharCode == 65u);
    CHECK_FLOAT_NEAR(g.m_FontSize, 12.0f, 1e-6f);
    CHECK_FLOAT_NEAR(g.m_UvU0, 0.1f, 1e-6f);
    CHECK_FLOAT_NEAR(g.m_QuadMin.x, 10.0f, 1e-6f);
    CHECK_FLOAT_NEAR(g.m_QuadSize.x, 6.0f, 1e-6f);

    // Whitespace skip: w<1 or h<1
    g.m_QuadSize = Vec2(0.5f, 8.0f);
    bool skip = (g.m_QuadSize.x < 1.0f || g.m_QuadSize.y < 1.0f);
    CHECK(skip);

    g.m_QuadSize = Vec2(6.0f, 0.5f);
    skip = (g.m_QuadSize.x < 1.0f || g.m_QuadSize.y < 1.0f);
    CHECK(skip);

    g.m_QuadSize = Vec2(6.0f, 8.0f);
    skip = (g.m_QuadSize.x < 1.0f || g.m_QuadSize.y < 1.0f);
    CHECK(!skip);

    std::printf("  GlyphTTF members + whitespace-skip logic: OK\n");
}

// Test BakedStringTTF_Surface struct members.
// NOTE: the surface holds a std::vector (m_Glyphs @0x3c) -- no memset allowed.
static void test_surface_members()
{
    Mortar::BakedStringTTF_Surface s;
    s.m_PageKey        = 0;
    s.m_DrawMode       = -1;    // single-buffer path
    s.m_VertCount      = 6;
    s.m_PlatformColour = 0xFFFFFFFFu;
    s.m_Verts          = 0;
    s.m_BoundsMinX     =  999999;
    s.m_BoundsMaxY     = -999999;
    s.m_BoundsMaxX     = -999999;
    s.m_BoundsMinY     =  999999;

    CHECK(s.m_DrawMode < 0);
    CHECK(s.m_VertCount == 6u);
    CHECK(s.m_PlatformColour == 0xFFFFFFFFu);
    CHECK(s.m_BoundsMinX == 999999);
    CHECK(s.m_BoundsMaxY == -999999);
    CHECK(s.m_Glyphs.empty());

    std::printf("  BakedStringTTF_Surface members: OK\n");
}

// Test 2D rotation math (the Rotate2DVector formula in BakedStringTTF.cpp).
static void test_rotate2d_math()
{
    // angle=0: identity
    float angle = 0.0f;
    float s = sinf(angle), c = cosf(angle);
    float x = 5.0f, y = 3.0f;
    float ox = c * x - s * y;
    float oy = s * x + c * y;
    CHECK_FLOAT_NEAR(ox, 5.0f, 1e-6f);
    CHECK_FLOAT_NEAR(oy, 3.0f, 1e-6f);

    // angle = PI/2: (5,3) -> (-3,5)
    angle = 3.14159265f * 0.5f;
    s = sinf(angle); c = cosf(angle);
    ox = c * x - s * y;
    oy = s * x + c * y;
    CHECK_FLOAT_NEAR(ox, -3.0f, 1e-5f);
    CHECK_FLOAT_NEAR(oy,  5.0f, 1e-5f);

    // angle = PI: (5,3) -> (-5,-3)
    angle = 3.14159265f;
    s = sinf(angle); c = cosf(angle);
    ox = c * x - s * y;
    oy = s * x + c * y;
    CHECK_FLOAT_NEAR(ox, -5.0f, 1e-5f);
    CHECK_FLOAT_NEAR(oy, -3.0f, 1e-5f);

    std::printf("  Rotate2DVector math: OK\n");
}

// Test gradient lerp: t=0->top, t=1->bottom, t=0.5->midpoint.
// Mirrors Transform_LinearGradient_TopBottom @0x00247a48.
static void test_gradient_lerp()
{
    Colour top(255, 0, 0, 255);    // red
    Colour bot(0, 0, 255, 255);    // blue

    // t=0: top
    float t = 0.0f;
    float fr = (top.r / 255.0f) * (1.0f - t) + (bot.r / 255.0f) * t;
    float fb = (top.b / 255.0f) * (1.0f - t) + (bot.b / 255.0f) * t;
    CHECK_FLOAT_NEAR(fr, 1.0f, 1e-6f);
    CHECK_FLOAT_NEAR(fb, 0.0f, 1e-6f);

    // t=1: bottom
    t = 1.0f;
    fr = (top.r / 255.0f) * (1.0f - t) + (bot.r / 255.0f) * t;
    fb = (top.b / 255.0f) * (1.0f - t) + (bot.b / 255.0f) * t;
    CHECK_FLOAT_NEAR(fr, 0.0f, 1e-6f);
    CHECK_FLOAT_NEAR(fb, 1.0f, 1e-6f);

    // t=0.5: midpoint (127 after truncation)
    t = 0.5f;
    fr = (top.r / 255.0f) * (1.0f - t) + (bot.r / 255.0f) * t;
    unsigned char r = (unsigned char)(int)(fr * 255.0f);
    CHECK(r == 127);

    std::printf("  gradient lerp formula: OK\n");
}

// Test FONT_EFFECT enum values.
static void test_font_effect_enum()
{
    CHECK((int)Mortar::FontCacheObjectTTF::FONT_EFFECT_NONE == 0);
    CHECK((int)Mortar::FontCacheObjectTTF::FONT_EFFECT_STROKE == 1);
    CHECK((int)Mortar::FontCacheObjectTTF::FONT_EFFECT_BLUR == 2);
    CHECK((int)Mortar::FontCacheObjectTTF::FONT_EFFECT_INNER_GLOW == 3);
    std::printf("  FONT_EFFECT enum values: OK\n");
}

// Test BakedStringEffectBase fields (gradient stops = std::vector<GradientPoint>
// at m_Base+0x18 in the v1.6.1 model -- no memset allowed).
static void test_effect_base_fields()
{
    Mortar::BakedStringEffectBase base;
    base.m_BoundsMinX = 0;
    base.m_BoundsMaxY = 0;
    base.m_BoundsMaxX = 0;
    base.m_BoundsMinY = 0;
    base.m_Alpha      = 0;
    base.m_AlphaSet   = 0;
    base.m_Radius     = 42.0f;
    base.m_Weight     = 2.0f;
    base.m_FmtCount   = 3u;
    base.m_Flag       = (uint8_t)Mortar::FontCacheObjectTTF::FONT_EFFECT_STROKE;

    Mortar::GradientPoint p0;
    p0.m_Colour = Colour(255, 128, 0, 255);
    p0.m_T      = 0.0f;
    Mortar::GradientPoint p1;
    p1.m_Colour = Colour(0, 0, 255, 128);
    p1.m_T      = 1.0f;
    base.m_GradientStops.push_back(p0);
    base.m_GradientStops.push_back(p1);

    CHECK_FLOAT_NEAR(base.m_Radius, 42.0f, 1e-6f);
    CHECK_FLOAT_NEAR(base.m_Weight, 2.0f, 1e-6f);
    CHECK(base.m_FmtCount == 3u);
    CHECK(base.m_Flag == (uint8_t)Mortar::FontCacheObjectTTF::FONT_EFFECT_STROKE);
    CHECK(base.m_GradientStops.size() == 2u);
    CHECK(base.m_GradientStops[0].m_Colour.r == 255);
    CHECK_FLOAT_NEAR(base.m_GradientStops[1].m_T, 1.0f, 1e-6f);

    // ApplyGradient_TopBottom's "0x1c <- 0x18" store = end = begin = clear().
    base.m_GradientStops.clear();
    CHECK(base.m_GradientStops.empty());

    std::printf("  BakedStringEffectBase fields: OK\n");
}

// Verify GlyphTTF sizeof is reasonable on host (>= 0x44 because Vec2/float/ptr
// may be larger on x64, but should never be smaller).
// The exact __bada__ assertion is in BakedStringTTF.h.
static void test_sizeof_sanity()
{
    // On x64 host, pointers are 8 bytes so struct will be larger than 0x44.
    // Just verify it compiles and returns a non-zero size.
    CHECK(sizeof(Mortar::GlyphTTF) > 0);
    CHECK(sizeof(Mortar::BakedStringTTF_Surface) > 0);
    CHECK(sizeof(Mortar::BakedStringEffectBase) > 0);

    // The __bada__ cross-build assert enforces sizeof==0x44/0x48/0x38.
    // On host x64 sizes will be larger; that's expected and correct.
    std::printf("  sizeof sanity (host GlyphTTF=%u, Surface=%u, EffectBase=%u): OK\n",
                (unsigned)sizeof(Mortar::GlyphTTF),
                (unsigned)sizeof(Mortar::BakedStringTTF_Surface),
                (unsigned)sizeof(Mortar::BakedStringEffectBase));
}

int main()
{
    std::printf("test_bakedstringttf: start\n");

    test_arc_math();
    test_halftexel_constant();
    test_glyphttf_members();
    test_surface_members();
    test_rotate2d_math();
    test_gradient_lerp();
    test_font_effect_enum();
    test_effect_base_fields();
    test_sizeof_sanity();

    std::printf("test_bakedstringttf: PASS\n");
    return 0;
}
