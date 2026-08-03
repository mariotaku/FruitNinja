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

// ---------------------------------------------------------------------------
// Colour::Lerp(Colour a, Colour b, float t) -- v1.6.1 @ 0x0021e828.
//
// The binary SEEDS *this FROM THE SECOND PARAMETER (`ldrb rN,[r2,#k]` ->
// `strb rN,[r0,#k]`), then subtracts the scaled delta:
//     *this = b;   this->ch -= (b.ch - a.ch) * t
// which is  result = b + (a - b) * t.  So t=0 gives b and t=1 gives a --
// Lerp(a, b, t) walks FROM b TO a, not the other way round.
//
// Do NOT "fix" the seeding order back to `a`: the tests below fail if you do,
// and test_lerp_from_array_interpolate shows independently why `b` is right
// (it is the only seeding that makes the gradient continuous at t == 1).
// ---------------------------------------------------------------------------

// t=0 must return b exactly. a and b are deliberately different in every
// channel so a swapped seed cannot pass by coincidence.
static void test_lerp_3arg_t0()
{
    Colour result;
    Colour a(10, 20, 30, 40);
    Colour b(200, 201, 202, 203);
    result.Lerp(a, b, 0.0f);
    // b + (a-b)*0 = b.
    CHECK(result.r == 200);
    CHECK(result.g == 201);
    CHECK(result.b == 202);
    CHECK(result.a == 203);
}

// t=1 must return a exactly.
static void test_lerp_3arg_t1()
{
    Colour result;
    Colour a(10, 20, 30, 40);
    Colour b(200, 201, 202, 203);
    result.Lerp(a, b, 1.0f);
    // b + (a-b)*1 = a. Per channel: 200 - (200-10)*1 = 10, etc.
    CHECK(result.r == 10);
    CHECK(result.g == 20);
    CHECK(result.b == 30);
    CHECK(result.a == 40);
}

// Midpoint, all channels land in range so the clamp is not involved.
static void test_lerp_3arg_no_clamp()
{
    Colour result;
    Colour a(200, 200, 200, 200);
    Colour b(100, 100, 100, 100);
    result.Lerp(a, b, 0.5f);
    // seed = b = 100; delta = b-a = -100; f = 100 - (-100)*0.5 = 150.
    CHECK(result.r == 150);
    CHECK(result.g == 150);
    CHECK(result.b == 150);
    CHECK(result.a == 150);
}

// The binary puts no clamp on t, so t outside [0,1] extrapolates. Negative
// results hit the `vcvt.u32.f32` saturation (round-to-zero, <0 -> 0), which
// the port spells (0.0f < f) ? (char)(int)f : 0.
// t=2 with b > a drives every channel negative.
static void test_lerp_3arg_negative_clamp()
{
    Colour result;
    Colour a(10, 20, 30, 40);
    Colour b(100, 100, 100, 100);
    result.Lerp(a, b, 2.0f);
    // r: 100 - (100-10)*2 = -80 -> 0.  g: 100 - 80*2 = -60 -> 0.
    // b: 100 - 70*2 = -40 -> 0.        a: 100 - 60*2 = -20 -> 0.
    CHECK(result.r == 0);
    CHECK(result.g == 0);
    CHECK(result.b == 0);
    CHECK(result.a == 0);
}

// Lerp 2-arg: `self.Lerp(other, t)` -> 3arg(a=other, b=self, t),
// so the port computes self + (other - self)*t.
// At t=0.5 that coincides with the binary's a + (self - a)*t, so this case is
// traceable to 0x0021e900 despite the outstanding arg-order divergence.
// self=(200,200,200,200), other=(100,100,100,100), t=0.5:
//   seed = self = 200; delta = self-other = 100; f = 200 - 100*0.5 = 150.
static void test_lerp_2arg()
{
    Colour self(200, 200, 200, 200);
    Colour other(100, 100, 100, 100);
    self.Lerp(other, 0.5f);
    CHECK(self.r == 150);
    CHECK(self.g == 150);
    CHECK(self.b == 150);
    CHECK(self.a == 150);
}

