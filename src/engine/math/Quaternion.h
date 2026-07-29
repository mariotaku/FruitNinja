#ifndef MORTAR_QUATERNION_H
#define MORTAR_QUATERNION_H

#include "_Vector3.h"
#include "Matrix44.h"
#include "MathUtil.h"
#include <cmath>
#include <cstdint>

// Templated `_Quaternion<T>` matching the binary's class name + ABI.
// Mangled symbols in FruitNinja.exe reference `8_QuaternionIfE` at global
// scope (Itanium ABI: name-length 8, name `_Quaternion`, template-arg
// `<float>`) -- same convention as `_Vector3<T>` / `_Vector2<T>`.
//
// Size 16, TRIVIAL (no dtor in the binary). Fields x,y,z,w @ 0/4/8/12.
template<class T>
struct _Quaternion {
    T x, y, z, w;

    _Quaternion() : x(0), y(0), z(0), w(1) {}
    _Quaternion(T x, T y, T z, T w) : x(x), y(y), z(z), w(w) {}

    // Quaternion multiplication
    _Quaternion operator*(const _Quaternion& q) const {
        return _Quaternion(
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z
        );
    }

    _Quaternion normalized() const {
        T len = T(std::sqrt((double)(x * x + y * y + z * z + w * w)));
        if (len < T(1e-8)) return _Quaternion();
        T inv = T(1) / len;
        return _Quaternion(x * inv, y * inv, z * inv, w * inv);
    }

    // NOTE: a radians-taking FromAxisAngle(axis, angleRad) used to live here. It was a
    // port invention -- v1.6.1 has NO radians axis-angle builder at all. The only
    // AxisAngle symbols in the binary are CreateFromAxisAngle @0x001bfe88 and a linker
    // veneer @0x00106e58 that thunks into it. Its comment even claimed to match
    // "QuatFromAxisAngle used in Fruit::Update", which Fruit::Update never called.
    // Removed; use CreateFromAxisAngle with a 16-bit angle index instead.

    // ASM-verified: 2026-05-06T00:00 v1.6.1 Matrix33Unit @0x001e3064 / Matrix44Unit @0x001e32cc / Copy33To44 @0x001bf3b8 (asm-inspector)
    // (Matrix33Unit + Copy33To44 pipeline collapsed into a single column-major write;
    //  bit-identical 16-float output -- verified against the binary's row-major M[]
    //  followed by the Copy33To44 padding/fill-with-0/1 pattern.)
    //
    // Matches _Quaternion::Matrix44Unit (calls Matrix33Unit @ 0x001e3064 then
    // Copy33To44 @ 0x001bf3b8). The binary writes the 9 rotation values in
    // ROW-MAJOR-flat order into a 16-float buffer; that buffer is uploaded to
    // GL (column-major), so the binary's effective rotation matrix is R(q)^T
    // = R(q^-1) -- the transpose of the standard Hamilton quaternion-to-matrix.
    //
    // Port stores column-major m[col*4+row]. To produce the SAME 16 floats
    // the binary's pipeline ends up handing to GL, we write the binary's
    // row-major layout directly into our column-major m[]. That is equivalent
    // to flipping the sign of every w-cross-product (off-diagonal) term --
    // i.e. transposing the standard Hamilton rotation matrix.
    Matrix44 ToMatrix44() const {
        Matrix44 mat;
        T xx = x * x, yy = y * y, zz = z * z;
        T xy = x * y, xz = x * z, yz = y * z;
        T wx = w * x, wy = w * y, wz = w * z;

        mat.m[0]  = 1.0f - 2.0f * (yy + zz);
        mat.m[1]  = 2.0f * (xy - wz);
        mat.m[2]  = 2.0f * (xz + wy);
        mat.m[3]  = 0.0f;
        mat.m[4]  = 2.0f * (xy + wz);
        mat.m[5]  = 1.0f - 2.0f * (xx + zz);
        mat.m[6]  = 2.0f * (yz - wx);
        mat.m[7]  = 0.0f;
        mat.m[8]  = 2.0f * (xz - wy);
        mat.m[9]  = 2.0f * (yz + wx);
        mat.m[10] = 1.0f - 2.0f * (xx + yy);
        mat.m[11] = 0.0f;
        mat.m[12] = 0.0f; mat.m[13] = 0.0f; mat.m[14] = 0.0f; mat.m[15] = 1.0f;
        return mat;
    }

    static _Quaternion Identity() { return _Quaternion(0, 0, 0, 1); }

    // ASM-spec v1.6.1 _Quaternion<float>::CreateFromAxisAngle @0x001bfe88:
    // half = angle16 >> 1 is a 16-bit LUT INDEX, not a radian value -- no
    // libm sin/cos call in the binary, pure SinIdx/CosIdx table lookup at
    // single precision. The axis is NOT normalized by this function --
    // binary does not normalize, caller is responsible for unit-length
    // axes. w = CosIdx(half); x/y/z = axis * SinIdx(half); if w == 0
    // (degenerate angle) the binary tail-calls Quaternion_Identity.
    void CreateFromAxisAngle(T ax, T ay, T az, uint32_t angle16) {
        const uint16_t half = (uint16_t)(angle16 >> 1);
        const T s = T(SinIdx(half));
        const T c = T(CosIdx(half));
        x = ax * s;
        y = ay * s;
        z = az * s;
        w = c;
        if (w == T(0)) {
            *this = Identity();
        }
    }
};

// The binary's `_Quaternion<float>` instantiation -- the port's sole
// Quaternion type. Bridge typedef keeps every existing name-based use
// (Fruit, Jiblet, SuperFruitControl, RenderInterp, tests) compiling
// unchanged; none depend on offsetof/sizeof, so no call-site edits needed.
typedef _Quaternion<float> Quaternion;

#endif
