// Colour unit test -- regression guard for channel order, packing, Lerp, and
// TintColour/TintWhite correctness.
//
// The binary stores Colour as BGRA bytes at offsets {0=b, 1=g, 2=r, 3=a}.
// The constructor takes (r,g,b,a) parameters but stores them reversed in memory.
// PlatformColour() packs as (a<<24)|(b<<16)|(g<<8)|r.
// This order has bitten the project before -- tests here pin it.
//
// Pure in-process test: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "math/Colour.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

// Verify struct field order: constructor takes (r,g,b,a), stored as BGRA bytes.
static void test_channel_order()
{
    Colour c(10, 20, 30, 40);
    // Header line 9: "uint8_t b, g, r, a;" -- ctor sets r=10, g=20, b=30, a=40.
    CHECK(c.r == 10);
    CHECK(c.g == 20);
    CHECK(c.b == 30);
    CHECK(c.a == 40);
}

// Default constructor: b=0, g=0, r=0, a=255.
static void test_default_ctor()
{
    Colour c;
    CHECK(c.r == 0);
    CHECK(c.g == 0);
    CHECK(c.b == 0);
    CHECK(c.a == 255);
}

// Default alpha: omitting the 4th argument gives a=255.
static void test_default_alpha()
{
    Colour c(1, 2, 3);
    CHECK(c.r == 1);
    CHECK(c.g == 2);
    CHECK(c.b == 3);
    CHECK(c.a == 255);
}

// PlatformColour() packing: (a<<24)|(b<<16)|(g<<8)|r.
// Verified against Colour.h line 17.
static void test_platform_colour_zero()
{
    Colour c(0, 0, 0, 0);
    CHECK(c.PlatformColour() == 0x00000000u);
}

static void test_platform_colour_all_ff()
{
    Colour c(255, 255, 255, 255);
    // a=255, b=255, g=255, r=255: (255<<24)|(255<<16)|(255<<8)|255 = 0xFFFFFFFF
    CHECK(c.PlatformColour() == 0xFFFFFFFFu);
}

static void test_platform_colour_mixed()
{
    // r=0x11, g=0x22, b=0x33, a=0x44
    Colour c(0x11, 0x22, 0x33, 0x44);
    // (a<<24)|(b<<16)|(g<<8)|r = (0x44<<24)|(0x33<<16)|(0x22<<8)|0x11
    //                           = 0x44332211
    uint32_t expected = (0x44u << 24) | (0x33u << 16) | (0x22u << 8) | 0x11u;
    CHECK(c.PlatformColour() == expected);
}

static void test_platform_colour_alpha_only()
{
    // r=0, g=0, b=0, a=0x80: only alpha set.
    Colour c(0, 0, 0, 0x80);
    uint32_t expected = (0x80u << 24);
    CHECK(c.PlatformColour() == expected);
}

static void test_platform_colour_red()
{
    // Pure red: r=255, g=0, b=0, a=255.
    // (255<<24)|(0<<16)|(0<<8)|255 = 0xFF0000FF
    Colour c(255, 0, 0, 255);
    CHECK(c.PlatformColour() == 0xFF0000FFu);
}

// Static constants pin the documented values.
static void test_static_constants()
{
    CHECK(Colour::Red.r   == 255 && Colour::Red.g   == 0   && Colour::Red.b   == 0   && Colour::Red.a   == 255);
    CHECK(Colour::White.r == 255 && Colour::White.g == 255 && Colour::White.b == 255 && Colour::White.a == 255);
    CHECK(Colour::Black.r == 0   && Colour::Black.g == 0   && Colour::Black.b == 0   && Colour::Black.a == 255);
}

// TintWhite: float (0..1) RGB -> Colour(rc, gc, bc, 255).
// Verified against Colour.h lines 21-26.
static void test_tint_white_red()
{
    Colour c = Colour::TintWhite(1.0f, 0.0f, 0.0f);
    CHECK(c.r == 255);
    CHECK(c.g == 0);
    CHECK(c.b == 0);
    CHECK(c.a == 255);
}

static void test_tint_white_grey()
{
    // 0.5*255 = 127 (truncates toward zero via (int) cast).
    Colour c = Colour::TintWhite(0.5f, 0.5f, 0.5f);
    CHECK(c.r == 127);
    CHECK(c.g == 127);
    CHECK(c.b == 127);
    CHECK(c.a == 255);
}

static void test_tint_white_clamp_negative()
{
    Colour c = Colour::TintWhite(-0.5f, 0.0f, 2.0f);
    CHECK(c.r == 0);    // clamped from negative
    CHECK(c.g == 0);
    CHECK(c.b == 255);  // clamped from >1
    CHECK(c.a == 255);
}

