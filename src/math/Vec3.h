#ifndef FN_VEC3_H
#define FN_VEC3_H

#include <cmath>

// Matches original _Vector3<float> (12 bytes)
struct Vec3 {
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(float s) const { float inv = 1.0f / s; return Vec3(x * inv, y * inv, z * inv); }

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }

    // Component-wise multiply (used in HUDControl3d::Draw)
    Vec3 operator*(const Vec3& o) const { return Vec3(x * o.x, y * o.y, z * o.z); }

    bool operator!=(const Vec3& o) const { return x != o.x || y != o.y || z != o.z; }
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }

    float length() const { return sqrtf(x * x + y * y + z * z); }
    Vec3 normalized() const { float l = length(); return l > 0 ? *this / l : Vec3(); }

    static Vec3 Zero() { return Vec3(0, 0, 0); }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

#endif