// NOT binary-traceable. This pins the KNOWN divergence documented by the
// // TODO at v1.6.1 0x0021e900 in Colour.h/Colour.cpp: the binary returns a
// new Colour by value (sret) and passes (p1=*this, p2=other), so at t=0 it
// yields OTHER, i.e. (50,60,70,80). The port mutates *this and passes the
// pair swapped, so it yields SELF. When the 2-arg overload is ported to its
// real `Colour Lerp(Colour const&, float) const` signature, delete this test
// and assert (50,60,70,80) instead.
static void test_lerp_2arg_t0_pins_known_divergence()
{
    Colour self(200, 200, 200, 200);
    Colour other(50, 60, 70, 80);
    self.Lerp(other, 0.0f);
    CHECK(self.r == 200);
    CHECK(self.g == 200);
    CHECK(self.b == 200);
    CHECK(self.a == 200);
}

// ---------------------------------------------------------------------------
// LerpColourFromArray -- v1.6.1 @ 0x0014f254.
//   t >= 1.0f             -> arr[count-1]
//   t <= 0.0f || count==1 -> arr[0]
//   otherwise             -> Lerp(arr[idx+1], arr[idx], frac)
// Every array below carries one extra trailing SENTINEL element that `count`
// excludes. Any read past arr[count-1] shows up as a sentinel-tinted result,
// which is how the t == 1.0f one-past-the-end read is caught.
// ---------------------------------------------------------------------------

// Fills a 4-entry gradient plus a 5th sentinel; count passed to the function
// is 4, so arr[4] must never be read.
static void make_gradient(Colour* arr)
{
    arr[0] = Colour(0, 10, 20, 30);
    arr[1] = Colour(100, 110, 120, 130);
    arr[2] = Colour(200, 200, 200, 200);
    arr[3] = Colour(60, 61, 62, 63);
    arr[4] = Colour(7, 7, 7, 7);      // sentinel -- out of bounds for count=4
}

// t == 1.0f takes the `vcmpe.f32 s0,1.0 / bge` path: arr[count-1].
// Before the guard existed this fell through to the main path, computed
// idx == count-1 and read arr[count] -- the sentinel.
static void test_lerp_from_array_t1_no_overread()
{
    Colour arr[5];
    make_gradient(arr);
    Colour c = LerpColourFromArray(1.0f, arr, 4);
    CHECK(c.r == 60 && c.g == 61 && c.b == 62 && c.a == 63);   // arr[3]
    // Explicit: the sentinel must not have leaked into the result.
    CHECK(!(c.r == 7 && c.g == 7 && c.b == 7 && c.a == 7));
    CHECK(arr[4].r == 7 && arr[4].g == 7 && arr[4].b == 7 && arr[4].a == 7);
}

// t > 1.0f takes the same `bge` path.
static void test_lerp_from_array_t_above_1()
{
    Colour arr[5];
    make_gradient(arr);
    Colour c = LerpColourFromArray(4.0f, arr, 4);
    CHECK(c.r == 60 && c.g == 61 && c.b == 62 && c.a == 63);   // arr[3]
}

// t == 0.0f: r3 = (t <= 0) is set, so the function falls through to L_ret with
// r1 still == arr -- arr[0], NOT arr[count-1].
static void test_lerp_from_array_t0()
{
    Colour arr[5];
    make_gradient(arr);
    Colour c = LerpColourFromArray(0.0f, arr, 4);
    CHECK(c.r == 0 && c.g == 10 && c.b == 20 && c.a == 30);    // arr[0]
}

// t < 0 is the same `t <= 0` path.
static void test_lerp_from_array_t_negative()
{
    Colour arr[5];
    make_gradient(arr);
    Colour c = LerpColourFromArray(-0.5f, arr, 4);
    CHECK(c.r == 0 && c.g == 10 && c.b == 20 && c.a == 30);    // arr[0]
}

// count == 1: `cmp r2,#1 / orreq r3,r3,#1` forces the early-out even for a
// mid-range t. Result is arr[0], which for count==1 is also the only element.
static void test_lerp_from_array_count1()
{
    Colour arr[5];
    make_gradient(arr);
    Colour c = LerpColourFromArray(0.5f, arr, 1);
    CHECK(c.r == 0 && c.g == 10 && c.b == 20 && c.a == 30);    // arr[0]
    // arr[1] would be the wrong answer -- make the failure mode explicit.
    CHECK(!(c.r == 100 && c.g == 110 && c.b == 120 && c.a == 130));
}