// TintColour: per-channel float multiplier, preserves alpha.
// Verified against Colour.h lines 39-44 and Clamp255.
static void test_tint_colour_identity()
{
    Colour src(100, 150, 200, 128);
    float tint[3] = {1.0f, 1.0f, 1.0f};
    Colour result = Colour::TintColour(src, tint);
    // 100*1=100.0 -> 100, 150*1=150.0 -> 150, 200*1=200.0 -> 200, alpha=128.
    CHECK(result.r == 100);
    CHECK(result.g == 150);
    CHECK(result.b == 200);
    CHECK(result.a == 128);
}

static void test_tint_colour_zero()
{
    Colour src(200, 150, 100, 255);
    float tint[3] = {0.0f, 0.0f, 0.0f};
    Colour result = Colour::TintColour(src, tint);
    CHECK(result.r == 0);
    CHECK(result.g == 0);
    CHECK(result.b == 0);
    CHECK(result.a == 255);
}

static void test_tint_colour_half()
{
    // 200*0.5 = 100.0, 100*0.5 = 50.0 -- exact integer results.
    Colour src(200, 100, 50, 255);
    float tint[3] = {0.5f, 0.5f, 0.5f};
    Colour result = Colour::TintColour(src, tint);
    CHECK(result.r == 100);
    CHECK(result.g == 50);
    CHECK(result.b == 25);
    CHECK(result.a == 255);
}

static void test_tint_colour_clamp_high()
{
    // 200*2 = 400.0 -> Clamp255 returns 255.
    Colour src(200, 100, 50, 128);
    float tint[3] = {2.0f, 2.0f, 2.0f};
    Colour result = Colour::TintColour(src, tint);
    CHECK(result.r == 255);
    CHECK(result.g == 200);
    CHECK(result.b == 100);
    CHECK(result.a == 128);
}

// toFloat: channels divided by 255.0, RGBA order.
// Verified against Colour.h lines 53-58.
static void test_to_float()
{
    Colour c(255, 0, 128, 64);
    float f[4];
    c.toFloat(f);
    // f[0]=r/255, f[1]=g/255, f[2]=b/255, f[3]=a/255
    CHECK(f[0] == 255.0f / 255.0f);
    CHECK(f[1] == 0.0f   / 255.0f);
    // 128/255 -- just check it's in (0,1) range and non-zero.
    CHECK(f[2] > 0.0f && f[2] < 1.0f);
    CHECK(f[3] > 0.0f && f[3] < 1.0f);
}

static void test_to_float_zero_alpha()
{
    Colour c(0, 0, 0, 0);
    float f[4];
    c.toFloat(f);
    CHECK(f[0] == 0.0f);
    CHECK(f[1] == 0.0f);
    CHECK(f[2] == 0.0f);
    CHECK(f[3] == 0.0f);
}

// Lerp 3-arg: formula is `a - (b-a)*t` per channel (binary Colour.cpp lines 23-42).
// Choose a.r=200, b.r=100, t=0.5: dr=100-200=-100; fr=200-(-100)*0.5=250 -- no clamp.
// Verify with a=(200,200,200,200), b=(100,100,100,100), t=0.5.
static void test_lerp_3arg_no_clamp()
{
    Colour result;
    Colour a(200, 200, 200, 200);
    Colour b(100, 100, 100, 100);
    result.Lerp(a, b, 0.5f);
    // dr = 100-200 = -100; fr = 200 - (-100)*0.5 = 250 -> (char)(int)250 = 250.
    CHECK(result.r == 250);
    CHECK(result.g == 250);
    CHECK(result.b == 250);
    CHECK(result.a == 250);
}

// Lerp 3-arg with positive delta (b > a): result = a - (b-a)*t.
// a.r=50, b.r=150, t=0.5: dr=100; fr=50-100*0.5=50-50=0.
static void test_lerp_3arg_zero_result()
{
    Colour result;
    Colour a(50, 50, 50, 50);
    Colour b(150, 150, 150, 150);
    result.Lerp(a, b, 0.5f);
    // fr = 50 - (150-50)*0.5 = 50 - 50 = 0. The check is (0.0 < 0.0) which is false
    // -> result clamped to 0.
    CHECK(result.r == 0);
    CHECK(result.g == 0);
    CHECK(result.b == 0);
    CHECK(result.a == 0);
}

// Lerp 3-arg at t=0: result = a (since delta*0 = 0).
static void test_lerp_3arg_t0()
{
    Colour result;
    Colour a(10, 20, 30, 40);
    Colour b(200, 200, 200, 200);
    result.Lerp(a, b, 0.0f);
    CHECK(result.r == 10);
    CHECK(result.g == 20);
    CHECK(result.b == 30);
    CHECK(result.a == 40);
}

