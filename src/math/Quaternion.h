#ifndef FN_QUATERNION_H
#define FN_QUATERNION_H

#include "Vec3.h"
#include "Matrix44.h"
#include <cmath>

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
        Matrix44 m;
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;
        float wx = w * x, wy = w * y, wz = w * z;

        m.m[0]  = 1.0f - 2.0f * (yy + zz);
        m.m[1]  = 2.0f * (xy + wz);
        m.m[2]  = 2.0f * (xz - wy);
        m.m[3]  = 0.0f;
        m.m[4]  = 2.0f * (xy - wz);
        m.m[5]  = 1.0f - 2.0f * (xx + zz);
        m.m[6]  = 2.0f * (yz + wx);
        m.m[7]  = 0.0f;
        m.m[8]  = 2.0f * (xz + wy);
        m.m[9]  = 2.0f * (yz - wx);
        m.m[10] = 1.0f - 2.0f * (xx + yy);
        m.m[11] = 0.0f;
        m.m[12] = 0.0f; m.m[13] = 0.0f; m.m[14] = 0.0f; m.m[15] = 1.0f;
        return m;
    }

    static Quaternion Identity() { return Quaternion(0, 0, 0, 1); }
};

#endif
