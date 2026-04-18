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

    // Matches Quaternion::Matrix44Unit — convert quaternion to 4x4 rotation matrix
    Matrix44 ToMatrix44() const {
        Matrix44 mat;
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

        mat.m[0]  = 1.0f - 2.0f * (yy + zz);
        mat.m[1]  = 2.0f * (xy + wz);
        mat.m[2]  = 2.0f * (xz - wy);
        mat.m[3]  = 0.0f;
        mat.m[4]  = 2.0f * (xy - wz);
        mat.m[5]  = 1.0f - 2.0f * (xx + zz);
        mat.m[6]  = 2.0f * (yz + wx);
        mat.m[7]  = 0.0f;
        mat.m[8]  = 2.0f * (xz + wy);
        mat.m[9]  = 2.0f * (yz - wx);
        mat.m[10] = 1.0f - 2.0f * (xx + yy);
        mat.m[11] = 0.0f;
        mat.m[12] = 0.0f; mat.m[13] = 0.0f; mat.m[14] = 0.0f; mat.m[15] = 1.0f;
        return mat;
    }

    static Quaternion Identity() { return Quaternion(0, 0, 0, 1); }

    // Sets this quaternion to identity then computes axis-angle with 16-bit
    // angle encoding (0x10000 = 2pi). Matches Quaternion::CreateFromAxisAngle
    // @ 0x000f7e90. The axis is NOT normalized by this function (binary
    // does not normalize — caller is responsible for unit-length axes).
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