// Lerp 2-arg: `self.Lerp(other, t)` delegates to `3arg(other, self, t)`.
// So result = other - (self - other)*t.
// self=(200,200,200,200), other=(100,100,100,100), t=0.5:
//   3arg(other=100, self=200, t=0.5): dr=200-100=100; fr=100-100*0.5=50.
static void test_lerp_2arg()
{
    Colour self(200, 200, 200, 200);
    Colour other(100, 100, 100, 100);
    self.Lerp(other, 0.5f);
    // result = other - (self - other)*0.5 = 100 - 100*0.5 = 50.
    CHECK(self.r == 50);
    CHECK(self.g == 50);
    CHECK(self.b == 50);
    CHECK(self.a == 50);
}

// Lerp 2-arg at t=0: result = other (first arg).
static void test_lerp_2arg_t0()
{
    Colour self(200, 200, 200, 200);
    Colour other(50, 60, 70, 80);
    self.Lerp(other, 0.0f);
    // 3arg(other, self, 0.0): result = other - 0 = other.
    CHECK(self.r == 50);
    CHECK(self.g == 60);
    CHECK(self.b == 70);
    CHECK(self.a == 80);
}

// ToString format: "%d, %d, %d, %d (argb)" with args (a, r, g, b).
// Verified against Colour.cpp lines 54-58.
static void test_tostring()
{
    Colour c(1, 2, 3, 4);
    // a=4, r=1, g=2, b=3: "4, 1, 2, 3 (argb)"
    char* s = c.ToString();
    CHECK(std::strcmp(s, "4, 1, 2, 3 (argb)") == 0);
}

static void test_tostring_white()
{
    char* s = Colour::White.ToString();
    // a=255, r=255, g=255, b=255
    CHECK(std::strcmp(s, "255, 255, 255, 255 (argb)") == 0);
}

int main()
{
    std::printf("test_colour: start\n");

    test_channel_order();
    std::printf("  channel order (BGRA storage, RGB ctor params): OK\n");

    test_default_ctor();
    std::printf("  default ctor (r=g=b=0, a=255): OK\n");

    test_default_alpha();
    std::printf("  default alpha=255: OK\n");

    test_platform_colour_zero();
    std::printf("  PlatformColour all-zero: OK\n");

    test_platform_colour_all_ff();
    std::printf("  PlatformColour all-0xFF: OK\n");

    test_platform_colour_mixed();
    std::printf("  PlatformColour mixed channels (0x44332211): OK\n");

    test_platform_colour_alpha_only();
    std::printf("  PlatformColour alpha-only: OK\n");

    test_platform_colour_red();
    std::printf("  PlatformColour pure red (0xFF0000FF): OK\n");

    test_static_constants();
    std::printf("  static constants Red/White/Black: OK\n");

    test_tint_white_red();
    std::printf("  TintWhite(1,0,0): OK\n");

    test_tint_white_grey();
    std::printf("  TintWhite(0.5,0.5,0.5) -> 127: OK\n");

    test_tint_white_clamp_negative();
    std::printf("  TintWhite clamp negative/overflow: OK\n");

    test_tint_colour_identity();
    std::printf("  TintColour identity tint: OK\n");

    test_tint_colour_zero();
    std::printf("  TintColour zero tint: OK\n");

    test_tint_colour_half();
    std::printf("  TintColour 0.5 tint: OK\n");

    test_tint_colour_clamp_high();
    std::printf("  TintColour clamp high: OK\n");

    test_to_float();
    std::printf("  toFloat RGBA order: OK\n");

    test_to_float_zero_alpha();
    std::printf("  toFloat all-zero: OK\n");

    test_lerp_3arg_no_clamp();
    std::printf("  Lerp 3-arg (a=200,b=100,t=0.5) -> 250: OK\n");

    test_lerp_3arg_zero_result();
    std::printf("  Lerp 3-arg (a=50,b=150,t=0.5) -> 0 (clamp): OK\n");

    test_lerp_3arg_t0();
    std::printf("  Lerp 3-arg at t=0 -> a: OK\n");

    test_lerp_2arg();
    std::printf("  Lerp 2-arg (self=200,other=100,t=0.5) -> 50: OK\n");

    test_lerp_2arg_t0();
    std::printf("  Lerp 2-arg at t=0 -> other: OK\n");

    test_tostring();
    std::printf("  ToString format (a,r,g,b) argb order: OK\n");

    test_tostring_white();
    std::printf("  ToString White: OK\n");

    std::printf("test_colour: PASS\n");
    return 0;
}
