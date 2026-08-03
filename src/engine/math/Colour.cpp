#include "Colour.h"

#include <cstdio>
#include <cmath>

// TODO: v1.6.1 0x0021e900 (Colour::Lerp(Colour const&, float) const) --
//   the earlier "binary mutates *this" reading was wrong; it came from a
//   stale v1.5.1 address. In v1.6.1 the mangled name is
//   _ZNK6Colour4LerpERKS_f and the return type is `Colour` by value, so the
//   ABI is r0=sret, r1=this, r2=&a, s0=t (Colour is non-POD in the binary --
//   it has a user-declared dtor -- so it returns in memory). The body copies
//   *this to sp+4 and a to sp+0, then calls the 3-arg overload as
//   Lerp(sret, /*p1=*/*this, /*p2=*/a, t); *this is never written.
//   Correct semantic:  return a + (*this - a) * t.
//   The port both mutates *this and passes the two colours in the opposite
//   order, so it computes *this + (a - *this) * t. Fixing it changes the
//   public signature to `Colour Lerp(...) const` and invalidates
//   tests/test_colour.cpp test_lerp_2arg_t0_pins_known_divergence.
Colour* Colour::Lerp(Colour const& a, float t) const {
    Colour ca = a;
    Colour cb = *this;
    return const_cast<Colour*>(this)->Lerp(ca, cb, t);
}

// ASM-spec v1.6.1 Colour::Lerp(Colour, Colour, float) @ 0x0021e828:
//   ABI is r0=this, r1=&a, r2=&b, s0=t (both Colour params are non-POD, so
//   they arrive by invisible reference). The prologue stores *r2 into all four
//   bytes of *this, i.e. the seed is `*this = b`:
//       0021e82c ldrb r6,[r2,#2] ; 0021e840 strb r6,[r0,#2]
//       0021e838 ldrb r5,[r2,#1] ; 0021e84c strb r5,[r0,#1]
//       0021e83c ldrb r4,[r2,#0] ; 0021e85c strb r4,[r0,#0]
//       0021e860 ldrb r12,[r2,#3]; 0021e864 strb r12,[r0,#3]
//   The delta term is rsb r6,r6,r7 == b.ch - a.ch, then
//   vmls s15,s14,s0 == b.ch - (b.ch - a.ch)*t.
//   Result semantic: b + (a - b) * t, i.e. Lerp(a,b,t) walks FROM b (t=0)
//   TO a (t=1). Do not "correct" the seed back to `a` -- see the continuity
//   argument in tests/test_colour.cpp test_lerp_from_array_interpolate.
// The delta is a signed int subtraction of the unsigned byte channels,
// int->float vcvt, multiplied by t. The negative clamp in the binary is
// `vcvt.u32.f32` (round toward zero, saturates <0 to 0) + strb, which is
// value-identical to the (0.0f < f) ? (char)(int)f : 0 idiom used here.
Colour* Colour::Lerp(Colour a, Colour b, float t) {
    *this = b;

    // Channel order in the binary's BGRA struct: offset 0=b, 1=g, 2=r, 3=a.
    float db = (float)((int)b.b - (int)a.b);
    float dg = (float)((int)b.g - (int)a.g);
    float dr = (float)((int)b.r - (int)a.r);
    float da = (float)((int)b.a - (int)a.a);

    float fr = (float)this->r - dr * t;
    float fg = (float)this->g - dg * t;
    float fb = (float)this->b - db * t;
    float fa = (float)this->a - da * t;

    this->r = (uint8_t)((0.0f < fr) ? (char)(int)fr : 0);
    this->g = (uint8_t)((0.0f < fg) ? (char)(int)fg : 0);
    this->b = (uint8_t)((0.0f < fb) ? (char)(int)fb : 0);
    this->a = (uint8_t)((0.0f < fa) ? (char)(int)fa : 0);
    return this;
}

// Static colour constants. Binary keeps them in BSS (Black 0x0034e2f4,
// White 0x0034e2f8, Red 0x0034e2fc) zero-init, then fills them in
// global.constructors.keyed.to.Colour.cpp @ 0x0021e9b8.
// DIFFERS: original = non-const BSS objects with an atexit'd ~Colour
// (v1.6.1 global ctors keyed to Colour.cpp @0x0021e9b8), using const because
// nothing in the port writes them and const avoids a dynamic-init order trap.
const Colour Colour::Red(255, 0, 0, 255);
const Colour Colour::White(255, 255, 255, 255);
const Colour Colour::Black(0, 0, 0, 255);

// ASM-spec v1.6.1 LerpColourFromArray @ 0x0014f254:
//   ABI: r0=sret, r1=arr, r2=count, s0=t. Binary control flow:
//     vcmpe.f32 s0, 1.0 ; subge r2,r2,#1 ; addge r1,r1,r2,lsl#2 ; bge L_ret
//     vcmpe.f32 s0, #0  ; r3 = (t <= 0) ; cmp r2,#1 ; orreq r3,r3,#1
//     cmp r3,#0 ; beq L_main            ; (falls through to L_ret with r1=arr)
//     L_ret: Colour::Colour(sret, r1)   ; return
//   so:
//     t >= 1.0f             -> return arr[count-1]
//     t <= 0.0f || count==1 -> return arr[0]   (r1 is unmodified on that path)
//     otherwise             -> interpolate
//   The binary has no NaN test: vcmpe leaves C=1,Z=0 for unordered, so NaN
//   takes neither `bge` nor `ls` and reaches the main path, where ARM's
//   vcvt.s32.f32 yields idx 0. A host x86 cvttss2si yields INT_MIN instead --
//   no port-side guard is added because the binary has none, and no caller
//   can feed a NaN here.
Colour LerpColourFromArray(float t, Colour* arr, int count) {
    if (t >= 1.0f) {
        return arr[count - 1];
    }
    if (t <= 0.0f || count == 1) {
        return arr[0];
    }
    float scaled = t * (float)(count - 1);
    int idx = (int)scaled;
    float frac = (float)fmod((double)scaled, 1.0);
    Colour result;
    // a=arr[idx+1], b=arr[idx] -- with Lerp's `b + (a-b)*t` seeding this walks
    // arr[idx] -> arr[idx+1] as frac goes 0 -> 1, which is what makes the
    // t -> 1 limit meet the `t >= 1.0f` early-out value arr[count-1].
    result.Lerp(arr[idx + 1], arr[idx], frac);
    return result;
}

// ASM-spec v1.6.1 Colour::ToString @ 0x0021e95c:
// snprintf into a single shared 0x100 static buffer (0x0034e1f4); format is
// "%d, %d, %d, %d (argb)" with args (a, r, g, b). Returns the buffer.
char* Colour::ToString() const {
    static char s_buf[0x100];
    snprintf(s_buf, sizeof(s_buf), "%d, %d, %d, %d (argb)",
             (unsigned)this->a, (unsigned)this->r,
             (unsigned)this->g, (unsigned)this->b);
    return s_buf;
}
