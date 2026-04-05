#ifndef MORTAR_VEC3_H
#define MORTAR_VEC3_H

#include <cmath>

// Matches original _Vector3<float> (12 bytes: float x, y, z)
struct Vec3 {
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(float s) const { float inv = 1.0f / s; return Vec3(x * inv, y * inv, z * inv); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    // Matches operator/= (0x00138b40)
    Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }

    // Component-wise multiply (used in HUDControl3d::Draw)
    Vec3 operator*(const Vec3& o) const { return Vec3(x * o.x, y * o.y, z * o.z); }

    bool operator!=(const Vec3& o) const { return x != o.x || y != o.y || z != o.z; }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }

    // Matches Dot (0x00133c4c) — instance method
    float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    // Matches MagnitudeSqr (0x00133c74)
    float MagnitudeSqr() const { return Dot(*this); }

    // Matches Magnitude (0x00138cdc)
    float Magnitude() const { return sqrtf(MagnitudeSqr()); }

    // Matches Normalise (0x00138ce8) — in-place, returns original magnitude
    // Recursive retry with 1M scale-up for near-zero vectors
    float Normalise() {
        if (x == 0.0f && y == 0.0f && z == 0.0f) return 0.0f;
        float mag = Magnitude();
        if (mag == 0.0f) {
            *this *= 1000000.0f;  // DAT_00138d5c
            Normalise();          // recursive retry
        } else {
            *this /= mag;
        }
        return mag;
    }

    // Matches Cross (0x0017ea04) — static, matches ARM struct-return convention
    static Vec3 Cross(const Vec3& a, const Vec3& b) {
        return Vec3(
            a.y * b.z - b.y * a.z,
            b.x * a.z - a.x * b.z,
            a.x * b.y - b.x * a.y
        );
    }

    // Convenience aliases (port uses both styles)
    float length() const { return Magnitude(); }
    float lengthSq() const { return MagnitudeSqr(); }
    Vec3 normalized() const { float l = Magnitude(); return l > 0 ? *this / l : Vec3(); }
    static float dot(const Vec3& a, const Vec3& b) { return a.Dot(b); }
    static Vec3 cross(const Vec3& a, const Vec3& b) { return Cross(a, b); }
    float dot(const Vec3& o) const { return Dot(o); }
    Vec3 cross(const Vec3& o) const { return Cross(*this, o); }

    // Static constants matching _Vector3<float>::Zero / One from BSS
    static const Vec3& Zero() { static Vec3 z(0, 0, 0); return z; }
    static const Vec3& One()  { static Vec3 o(1, 1, 1); return o; }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

#endif