// Main path. count=4 so scaled = t*3.
//   t=0.25 -> scaled=0.75, idx=0, frac=0.75
//   Lerp(a=arr[1], b=arr[0], 0.75) = arr[0] + (arr[1]-arr[0])*0.75
//   r: 0 + 100*0.75 = 75.  g: 10 + 100*0.75 = 85.
//   b: 20 + 100*0.75 = 95. a: 30 + 100*0.75 = 105.
// This is the case that proves the seed must be `b`: with the old `a` seed the
// same call returned arr[1] + (arr[1]-arr[0])*frac, which runs AWAY from
// arr[0] and does not meet arr[count-1] as t -> 1.
static void test_lerp_from_array_interpolate()
{
    Colour arr[5];
    make_gradient(arr);
    Colour c = LerpColourFromArray(0.25f, arr, 4);
    CHECK(c.r == 75);
    CHECK(c.g == 85);
    CHECK(c.b == 95);
    CHECK(c.a == 105);
}

// Second segment: t=0.5 -> scaled=1.5, idx=1, frac=0.5
//   Lerp(a=arr[2], b=arr[1], 0.5) = arr[1] + (arr[2]-arr[1])*0.5
//   r: 100 + 100*0.5 = 150.  g: 110 + 90*0.5 = 155.
//   b: 120 + 80*0.5 = 160.   a: 130 + 70*0.5 = 165.
static void test_lerp_from_array_second_segment()
{
    Colour arr[5];
    make_gradient(arr);
    Colour c = LerpColourFromArray(0.5f, arr, 4);
    CHECK(c.r == 150);
    CHECK(c.g == 155);
    CHECK(c.b == 160);
    CHECK(c.a == 165);
}

// Continuity check: the main path's limit as t -> 1 must equal the value the
// t >= 1.0f early-out returns. Sampling just under a segment boundary
// (t=0.999 -> scaled=2.997, idx=2, frac=0.997) has to land next to arr[3].
static void test_lerp_from_array_continuous_at_end()
{
    Colour arr[5];
    make_gradient(arr);
    Colour c = LerpColourFromArray(0.999f, arr, 4);
    Colour end = LerpColourFromArray(1.0f, arr, 4);
    // arr[2]=(200,200,200,200) -> arr[3]=(60,61,62,63); at frac 0.997 the
    // result is within a couple of steps of arr[3] in every channel.
    CHECK(c.r >= end.r && c.r <= end.r + 2);
    CHECK(c.g >= end.g && c.g <= end.g + 2);
    CHECK(c.b >= end.b && c.b <= end.b + 2);
    CHECK(c.a >= end.a && c.a <= end.a + 2);
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

    test_lerp_3arg_t0();
    std::printf("  Lerp 3-arg at t=0 -> b: OK\n");

    test_lerp_3arg_t1();
    std::printf("  Lerp 3-arg at t=1 -> a: OK\n");

    test_lerp_3arg_no_clamp();
    std::printf("  Lerp 3-arg (a=200,b=100,t=0.5) -> 150: OK\n");

    test_lerp_3arg_negative_clamp();
    std::printf("  Lerp 3-arg (a=10..,b=100,t=2) -> 0 (clamp): OK\n");

    test_lerp_2arg();
    std::printf("  Lerp 2-arg (self=200,other=100,t=0.5) -> 150: OK\n");

    test_lerp_2arg_t0_pins_known_divergence();
    std::printf("  Lerp 2-arg at t=0 (pins known 0x0021e900 divergence): OK\n");

    test_lerp_from_array_t1_no_overread();
    std::printf("  LerpColourFromArray t=1 -> arr[count-1], no over-read: OK\n");

    test_lerp_from_array_t_above_1();
    std::printf("  LerpColourFromArray t>1 -> arr[count-1]: OK\n");

    test_lerp_from_array_t0();
    std::printf("  LerpColourFromArray t=0 -> arr[0]: OK\n");

    test_lerp_from_array_t_negative();
    std::printf("  LerpColourFromArray t<0 -> arr[0]: OK\n");

    test_lerp_from_array_count1();
    std::printf("  LerpColourFromArray count=1 -> arr[0]: OK\n");

    test_lerp_from_array_interpolate();
    std::printf("  LerpColourFromArray t=0.25 -> segment 0 interpolate: OK\n");

    test_lerp_from_array_second_segment();
    std::printf("  LerpColourFromArray t=0.5 -> segment 1 interpolate: OK\n");

    test_lerp_from_array_continuous_at_end();
    std::printf("  LerpColourFromArray continuous into the t>=1 early-out: OK\n");

    test_tostring();
    std::printf("  ToString format (a,r,g,b) argb order: OK\n");

    test_tostring_white();
    std::printf("  ToString White: OK\n");

    std::printf("test_colour: PASS\n");
    return 0;
}
