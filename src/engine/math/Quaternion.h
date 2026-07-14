#ifndef MORTAR_QUATERNION_H
#define MORTAR_QUATERNION_H

#include "Vec3.h"
#include "Matrix44.h"
#include <cmath>
#include <cstdint>

// Matches original Quaternion (16 bytes)
struct Quaternion {
    float x, y, z, w;

    Quaternion() : x(0), y(0), z(0), w(1) {}
    Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    // Quaternion multiplication
    Quaternion operator*(const Quaternion& q) const {
        return Quaternion(
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z
        );
    }

    Quaternion normalized() const {
        float len = sqrtf(x * x + y * y + z * z + w * w);
        if (len < 1e-8f) return Quaternion();
        float inv = 1.0f / len;
        return Quaternion(x * inv, y * inv, z * inv, w * inv);
    }

    // Matches QuatFromAxisAngle used in Fruit::Update
    static Quaternion FromAxisAngle(const Vec3& axis, float angleRad) {
        float half = angleRad * 0.5f;
        float s = sinf(half);
        return Quaternion(axis.x * s, axis.y * s, axis.z * s, cosf(half));
    }

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
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

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

    static Quaternion Identity() { return Quaternion(0, 0, 0, 1); }

    // ASM-verified: 2026-05-06T00:00 v1.6.1 binary @ 0x0017ac68 (asm-inspector)
    // 16-bit angle encoding (0x10000 = 2pi). The axis is NOT normalized by
    // this function -- binary does not normalize, caller is responsible for
    // unit-length axes. Calls SinIdx 3x (one per component) + CosIdx 1x;
    // tail-calls Quaternion_Identity if cos(half)==0 (degenerate angle).
    void CreateFromAxisAngle(float ax, float ay, float az, uint32_t angle16) {
        const float rad  = (float)(int32_t)angle16 * (6.2831853f / 65536.0f);
        const float half = rad * 0.5f;
        const float s    = sinf(half);
        x = ax * s;
        y = ay * s;
        z = az * s;
        w = cosf(half);
    }
};

#endif
